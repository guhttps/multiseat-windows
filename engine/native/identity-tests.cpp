#include "identity.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using multiseat::native::IdentityError;
using multiseat::native::IdentityFailure;
using multiseat::native::VerifiedIdentity;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class TestToken final {
public:
    explicit TestToken(HANDLE token) noexcept : token_(token) {}
    TestToken(const TestToken&) = delete;
    TestToken& operator=(const TestToken&) = delete;
    ~TestToken() { CloseHandle(token_); }
    [[nodiscard]] HANDLE Get() const noexcept { return token_; }

private:
    HANDLE token_;
};

template <typename Action>
void ExpectFailure(Action action, IdentityFailure expected, const char* message) {
    try {
        action();
    } catch (const IdentityError& error) {
        Require(error.Reason() == expected, message);
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] TestToken OpenOwnPrimary(DWORD rights) {
    HANDLE token = nullptr;
    Require(OpenProcessToken(GetCurrentProcess(), rights, &token) != FALSE,
        "identity test: opening the own primary token failed.");
    return TestToken(token);
}

[[nodiscard]] TestToken CloneOwnToken(HANDLE source, TOKEN_TYPE type) {
    HANDLE token = nullptr;
    Require(DuplicateTokenEx(source, TOKEN_QUERY | TOKEN_DUPLICATE, nullptr,
        SecurityIdentification, type, &token) != FALSE,
        "identity test: duplicating the own token failed.");
    return TestToken(token);
}

[[nodiscard]] std::vector<unsigned char> ObserveTokenBuffer(
    HANDLE token, TOKEN_INFORMATION_CLASS information, std::size_t minimum) {
    DWORD required = 0;
    const BOOL first = GetTokenInformation(token, information, nullptr, 0, &required);
    const DWORD error = GetLastError();
    Require(!first && error == ERROR_INSUFFICIENT_BUFFER && required >= minimum &&
        required <= 1024U * 1024U,
        "identity test: independent token query sizing failed.");
    std::vector<unsigned char> data(required);
    DWORD returned = 0;
    Require(GetTokenInformation(token, information, data.data(), required, &returned) != FALSE &&
        returned >= minimum && returned <= required,
        "identity test: independent token query failed.");
    data.resize(returned);
    return data;
}

struct ObservedPrincipal final {
    DWORD session_id = 0;
    bool service = false;
    bool interactive = false;
};

// This observes the actual kernel token independently of VerifiedIdentity. A
// hosted CI runner may legitimately have no interactive seat identity; only
// the exact rejection justified by these observed facts is accepted below.
[[nodiscard]] ObservedPrincipal ObservePrincipal(HANDLE token) {
    ObservedPrincipal observed;
    DWORD returned = 0;
    TOKEN_TYPE type{};
    Require(GetTokenInformation(token, TokenType, &type, sizeof(type), &returned) != FALSE &&
        returned == sizeof(type) && type == TokenPrimary,
        "identity test: the own process did not provide a primary token.");
    Require(GetTokenInformation(token, TokenSessionId, &observed.session_id,
        sizeof(observed.session_id), &returned) != FALSE && returned == sizeof(observed.session_id),
        "identity test: independent session observation failed.");

    const auto user_data = ObserveTokenBuffer(token, TokenUser, sizeof(TOKEN_USER));
    TOKEN_USER user{};
    std::memcpy(&user, user_data.data(), sizeof(user));
    Require(user.User.Sid != nullptr && IsValidSid(user.User.Sid) != FALSE,
        "identity test: independent user SID observation failed.");
    const SID_IDENTIFIER_AUTHORITY service_authority = SECURITY_NT_AUTHORITY;
    observed.service = IsWellKnownSid(user.User.Sid, WinLocalSystemSid) != FALSE ||
        IsWellKnownSid(user.User.Sid, WinLocalServiceSid) != FALSE ||
        IsWellKnownSid(user.User.Sid, WinNetworkServiceSid) != FALSE ||
        (*GetSidSubAuthorityCount(user.User.Sid) > 0 &&
            std::memcmp(GetSidIdentifierAuthority(user.User.Sid), &service_authority,
                sizeof(service_authority)) == 0 &&
            *GetSidSubAuthority(user.User.Sid, 0) == SECURITY_SERVICE_ID_BASE_RID);

    constexpr std::size_t offset = offsetof(TOKEN_GROUPS, Groups);
    const auto groups_data = ObserveTokenBuffer(token, TokenGroups, offset);
    DWORD count = 0;
    std::memcpy(&count, groups_data.data(), sizeof(count));
    Require(count <= (groups_data.size() - offset) / sizeof(SID_AND_ATTRIBUTES),
        "identity test: independent group observation was malformed.");
    for (DWORD index = 0; index < count; ++index) {
        SID_AND_ATTRIBUTES group{};
        std::memcpy(&group, groups_data.data() + offset +
            static_cast<std::size_t>(index) * sizeof(group), sizeof(group));
        Require(group.Sid != nullptr && IsValidSid(group.Sid) != FALSE,
            "identity test: independent group SID observation failed.");
        if (IsWellKnownSid(group.Sid, WinServiceSid)) observed.service = true;
        if (IsWellKnownSid(group.Sid, WinInteractiveSid) &&
            (group.Attributes & SE_GROUP_ENABLED) != 0 &&
            (group.Attributes & SE_GROUP_USE_FOR_DENY_ONLY) == 0) {
            observed.interactive = true;
        }
    }
    return observed;
}

void CheckNonSeat(HANDLE source, const ObservedPrincipal& observed) {
    IdentityFailure expected = IdentityFailure::MissingInteractiveGroup;
    if (observed.session_id == 0) {
        expected = IdentityFailure::SessionZero;
    } else if (observed.service) {
        expected = IdentityFailure::ServiceIdentity;
    } else {
        Require(!observed.interactive,
            "identity test: non-seat rejection had no observed justification.");
    }
    ExpectFailure([&] { (void)VerifiedIdentity::FromPrimaryToken(source); }, expected,
        "identity test: own non-seat primary token did not receive its exact rejection.");
    ExpectFailure([] { (void)VerifiedIdentity::CurrentProcess(); }, expected,
        "identity test: process identity did not receive its exact non-seat rejection.");
    std::puts("PASS: native identity rejects the independently observed non-seat token.");
}

void CheckInteractive(HANDLE source) {
    const auto first = VerifiedIdentity::FromPrimaryToken(source);
    const auto current = VerifiedIdentity::CurrentProcess();
    Require(first.SessionId() != 0 && first.SessionId() == current.SessionId() &&
        first.SameUser(current) && first.SameLogon(current),
        "identity test: independently captured own principal facts did not match.");

    const auto retained = [&] {
        const auto clone = CloneOwnToken(source, TokenPrimary);
        return VerifiedIdentity::FromPrimaryToken(clone.Get());
    }();
    Require(retained.SameUser(first) && retained.SameLogon(first) &&
        retained.SessionId() == first.SessionId(),
        "identity test: snapshot did not survive closing its source clone handle.");

    auto copied = retained;
    auto from_rvalue = std::move(copied);
    Require(from_rvalue.SameLogon(first) && copied.SameLogon(first),
        "identity test: rvalue copy invalidated a verified principal value.");
    copied = current;
    Require(copied.SameLogon(first),
        "identity test: principal copy assignment changed logon identity.");

    alignas(DWORD) BYTE interactive_sid[SECURITY_MAX_SID_SIZE]{};
    DWORD sid_bytes = sizeof(interactive_sid);
    Require(CreateWellKnownSid(WinInteractiveSid, nullptr, interactive_sid, &sid_bytes) != FALSE,
        "identity test: creating the standard interactive test SID failed.");
    SID_AND_ATTRIBUTES disabled{interactive_sid, 0};
    HANDLE restricted_handle = nullptr;
    Require(CreateRestrictedToken(source, 0, 1, &disabled, 0, nullptr, 0, nullptr,
        &restricted_handle) != FALSE,
        "identity test: creating a private restricted token failed.");
    const TestToken restricted(restricted_handle);
    const auto restricted_observation = ObservePrincipal(restricted.Get());
    Require(restricted_observation.session_id == first.SessionId() &&
        !restricted_observation.service && !restricted_observation.interactive,
        "identity test: the restricted copy retained interactive allow membership.");
    ExpectFailure([&] { (void)VerifiedIdentity::FromPrimaryToken(restricted.Get()); },
        IdentityFailure::MissingInteractiveGroup,
        "identity test: deny-only interactive membership was accepted.");

    // The private restricted token must not have changed the caller's source.
    const auto after = VerifiedIdentity::FromPrimaryToken(source);
    Require(after.SameUser(first) && after.SameLogon(first),
        "identity test: private token tests affected the source principal.");
    std::puts("PASS: native identity captures an actual interactive primary principal and immutable copies.");
}

} // namespace

