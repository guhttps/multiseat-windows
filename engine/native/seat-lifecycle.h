#pragma once

#include "identity.h"

#include <array>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <type_traits>
#include <utility>

namespace multiseat::native {

enum class Seat : std::uint8_t { First, Second };
enum class ResourceKind : std::uint8_t { Monitor, Keyboard, Mouse };
enum class Phase : std::uint8_t { Empty, Reserved, Suspended };
enum class Suspension : std::uint8_t {
    None, Locked, SecureDesktop, Disconnected, EndpointChanged, Expired,
    ClockRegression, TrackingFailure
};
enum class ReservationError : std::uint8_t {
    None, InvalidSeat, InvalidResources, InvalidIdentity, IdentityConflict,
    ResourceConflict, StaleEndpoint, Occupied, StaleReservation,
    ReassociationRequired, InvalidLifetime, ClockRegression, GenerationExhausted,
    TrackingFailure
};

// Opaque, process-local backend keys: never device paths or IPC authority.
// A copied key reserves policy bookkeeping only, never a physical routing path.
struct ResourceKey final {
    ResourceKind kind{};
    std::uint64_t id{};
    std::uint64_t arrival_generation{};
    bool operator==(const ResourceKey&) const = default;
};
struct SeatResources final {
    ResourceKey monitor;
    ResourceKey keyboard;
    ResourceKey mouse;
    std::array<ResourceKey, 3> Keys() const noexcept;
};
bool ValidResources(const SeatResources& resources) noexcept;
bool SameEndpoint(const ResourceKey& left, const ResourceKey& right) noexcept;

template<class Identity>
concept SeatIdentity = std::copyable<Identity> &&
    std::is_nothrow_copy_constructible_v<Identity> &&
    std::is_nothrow_copy_assignable_v<Identity> &&
    std::is_nothrow_move_constructible_v<Identity> &&
    std::is_nothrow_move_assignable_v<Identity> &&
    requires(const Identity& left, const Identity& right) {
        { left.SessionId() } -> std::convertible_to<std::uint32_t>;
        { left.SameUser(right) } -> std::same_as<bool>;
        { left.SameLogon(right) } -> std::same_as<bool>;
    };

template<SeatIdentity Identity> class BasicSeatLifecycle;

// A reservation is noninteractive. Its validity NEVER authorizes input delivery,
// login, terminal attachment or presentation. Destruction has no callback;
// Cancel, expiry or controller destruction retires its eligibility explicitly.
class Reservation final {
public:
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;
    Reservation(Reservation&&) noexcept = default;
    Reservation& operator=(Reservation&&) noexcept = default;
    Seat SeatId() const noexcept { return seat_; }
    std::uint64_t Generation() const noexcept { return generation_; }
private:
    template<SeatIdentity Identity> friend class BasicSeatLifecycle;
    struct Owner {};
    Reservation(const std::shared_ptr<const Owner>& owner, Seat seat,
                std::uint64_t generation) noexcept
        : owner_(owner), seat_(seat), generation_(generation) {}
    std::weak_ptr<const Owner> owner_;
    Seat seat_;
    std::uint64_t generation_;
};

struct ReservationResult final {
    ReservationError error;
    std::optional<Reservation> reservation;
};
struct SeatView final {
    Phase phase;
    Suspension reason;
    std::uint64_t generation;
};

// The template permits inert policy-test principals. Production uses the alias
// below, which requires immutable identities observed from real primary tokens.
// All reports and clocks are trusted in-process facts, never authenticated IPC.
// Calls must be serialized by the owning controller. It must Poll on its timer;
// View is a snapshot and does not itself advance the monotonic clock.
// A supplied memory_resource must outlive this controller. A tracking allocation
// failure preserves claims but suspends ALL eligibility, including the peer's,
// and permanently closes admission. Cancel remains available; a fresh controller
// needs fresh platform observations. Cancelling claims cannot clear this fault.
template<SeatIdentity Identity>
class BasicSeatLifecycle final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;
    static constexpr Duration MaximumLifetime =
        std::chrono::duration_cast<Duration>(std::chrono::minutes{5});

    explicit BasicSeatLifecycle(std::pmr::memory_resource* resource =
                                   std::pmr::get_default_resource())
        : owner_(std::make_shared<const Reservation::Owner>()),
          arrivals_(resource ? resource : std::pmr::get_default_resource()) {}
    BasicSeatLifecycle(const BasicSeatLifecycle&) = delete;
    BasicSeatLifecycle& operator=(const BasicSeatLifecycle&) = delete;
    BasicSeatLifecycle(BasicSeatLifecycle&&) = delete;
    BasicSeatLifecycle& operator=(BasicSeatLifecycle&&) = delete;

