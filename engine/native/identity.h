#pragma once

#include <Windows.h>

#include <memory>
#include <stdexcept>

namespace multiseat::native {

enum class IdentityFailure {
    InvalidToken,
    TokenQueryFailed,
    TokenDuplicationFailed,
    NotPrimary,
    SessionZero,
    InvalidUserSid,
    InvalidGroupSid,
    MissingInteractiveGroup,
    ServiceIdentity,
    InvalidLogonSid,
    InvalidAuthenticationId,
};

// Messages are fixed and contain no account, SID, token or machine details.
class IdentityError final : public std::runtime_error {
public:
    IdentityError(IdentityFailure reason, DWORD win32_error = ERROR_SUCCESS);

    [[nodiscard]] IdentityFailure Reason() const noexcept;
    [[nodiscard]] DWORD Win32Error() const noexcept;

private:
    IdentityFailure reason_;
    DWORD win32_error_;
};

// An in-process snapshot of an actual interactive primary token's principal.
// This is neither an authentication protocol nor proof that a terminal exists.
// No SID, logon identifier or token handle can be exported through this API.
class VerifiedIdentity final {
public:
    // Reads the process primary token even when the calling thread impersonates.
    [[nodiscard]] static VerifiedIdentity CurrentProcess();

    // Requires TOKEN_QUERY | TOKEN_DUPLICATE. The caller keeps ownership and must
    // keep this handle valid for the call. A private, noninheritable TOKEN_QUERY
    // duplicate is captured before inspecting the principal facts. No source
    // token, privilege, group, session, account or process is changed.
    [[nodiscard]] static VerifiedIdentity FromPrimaryToken(HANDLE token);

    VerifiedIdentity(const VerifiedIdentity&) noexcept = default;
    VerifiedIdentity& operator=(const VerifiedIdentity&) noexcept = default;
    ~VerifiedIdentity();

    [[nodiscard]] DWORD SessionId() const noexcept;
    [[nodiscard]] bool SameUser(const VerifiedIdentity& other) const noexcept;
    // Includes user SID, enabled logon SID, AuthenticationId and session ID.
    [[nodiscard]] bool SameLogon(const VerifiedIdentity& other) const noexcept;

private:
    struct Snapshot;
    explicit VerifiedIdentity(std::shared_ptr<const Snapshot> snapshot) noexcept;

    // Copy semantics deliberately also apply to rvalues: a moved-from identity
    // must never become an empty, apparently verified value.
    std::shared_ptr<const Snapshot> snapshot_;
};

// A snapshot does not establish current session connectivity, desktop safety,
// terminal ownership, process authorization or future token/session lifetime.
// Those facts require separate, fresh platform checks before any operation.

} // namespace multiseat::native