void identity_tests() {
    ExpectFailure([] { (void)VerifiedIdentity::FromPrimaryToken(nullptr); },
        IdentityFailure::InvalidToken, "identity test: a null token handle was accepted.");
    ExpectFailure([] { (void)VerifiedIdentity::FromPrimaryToken(INVALID_HANDLE_VALUE); },
        IdentityFailure::InvalidToken, "identity test: an invalid token handle was accepted.");

    const auto source = OpenOwnPrimary(TOKEN_QUERY | TOKEN_DUPLICATE);
    const auto impersonation = CloneOwnToken(source.Get(), TokenImpersonation);
    ExpectFailure([&] { (void)VerifiedIdentity::FromPrimaryToken(impersonation.Get()); },
        IdentityFailure::NotPrimary,
        "identity test: an actual impersonation token was accepted as a primary principal.");

    const auto query_only = OpenOwnPrimary(TOKEN_QUERY);
    ExpectFailure([&] { (void)VerifiedIdentity::FromPrimaryToken(query_only.Get()); },
        IdentityFailure::TokenDuplicationFailed,
        "identity test: a handle without token duplication rights was accepted.");
    std::puts("PASS: native identity rejects invalid handles, impersonation and missing duplication rights.");

    const auto observed = ObservePrincipal(source.Get());
    if (observed.session_id == 0 || observed.service || !observed.interactive) {
        CheckNonSeat(source.Get(), observed);
    } else {
        CheckInteractive(source.Get());
    }
}