    ReservationResult Reserve(Seat seat, const Identity& identity,
                              const SeatResources& resources, TimePoint now,
                              Duration lifetime) {
        if (tracking_failed_) return Failure(ReservationError::TrackingFailure);
        if (!ValidSeat(seat)) return Failure(ReservationError::InvalidSeat);
        if (!Poll(now)) return Failure(ReservationError::ClockRegression);
        const auto& record = records_[Index(seat)];
        if (record.phase != Phase::Empty)
            return Failure(record.phase == Phase::Suspended
                ? ReservationError::ReassociationRequired
                : ReservationError::Occupied);
        return Commit(seat, identity, resources, now, lifetime);
    }

    ReservationResult Reassociate(const Reservation& previous,
                                  const Identity& identity,
                                  const SeatResources& resources, TimePoint now,
                                  Duration lifetime) {
        if (tracking_failed_) return Failure(ReservationError::TrackingFailure);
        if (!Poll(now)) return Failure(ReservationError::ClockRegression);
        if (!Matches(previous)) return Failure(ReservationError::StaleReservation);
        if (records_[Index(previous.seat_)].phase != Phase::Suspended)
            return Failure(ReservationError::Occupied);
        if (!records_[Index(previous.seat_)].identity->SameLogon(identity))
            return Failure(ReservationError::IdentityConflict);
        return Commit(previous.seat_, identity, resources, now, lifetime);
    }

    bool IsCurrent(const Reservation& reservation, TimePoint now) {
        return Poll(now) && Matches(reservation) &&
            records_[Index(reservation.seat_)].phase == Phase::Reserved;
    }

    bool Suspend(const Reservation& expected, Suspension reason) noexcept {
        if (!ValidSuspension(reason) || !Matches(expected)) return false;
        auto& record = records_[Index(expected.seat_)];
        if (record.phase != Phase::Reserved) return false;
        record.phase = Phase::Suspended;
        record.reason = reason;
        return true;
    }

    bool Cancel(const Reservation& expected) noexcept {
        if (!Matches(expected)) return false;
        records_[Index(expected.seat_)] = Record{};
        return true;
    }

    // Remember removals even before reservation. A removal retires claims at or
    // before its generation, never a newer arrival. Claims remain exclusive.
    // Allocation failure returns false and latches TrackingFailure; it cannot
    // throw or leave another reservation eligible after losing tracking state.
    bool Invalidate(const ResourceKey& removed) noexcept {
        if (tracking_failed_ || !ValidKey(removed)) return false;
        bool observed = false;
        try {
            const auto [known, inserted] = arrivals_.try_emplace(
                Endpoint{removed.kind, removed.id},
                Arrival{removed.arrival_generation, true});
            if (inserted || removed.arrival_generation >= known->second.generation) {
                known->second = {removed.arrival_generation, true};
                observed = true;
            }
        } catch (...) {
            FailTracking();
            return false;
        }
        for (auto& record : records_) {
            if (record.phase == Phase::Empty) continue;
            for (const auto& key : record.resources.Keys()) {
                if (!SameEndpoint(key, removed) ||
                    key.arrival_generation > removed.arrival_generation) continue;
                record.phase = Phase::Suspended;
                record.reason = Suspension::EndpointChanged;
                observed = true;
            }
        }
        return observed;
    }

    bool Poll(TimePoint now) noexcept {
        if (tracking_failed_) return false;
        const bool regressed = last_time_ && now < *last_time_;
        if (!regressed) last_time_ = now;
        for (auto& record : records_) {
            if (record.phase != Phase::Reserved) continue;
            if (regressed || now >= record.expires) {
                record.phase = Phase::Suspended;
                record.reason = regressed ? Suspension::ClockRegression
                                          : Suspension::Expired;
            }
        }
        return !regressed;
    }

    std::optional<SeatView> View(Seat seat) const noexcept {
        if (!ValidSeat(seat)) return std::nullopt;
        const auto& record = records_[Index(seat)];
        return SeatView{record.phase, record.reason, record.generation};
    }

private:
    struct Record final {
        Phase phase{Phase::Empty};
        Suspension reason{Suspension::None};
        std::optional<Identity> identity;
        SeatResources resources{};
        std::uint64_t generation{};
        TimePoint expires{};
    };
    struct Arrival final { std::uint64_t generation; bool removed; };
    using Endpoint = std::pair<ResourceKind, std::uint64_t>;

