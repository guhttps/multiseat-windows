#include "identity.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace multiseat::native {
namespace {

constexpr DWORD kMaximumTokenInformationBytes = 1024U * 1024U;

class UniqueToken final {
public:
    explicit UniqueToken(HANDLE handle = nullptr) noexcept : handle_(handle) {}
    UniqueToken(const UniqueToken&) = delete;
    UniqueToken& operator=(const UniqueToken&) = delete;
    ~UniqueToken() {
        if (handle_ != nullptr) CloseHandle(handle_);
    }
    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }

private:
    HANDLE handle_;
};

[[nodiscard]] const char* FailureMessage(IdentityFailure reason) noexcept {
    switch (reason) {
    case IdentityFailure::InvalidToken: return "A valid token handle is required.";
    case IdentityFailure::TokenQueryFailed: return "Token inspection failed.";
    case IdentityFailure::TokenDuplicationFailed: return "Private token snapshot failed.";
    case IdentityFailure::NotPrimary: return "An actual primary token is required.";
    case IdentityFailure::SessionZero: return "Session zero is not an interactive seat identity.";
    case IdentityFailure::InvalidUserSid: return "The token user SID is invalid.";
    case IdentityFailure::InvalidGroupSid: return "A token group SID is invalid.";
    case IdentityFailure::MissingInteractiveGroup: return "An enabled interactive logon group is required.";
    case IdentityFailure::ServiceIdentity: return "System and service identities are not seat users.";
    case IdentityFailure::InvalidLogonSid: return "Exactly one enabled logon SID is required.";
    case IdentityFailure::InvalidAuthenticationId: return "A logon authentication identifier is required.";
    }
    return "Identity verification failed.";
}

template <typename T>
[[nodiscard]] T QueryFixed(HANDLE token, TOKEN_INFORMATION_CLASS information) {
    T value{};
    DWORD returned = 0;
    if (!GetTokenInformation(token, information, &value, sizeof(value), &returned)) {
        throw IdentityError(IdentityFailure::TokenQueryFailed, GetLastError());
    }
    if (returned != sizeof(value)) {
        throw IdentityError(IdentityFailure::TokenQueryFailed, ERROR_INVALID_DATA);
    }
    return value;
}

[[nodiscard]] std::vector<unsigned char> QueryBuffer(
    HANDLE token, TOKEN_INFORMATION_CLASS information, std::size_t minimum_bytes) {
    DWORD required = 0;
    const BOOL initial = GetTokenInformation(token, information, nullptr, 0, &required);
    const DWORD query_error = GetLastError();
    if (initial || query_error != ERROR_INSUFFICIENT_BUFFER ||
        required < minimum_bytes || required > kMaximumTokenInformationBytes) {
        throw IdentityError(IdentityFailure::TokenQueryFailed,
            initial || query_error == ERROR_INSUFFICIENT_BUFFER ? ERROR_INVALID_DATA : query_error);
    }
    std::vector<unsigned char> buffer(required);
    DWORD returned = 0;
    if (!GetTokenInformation(token, information, buffer.data(), required, &returned)) {
        throw IdentityError(IdentityFailure::TokenQueryFailed, GetLastError());
    }
    if (returned < minimum_bytes || returned > required) {
        throw IdentityError(IdentityFailure::TokenQueryFailed, ERROR_INVALID_DATA);
    }
    buffer.resize(returned);
    return buffer;
}

