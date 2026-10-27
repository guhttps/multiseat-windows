#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "identity.h"

namespace multiseat::native::ipc {

enum class PipeFailure {
    NameInvalid,
    LimitsInvalid,
    InvalidArgument,
    SecurityDescriptorFailed,
    CreateFailed,
    NotVerified,
    ThreadAlreadyImpersonating,
    ImpersonationRevertFailed,
    ConnectTimedOut,
    ConnectFailed,
    PeerQueryFailed,
    PeerProcessUnavailable,
    PeerTokenUnavailable,
    ImpersonationFailed,
    PeerBindingMismatch,
    PeerPolicyMismatch,
    MessageTooLarge,
    IoTimedOut,
    IoFailed,
    Disconnected,
    ClientConnectFailed,
    Cancelled,
};

// Fixed messages. A PipeError never embeds a pipe name, SID, path, token value
// or any payload byte; only a coarse reason and an optional Win32 code.
class PipeError final : public std::runtime_error {
public:
    explicit PipeError(PipeFailure reason, DWORD win32_error = ERROR_SUCCESS);
    [[nodiscard]] PipeFailure Reason() const noexcept;
    [[nodiscard]] DWORD Win32Error() const noexcept;

private:
    PipeFailure reason_;
    DWORD win32_error_;
};

// Bounded message allocation and finite I/O wait budget (no INFINITE/MAXDWORD).
// The budget covers a whole frame, so a truncated or stalled frame expires as
// IoTimedOut (ConnectTimedOut while accepting). A signaled cancel event
// interrupts a pending wait and is reported as Cancelled; it does not discard
// an operation that the kernel already completed synchronously.
// On expiry, pending I/O is cancelled and drained before its memory is freed.
// Cancellation completion can exceed the budget; this is not a hard real-time
// deadline or protection against a faulty kernel driver.
struct PipeLimits {
    std::uint32_t max_message_bytes = 4096;
    std::uint32_t deadline_ms = 10000;
};

// Access a client must be granted and requests: data read/write, attribute read
// and synchronize only. Notably this excludes FILE_CREATE_PIPE_INSTANCE, which
// the generic-write right would otherwise imply.
inline constexpr DWORD kClientPipeAccess =
    FILE_READ_DATA | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE;

// Local, single-instance, authenticated named-pipe server endpoint.
//
// The authenticated identity of the peer is derived ONLY from the kernel: the
// client process id reported for this exact pipe instance, that process's real
// primary token, and an impersonation cross-check that binds the connection to
// that same logon: the context that opened the pipe and the primary token must
// agree on user SID, AuthenticationId, session id and logon SIDs. A process
// created after the connection was observed is refused as a reused id. A
// configured logon policy is re-checked against the primary token. A user, SID
// or session id declared inside any message is never treated as an authority.
//
// State is one-shot and fail-closed. ReadMessage/WriteMessage work only after a
// successful AcceptAndVerify; any accept failure or any later transport error
// closes the endpoint, so ambiguous bytes are never reused as a verified
// exchange.
// Callers must serialize operations and destruction on each endpoint. The
// optional cancel event must remain valid until the operation returns.
//
// This is a control-plane transport primitive for the native core. It creates
// no account, session or terminal, routes no input and presents no desktop. A
// returned VerifiedIdentity is a token snapshot, not proof that a seat exists.
class AuthenticatedPipeServer final {
public:
    // Creates the first (and only) instance of a local pipe. By default the
    // DACL grants only the creating user the minimal client access, with remote
    // clients rejected by the kernel. A non-empty allowed_client_sid (canonical
    // logon SID string) applies trusted server policy instead, for a differently
    // owned client such as a service-hosted core talking to a user session.
    // The DACL applies it at open time; AcceptAndVerify additionally requires
    // it to be the peer primary token's logon SID (PeerPolicyMismatch).
    // Never obtain this policy from an untrusted payload. Other SID types are
    // refused. The default user-scoped policy is not a same-user isolation boundary.
    [[nodiscard]] static AuthenticatedPipeServer Create(const std::wstring& name,
                                                        PipeLimits limits = {},
                                                        const std::wstring& allowed_client_sid = {});

    AuthenticatedPipeServer(AuthenticatedPipeServer&& other) noexcept;
    AuthenticatedPipeServer& operator=(AuthenticatedPipeServer&& other) noexcept;
    AuthenticatedPipeServer(const AuthenticatedPipeServer&) = delete;
    AuthenticatedPipeServer& operator=(const AuthenticatedPipeServer&) = delete;
    ~AuthenticatedPipeServer();

    // Waits for one local client within the deadline and returns the verified
    // identity of the actual peer process. Throws PipeError on transport or
    // binding failure. When the bound peer is a real process but not an
    // interactive seat principal, the underlying IdentityError is propagated
    // unchanged, so the VerifiedIdentity contract is never weakened here. Any
    // failure fail-closes the endpoint; it is callable once.
    [[nodiscard]] VerifiedIdentity AcceptAndVerify(HANDLE cancel = nullptr);

    [[nodiscard]] std::vector<unsigned char> ReadMessage(HANDLE cancel = nullptr);
    void WriteMessage(const unsigned char* data, std::size_t size, HANDLE cancel = nullptr);

private:
    enum class State { Fresh, Verified, Closed };

    AuthenticatedPipeServer(HANDLE pipe, PipeLimits limits, std::vector<unsigned char> policy_sid) noexcept;
    void FailClosed() noexcept;

    HANDLE pipe_;
    PipeLimits limits_;
    std::vector<unsigned char> policy_sid_; // empty: default user-scoped DACL
    State state_;
};

// Minimal local client used by the native core's unprivileged side and by the
// automated tests. It connects granting the server identification-level
// impersonation only and requests kClientPipeAccess (no generic write), so the
// server can read who the client is but cannot act as the client. It is also
// fail-closed: a transport error closes the endpoint.
// Operations and destruction must be serialized. The server's identity is not
// authenticated here: obtain the endpoint name from a trusted source.
class AuthenticatedPipeClient final {
public:
    [[nodiscard]] static AuthenticatedPipeClient Connect(const std::wstring& name,
                                                         PipeLimits limits = {});

    AuthenticatedPipeClient(AuthenticatedPipeClient&& other) noexcept;
    AuthenticatedPipeClient& operator=(AuthenticatedPipeClient&& other) noexcept;
    AuthenticatedPipeClient(const AuthenticatedPipeClient&) = delete;
    AuthenticatedPipeClient& operator=(const AuthenticatedPipeClient&) = delete;
    ~AuthenticatedPipeClient();

    [[nodiscard]] std::vector<unsigned char> ReadMessage(HANDLE cancel = nullptr);
    void WriteMessage(const unsigned char* data, std::size_t size, HANDLE cancel = nullptr);

private:
    AuthenticatedPipeClient(HANDLE pipe, PipeLimits limits) noexcept;
    void FailClosed() noexcept;

    HANDLE pipe_;
    PipeLimits limits_;
    bool closed_;
};

} // namespace multiseat::native::ipc