    static bool ValidSeat(Seat seat) noexcept {
        return seat == Seat::First || seat == Seat::Second;
    }
    static bool ValidKey(const ResourceKey& key) noexcept {
        return key.id != 0 && key.arrival_generation != 0 &&
            (key.kind == ResourceKind::Monitor ||
             key.kind == ResourceKind::Keyboard || key.kind == ResourceKind::Mouse);
    }
    static bool ValidSuspension(Suspension reason) noexcept {
        return reason == Suspension::Locked ||
            reason == Suspension::SecureDesktop ||
            reason == Suspension::Disconnected ||
            reason == Suspension::EndpointChanged ||
            reason == Suspension::Expired ||
            reason == Suspension::ClockRegression;
    }
    static std::size_t Index(Seat seat) noexcept {
        return static_cast<std::size_t>(seat);
    }
    static ReservationResult Failure(ReservationError error) {
        return {error, std::nullopt};
    }
    void FailTracking() noexcept {
        tracking_failed_ = true;
        for (auto& record : records_) {
            if (record.phase == Phase::Empty) continue;
            record.phase = Phase::Suspended;
            record.reason = Suspension::TrackingFailure;
        }
    }
    bool Matches(const Reservation& reservation) const noexcept {
        if (!ValidSeat(reservation.seat_) || reservation.owner_.lock() != owner_)
            return false;
        const auto& record = records_[Index(reservation.seat_)];
        return record.phase != Phase::Empty &&
            record.generation == reservation.generation_;
    }

    ReservationResult Commit(Seat seat, const Identity& identity,
                             const SeatResources& resources, TimePoint now,
                             Duration lifetime) {
        if (lifetime <= Duration::zero() || lifetime > MaximumLifetime ||
            now > (TimePoint::max)() - lifetime)
            return Failure(ReservationError::InvalidLifetime);
        if (!ValidResources(resources))
            return Failure(ReservationError::InvalidResources);
        if (identity.SessionId() == 0 ||
            identity.SessionId() == (std::numeric_limits<std::uint32_t>::max)())
            return Failure(ReservationError::InvalidIdentity);
        const auto& peer = records_[1U - Index(seat)];
        if (peer.phase != Phase::Empty) {
            if (peer.identity->SessionId() == identity.SessionId() ||
                peer.identity->SameUser(identity) || peer.identity->SameLogon(identity))
                return Failure(ReservationError::IdentityConflict);
            for (const auto& key : resources.Keys())
                for (const auto& peer_key : peer.resources.Keys())
                    if (SameEndpoint(key, peer_key))
                        return Failure(ReservationError::ResourceConflict);
        }
        for (const auto& key : resources.Keys()) {
            const auto known = arrivals_.find({key.kind, key.id});
            if (known != arrivals_.end() &&
                (key.arrival_generation < known->second.generation ||
                 (key.arrival_generation == known->second.generation &&
                  known->second.removed)))
                return Failure(ReservationError::StaleEndpoint);
        }
        if (generation_ == (std::numeric_limits<std::uint64_t>::max)())
            return Failure(ReservationError::GenerationExhausted);
        Record replacement{Phase::Reserved, Suspension::None, identity,
                           resources, generation_ + 1U, now + lifetime};
        // Tracking must succeed before replacing a claim. A failure retires all
        // eligibility permanently rather than forgetting a removal watermark.
        try {
            for (const auto& key : resources.Keys())
                arrivals_.try_emplace({key.kind, key.id}, Arrival{0, false});
        } catch (...) {
            FailTracking();
            return Failure(ReservationError::TrackingFailure);
        }
        for (const auto& key : resources.Keys())
            arrivals_.at({key.kind, key.id}) = {key.arrival_generation, false};
        records_[Index(seat)] = std::move(replacement);
        ++generation_;
        return {ReservationError::None, Reservation{owner_, seat, generation_}};
    }

    std::shared_ptr<const Reservation::Owner> owner_;
    std::array<Record, 2> records_{};
    std::pmr::map<Endpoint, Arrival> arrivals_;
    std::optional<TimePoint> last_time_;
    std::uint64_t generation_{};
    bool tracking_failed_{};
};

using SeatLifecycle = BasicSeatLifecycle<VerifiedIdentity>;
extern template class BasicSeatLifecycle<VerifiedIdentity>;

} // namespace multiseat::native