// The Windows-produced pointer must lie wholly inside its owning query buffer
// before any SID helper is allowed to inspect it.
[[nodiscard]] std::vector<unsigned char> CopyBoundedSid(
    PSID sid, const std::vector<unsigned char>& buffer, IdentityFailure failure) {
    const auto begin = reinterpret_cast<std::uintptr_t>(buffer.data());
    const auto address = reinterpret_cast<std::uintptr_t>(sid);
    constexpr std::size_t header_bytes = offsetof(SID, SubAuthority);
    if (sid == nullptr || address < begin || address - begin > buffer.size() ||
        buffer.size() - (address - begin) < header_bytes) {
        throw IdentityError(failure, ERROR_INVALID_SID);
    }
    const auto offset = static_cast<std::size_t>(address - begin);
    const BYTE subauthority_count = buffer[offset + offsetof(SID, SubAuthorityCount)];
    if (subauthority_count > SID_MAX_SUB_AUTHORITIES) {
        throw IdentityError(failure, ERROR_INVALID_SID);
    }
    const DWORD sid_bytes = GetSidLengthRequired(subauthority_count);
    if (sid_bytes > buffer.size() - offset || !IsValidSid(sid) ||
        GetLengthSid(sid) != sid_bytes) {
        throw IdentityError(failure, ERROR_INVALID_SID);
    }
    std::vector<unsigned char> copy(sid_bytes);
    if (!CopySid(sid_bytes, copy.data(), sid)) {
        throw IdentityError(failure, GetLastError());
    }
    return copy;
}

[[nodiscard]] bool IsServiceUser(PSID sid) noexcept {
    if (IsWellKnownSid(sid, WinLocalSystemSid) ||
        IsWellKnownSid(sid, WinLocalServiceSid) ||
        IsWellKnownSid(sid, WinNetworkServiceSid)) {
        return true;
    }
    const SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    return *GetSidSubAuthorityCount(sid) > 0 &&
        std::memcmp(GetSidIdentifierAuthority(sid), &nt_authority, sizeof(nt_authority)) == 0 &&
        *GetSidSubAuthority(sid, 0) == SECURITY_SERVICE_ID_BASE_RID;
}

[[nodiscard]] bool EnabledForAllow(DWORD attributes) noexcept {
    return (attributes & SE_GROUP_ENABLED) != 0 &&
        (attributes & SE_GROUP_USE_FOR_DENY_ONLY) == 0;
}

} // namespace

struct VerifiedIdentity::Snapshot final {
    std::vector<unsigned char> user_sid;
    std::vector<unsigned char> logon_sid;
    LUID authentication_id{};
    DWORD session_id = 0;
};

IdentityError::IdentityError(IdentityFailure reason, DWORD win32_error)
    : std::runtime_error(FailureMessage(reason)), reason_(reason), win32_error_(win32_error) {}

IdentityFailure IdentityError::Reason() const noexcept { return reason_; }
DWORD IdentityError::Win32Error() const noexcept { return win32_error_; }

VerifiedIdentity::VerifiedIdentity(std::shared_ptr<const Snapshot> snapshot) noexcept
    : snapshot_(std::move(snapshot)) {}

VerifiedIdentity::~VerifiedIdentity() = default;

VerifiedIdentity VerifiedIdentity::CurrentProcess() {
    HANDLE opened = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &opened)) {
        throw IdentityError(IdentityFailure::TokenQueryFailed, GetLastError());
    }
    const UniqueToken token(opened);
    return FromPrimaryToken(token.Get());
}

