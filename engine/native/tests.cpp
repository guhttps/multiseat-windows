#include "seat-lifecycle.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

void identity_tests();

namespace {
using namespace multiseat::native;
using namespace std::chrono_literals;

// Inert principals exercise policy only. They are never Windows identities,
// platform receipts or evidence that another terminal exists.
struct FixtureIdentity {
    std::uint32_t user, session, logon;
    std::uint32_t SessionId() const noexcept { return session; }
    bool SameUser(const FixtureIdentity& other) const noexcept { return user == other.user; }
    bool SameLogon(const FixtureIdentity& other) const noexcept {
        return SameUser(other) && session == other.session && logon == other.logon;
    }
};
using Policy = BasicSeatLifecycle<FixtureIdentity>;
constexpr FixtureIdentity first{1, 11, 101}, second{2, 12, 102};
const auto initial = Policy::TimePoint{100ms};

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
SeatResources resources(std::uint64_t base) {
    return {{ResourceKind::Monitor, base, 1},
            {ResourceKind::Keyboard, base + 1, 1},
            {ResourceKind::Mouse, base + 2, 1}};
}
Reservation take(ReservationResult result) {
    require(result.error == ReservationError::None && result.reservation.has_value(),
            "A valid noninteractive reservation was rejected.");
    return std::move(*result.reservation);
}
void rejected(const ReservationResult& result, ReservationError expected) {
    require(result.error == expected && !result.reservation,
            "A rejected reservation returned the wrong result or a lease.");
}

void admission_tests() {
    Policy policy;
    auto invalid = resources(10);
    invalid.keyboard.kind = ResourceKind::Monitor;
    rejected(policy.Reserve(Seat::First, first, invalid, initial, 1s), ReservationError::InvalidResources);
    invalid = resources(10); invalid.mouse.id = 0;
    rejected(policy.Reserve(Seat::First, first, invalid, initial, 1s), ReservationError::InvalidResources);
    invalid = resources(10); invalid.monitor.arrival_generation = 0;
    rejected(policy.Reserve(Seat::First, first, invalid, initial, 1s), ReservationError::InvalidResources);
    rejected(policy.Reserve(static_cast<Seat>(3), first, resources(10), initial, 1s), ReservationError::InvalidSeat);
    rejected(policy.Reserve(Seat::First, first, resources(10), initial, 0ms), ReservationError::InvalidLifetime);
    rejected(policy.Reserve(Seat::First, first, resources(10), initial, -1ms), ReservationError::InvalidLifetime);
    rejected(policy.Reserve(Seat::First, first, resources(10), initial, Policy::MaximumLifetime + 1ns),
             ReservationError::InvalidLifetime);
    rejected(policy.Reserve(Seat::First, FixtureIdentity{1, 0, 101}, resources(10), initial, 1s), ReservationError::InvalidIdentity);
    auto one = take(policy.Reserve(Seat::First, first, resources(10), initial, 1s));
    rejected(policy.Reserve(Seat::Second, FixtureIdentity{1, 12, 201}, resources(20), initial, 1s), ReservationError::IdentityConflict);
    rejected(policy.Reserve(Seat::Second, FixtureIdentity{2, 11, 202}, resources(20), initial, 1s), ReservationError::IdentityConflict);
    auto last_collision = resources(20);
    last_collision.mouse = resources(10).mouse;
    rejected(policy.Reserve(Seat::Second, second, last_collision, initial, 1s), ReservationError::ResourceConflict);
    require(policy.View(Seat::Second)->phase == Phase::Empty && policy.IsCurrent(one, initial),
            "Failed admission changed an existing reservation.");
    auto two = take(policy.Reserve(Seat::Second, second, resources(20), initial, 1s));
    require(policy.IsCurrent(one, initial) && policy.IsCurrent(two, initial), "Distinct reservations did not coexist.");
    require(!policy.View(static_cast<Seat>(3)), "Invalid seat returned a view.");
    require(policy.Cancel(one) && policy.IsCurrent(two, initial), "Cancel affected the peer.");
    require(policy.Cancel(two), "Peer cancellation failed.");

    Policy overflow;
    rejected(overflow.Reserve(Seat::First, first, resources(10), Policy::TimePoint::max() - 1ms, 2ms),
             ReservationError::InvalidLifetime);
}

void incarnation_tests() {
    Policy policy;
    auto one = take(policy.Reserve(Seat::First, first, resources(10), initial, 1s));
    auto two = take(policy.Reserve(Seat::Second, second, resources(20), initial, 1s));
    require(!policy.Suspend(one, Suspension::None) && !policy.Suspend(one, static_cast<Suspension>(255)) &&
            policy.IsCurrent(one, initial), "An invalid suspension changed eligibility.");
    rejected(policy.Reassociate(one, first, resources(10), initial, 1s), ReservationError::Occupied);
    require(policy.Suspend(one, Suspension::SecureDesktop), "Expected-generation suspension failed.");
    require(!policy.IsCurrent(one, initial) && policy.IsCurrent(two, initial), "Suspension affected the peer or kept eligibility.");
    auto conflict = resources(10);
    conflict.mouse = resources(20).mouse;
    rejected(policy.Reassociate(one, first, conflict, initial, 1s), ReservationError::ResourceConflict);
    require(policy.View(Seat::First)->phase == Phase::Suspended && policy.IsCurrent(two, initial),
            "Failed reassociation changed existing reservations.");
    auto replacement = take(policy.Reassociate(one, first, resources(10), initial, 1s));
    require(replacement.Generation() > one.Generation() && !policy.Cancel(one) &&
            !policy.Suspend(one, Suspension::Locked) && !policy.IsCurrent(one, initial),
            "An old lease affected a replacement reservation.");
    require(policy.IsCurrent(replacement, initial), "A stale operation changed the replacement.");
    require(policy.Suspend(replacement, Suspension::Disconnected), "Disconnect suspension failed.");
    rejected(policy.Reassociate(replacement, FixtureIdentity{1, 11, 999}, resources(10), initial, 1s),
             ReservationError::IdentityConflict);
    require(policy.Cancel(replacement) && policy.IsCurrent(two, initial), "Incarnation cancellation affected the peer.");
    auto fresh = take(policy.Reserve(Seat::First, FixtureIdentity{3, 13, 103}, resources(30), initial, 1s));
    require(!policy.Cancel(replacement) && policy.IsCurrent(fresh, initial), "A stale cancellation crossed account incarnations.");

    Policy suspended_claims;
    auto retained = take(suspended_claims.Reserve(Seat::First, first, resources(10), initial, 1s));
    require(suspended_claims.Suspend(retained, Suspension::Locked), "Lock suspension failed.");
    rejected(suspended_claims.Reserve(Seat::Second, FixtureIdentity{1, 12, 201}, resources(20), initial, 1s),
             ReservationError::IdentityConflict);
    auto stolen = resources(20); stolen.keyboard = resources(10).keyboard;
    rejected(suspended_claims.Reserve(Seat::Second, second, stolen, initial, 1s), ReservationError::ResourceConflict);
    auto peer = take(suspended_claims.Reserve(Seat::Second, second, resources(20), initial, 1s));
    require(suspended_claims.IsCurrent(peer, initial) && !suspended_claims.IsCurrent(retained, initial),
            "Suspended claims were transferred or silently resumed.");
}

void arrival_tests() {
    Policy policy;
    const auto original = resources(10);
    auto one = take(policy.Reserve(Seat::First, first, original, initial, 1s));
    auto two = take(policy.Reserve(Seat::Second, second, resources(20), initial, 1s));
    require(policy.Invalidate(original.keyboard), "A known keyboard removal was ignored.");
    require(!policy.IsCurrent(one, initial) && policy.IsCurrent(two, initial), "Removal crossed reservations.");
    rejected(policy.Reassociate(one, first, original, initial, 1s), ReservationError::StaleEndpoint);
    auto arrived = original;
    ++arrived.keyboard.arrival_generation;
    auto replacement = take(policy.Reassociate(one, first, arrived, initial, 1s));
    require(!policy.Invalidate(original.keyboard) && policy.IsCurrent(replacement, initial),
            "A late removal revoked a newer arrival.");
    require(policy.Cancel(replacement), "Replacement cancellation failed.");
    policy.Invalidate(arrived.keyboard);
    rejected(policy.Reserve(Seat::First, first, arrived, initial, 1s), ReservationError::StaleEndpoint);
    ++arrived.keyboard.arrival_generation;
    auto latest = take(policy.Reserve(Seat::First, first, arrived, initial, 1s));
    require(policy.IsCurrent(latest, initial) && policy.IsCurrent(two, initial), "Explicit arrival recovery failed.");

    Policy out_of_order;
    auto retired_before_reserve = resources(30);
    retired_before_reserve.mouse.arrival_generation = 2;
    require(out_of_order.Invalidate(retired_before_reserve.mouse), "Removal before admission was ignored.");
    rejected(out_of_order.Reserve(Seat::First, first, retired_before_reserve, initial, 1s), ReservationError::StaleEndpoint);
    ++retired_before_reserve.mouse.arrival_generation;
    auto admitted = take(out_of_order.Reserve(Seat::First, first, retired_before_reserve, initial, 1s));
    auto unseen_later_removal = retired_before_reserve.mouse;
    ++unseen_later_removal.arrival_generation;
    require(out_of_order.Invalidate(unseen_later_removal) && !out_of_order.IsCurrent(admitted, initial),
            "A later removal left an older reservation eligible.");
    auto already_retired = retired_before_reserve;
    already_retired.mouse = unseen_later_removal;
    rejected(out_of_order.Reassociate(admitted, first, already_retired, initial, 1s), ReservationError::StaleEndpoint);
    ++already_retired.mouse.arrival_generation;
    auto recovered = take(out_of_order.Reassociate(admitted, first, already_retired, initial, 1s));
    require(!out_of_order.Invalidate(unseen_later_removal) && out_of_order.IsCurrent(recovered, initial),
            "An old removal poisoned the latest arrival watermark.");
}

class FailingResource final : public std::pmr::memory_resource {
public:
    bool fail{};
private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (fail) throw std::bad_alloc();
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* value, std::size_t bytes, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(value, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};

void allocation_tests() {
    FailingResource storage;
    Policy policy(&storage);
    auto one = take(policy.Reserve(Seat::First, first, resources(10), initial, 1s));
    auto two = take(policy.Reserve(Seat::Second, second, resources(20), initial, 1s));
    storage.fail = true;
    // Retirement bookkeeping must never fail open if the watermark allocation
    // fails. The API latches a fatal tracking fault and returns false.
    require(!policy.Invalidate({ResourceKind::Keyboard, 999, 1}), "Allocation failure was not reported.");
    require(!policy.IsCurrent(one, initial) && !policy.IsCurrent(two, initial) &&
            policy.View(Seat::First)->reason == Suspension::TrackingFailure &&
            policy.View(Seat::Second)->reason == Suspension::TrackingFailure,
            "Tracking failure left a reservation eligible.");
    storage.fail = false;
    rejected(policy.Reassociate(one, first, resources(10), initial, 1s), ReservationError::TrackingFailure);
    require(policy.Cancel(one) && policy.Cancel(two), "Fatal tracking failure prevented cleanup.");
    rejected(policy.Reserve(Seat::First, first, resources(10), initial, 1s), ReservationError::TrackingFailure);

    FailingResource admission_storage;
    Policy admission(&admission_storage);
    auto retained = take(admission.Reserve(Seat::First, first, resources(10), initial, 1s));
    admission_storage.fail = true;
    rejected(admission.Reserve(Seat::Second, second, resources(20), initial, 1s), ReservationError::TrackingFailure);
    require(!admission.IsCurrent(retained, initial) && admission.View(Seat::Second)->phase == Phase::Empty &&
            admission.Cancel(retained), "Failed admission left a new claim or unsafe eligibility.");
}

void lifetime_tests() {
    Policy policy;
    auto one = take(policy.Reserve(Seat::First, first, resources(10), initial, 5ms));
    auto two = take(policy.Reserve(Seat::Second, second, resources(20), initial, 10ms));
    require(policy.Poll(initial + 5ms) && !policy.IsCurrent(one, initial + 5ms) &&
            policy.IsCurrent(two, initial + 5ms), "Expiry did not isolate the expired reservation.");
    require(policy.View(Seat::First)->reason == Suspension::Expired, "Expiry reason was lost.");
    auto renewed = take(policy.Reassociate(one, first, resources(10), initial + 5ms, 5ms));
    require(!policy.Poll(initial + 4ms), "A regressing clock was accepted.");
    require(!policy.IsCurrent(renewed, initial + 6ms) && !policy.IsCurrent(two, initial + 6ms),
            "Clock recovery silently resumed suspended reservations.");
    require(policy.View(Seat::First)->reason == Suspension::ClockRegression &&
            policy.View(Seat::Second)->reason == Suspension::ClockRegression, "Clock regression did not suspend eligibility.");
}

void ownership_tests() {
    auto owner = std::make_unique<Policy>();
    auto one = take(owner->Reserve(Seat::First, first, resources(10), initial, 1s));
    Policy other;
    require(!other.IsCurrent(one, initial) && !other.Cancel(one) && !other.Suspend(one, Suspension::Locked),
            "A lease crossed controller ownership.");
    auto moved = std::move(one);
    require(!owner->Cancel(one) && owner->IsCurrent(moved, initial), "Moved-from lease retained ownership.");
    owner.reset();
    require(!other.IsCurrent(moved, initial) && !other.Cancel(moved), "Destroyed controller lease matched another owner.");
}

void interleaving_tests() {
    Policy policy;
    std::uint64_t previous_generation = 0;
    for (std::uint64_t cycle = 0; cycle < 32; ++cycle) {
        auto a_resources = resources(100 + cycle * 10);
        auto b_resources = resources(1000 + cycle * 10);
        auto one = take(policy.Reserve(Seat::First, first, a_resources, initial, 1s));
        auto two = take(policy.Reserve(Seat::Second, second, b_resources, initial, 1s));
        require(one.Generation() > previous_generation && two.Generation() > one.Generation(), "Reservation generation was reused.");
        previous_generation = two.Generation();
        const bool first_removed = cycle % 2 == 0;
        require(policy.Invalidate(first_removed ? a_resources.mouse : b_resources.keyboard), "Removal interleaving failed.");
        require(policy.IsCurrent(first_removed ? two : one, initial), "Peer eligibility was lost during interleaving.");
        require(policy.Cancel(one) && policy.Cancel(two) && !policy.Cancel(one) && !policy.Cancel(two),
                "Cancellation was not generation-safe and idempotent.");
        require(policy.View(Seat::First)->phase == Phase::Empty && policy.View(Seat::Second)->phase == Phase::Empty,
                "An interleaving left an orphan reservation.");
    }
}
} // namespace

int main() {
    try {
        static_assert(!std::is_copy_constructible_v<Reservation> && std::is_move_constructible_v<Reservation>);
        identity_tests();
        admission_tests(); incarnation_tests(); arrival_tests();
        lifetime_tests(); ownership_tests(); interleaving_tests(); allocation_tests();
        std::cout << "PASS: noninteractive native reservations, exclusive resources, stale generations, expiry and peer isolation.\n";
        std::cout << "No native terminal, display, input delivery, service or driver was created.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