VerifiedIdentity VerifiedIdentity::FromPrimaryToken(HANDLE token) {
    if (token == nullptr || token == INVALID_HANDLE_VALUE) {
        throw IdentityError(IdentityFailure::InvalidToken, ERROR_INVALID_HANDLE);
    }
    // Do not convert an impersonation token into an apparently primary identity.
    if (QueryFixed<TOKEN_TYPE>(token, TokenType) != TokenPrimary) {
        throw IdentityError(IdentityFailure::NotPrimary, ERROR_BAD_TOKEN_TYPE);
    }
    HANDLE duplicated = nullptr;
    if (!DuplicateTokenEx(token, TOKEN_QUERY, nullptr, SecurityIdentification,
        TokenPrimary, &duplicated)) {
        throw IdentityError(IdentityFailure::TokenDuplicationFailed, GetLastError());
    }
    const UniqueToken snapshot_token(duplicated);
    const HANDLE stable_token = snapshot_token.Get();
    const auto statistics = QueryFixed<TOKEN_STATISTICS>(stable_token, TokenStatistics);
    if (statistics.TokenType != TokenPrimary) {
        throw IdentityError(IdentityFailure::NotPrimary, ERROR_BAD_TOKEN_TYPE);
    }
    const DWORD session_id = QueryFixed<DWORD>(stable_token, TokenSessionId);
    if (session_id == 0) {
        throw IdentityError(IdentityFailure::SessionZero);
    }
    if (statistics.AuthenticationId.LowPart == 0 && statistics.AuthenticationId.HighPart == 0) {
        throw IdentityError(IdentityFailure::InvalidAuthenticationId);
    }

    const auto user_buffer = QueryBuffer(stable_token, TokenUser, sizeof(TOKEN_USER));
    TOKEN_USER user{};
    std::memcpy(&user, user_buffer.data(), sizeof(user));
    auto user_sid = CopyBoundedSid(user.User.Sid, user_buffer, IdentityFailure::InvalidUserSid);
    if (IsServiceUser(user_sid.data())) {
        throw IdentityError(IdentityFailure::ServiceIdentity);
    }

    constexpr std::size_t groups_offset = offsetof(TOKEN_GROUPS, Groups);
    const auto groups_buffer = QueryBuffer(stable_token, TokenGroups, groups_offset);
    DWORD group_count = 0;
    std::memcpy(&group_count, groups_buffer.data(), sizeof(group_count));
    if (group_count > (groups_buffer.size() - groups_offset) / sizeof(SID_AND_ATTRIBUTES)) {
        throw IdentityError(IdentityFailure::TokenQueryFailed, ERROR_INVALID_DATA);
    }
    bool interactive = false;
    DWORD logon_count = 0;
    std::vector<unsigned char> logon_sid;
    for (DWORD index = 0; index < group_count; ++index) {
        SID_AND_ATTRIBUTES group{};
        const std::size_t offset = groups_offset + static_cast<std::size_t>(index) * sizeof(group);
        std::memcpy(&group, groups_buffer.data() + offset, sizeof(group));
        auto group_sid = CopyBoundedSid(group.Sid, groups_buffer, IdentityFailure::InvalidGroupSid);
        if (IsWellKnownSid(group_sid.data(), WinServiceSid)) {
            throw IdentityError(IdentityFailure::ServiceIdentity);
        }
        if (IsWellKnownSid(group_sid.data(), WinInteractiveSid) && EnabledForAllow(group.Attributes)) {
            interactive = true;
        }
        if ((group.Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID) {
            ++logon_count;
            if (logon_count != 1 || !EnabledForAllow(group.Attributes) ||
                !IsWellKnownSid(group_sid.data(), WinLogonIdsSid)) {
                throw IdentityError(IdentityFailure::InvalidLogonSid);
            }
            logon_sid = std::move(group_sid);
        }
    }
    if (!interactive) {
        throw IdentityError(IdentityFailure::MissingInteractiveGroup);
    }
    if (logon_count != 1) {
        throw IdentityError(IdentityFailure::InvalidLogonSid);
    }

    // These copied principal facts remain immutable when the source token is
    // subsequently adjusted. They do not prove that its session is still alive.
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->user_sid = std::move(user_sid);
    snapshot->logon_sid = std::move(logon_sid);
    snapshot->authentication_id = statistics.AuthenticationId;
    snapshot->session_id = session_id;
    return VerifiedIdentity(std::move(snapshot));
}

DWORD VerifiedIdentity::SessionId() const noexcept { return snapshot_->session_id; }

bool VerifiedIdentity::SameUser(const VerifiedIdentity& other) const noexcept {
    return snapshot_->user_sid == other.snapshot_->user_sid;
}

bool VerifiedIdentity::SameLogon(const VerifiedIdentity& other) const noexcept {
    return SameUser(other) && snapshot_->session_id == other.snapshot_->session_id &&
        snapshot_->authentication_id.LowPart == other.snapshot_->authentication_id.LowPart &&
        snapshot_->authentication_id.HighPart == other.snapshot_->authentication_id.HighPart &&
        snapshot_->logon_sid == other.snapshot_->logon_sid;
}

} // namespace multiseat::native
