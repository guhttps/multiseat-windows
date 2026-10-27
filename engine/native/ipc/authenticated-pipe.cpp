#include "authenticated-pipe.h"

#include <sddl.h>

#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace multiseat::native::ipc {
namespace {

constexpr std::uint32_t kAbsoluteMaxMessageBytes = 1024U * 1024U;
constexpr std::uint32_t kAbsoluteMaxDeadlineMs = 120000U; // finite ceiling, never INFINITE
constexpr DWORD kMaxTokenQueryBytes = 1024U * 1024U; // same ceiling as the identity module
constexpr DWORD kIoChunkBytes = 65536U;

static_assert(kClientPipeAccess == 0x00100083U,
              "The client access mask must stay explicit and exclude FILE_CREATE_PIPE_INSTANCE.");

// Closes any owned handle; normalises INVALID_HANDLE_VALUE to the empty state
// so a single Valid() predicate works for pipe, process, token and event
// handles alike.
class UniqueHandle final {
public:
    UniqueHandle() noexcept : handle_(nullptr) {}
    explicit UniqueHandle(HANDLE handle) noexcept
        : handle_(handle == INVALID_HANDLE_VALUE ? nullptr : handle) {}
    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    ~UniqueHandle() { Reset(); }

    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }
    [[nodiscard]] bool Valid() const noexcept { return handle_ != nullptr; }

private:
    void Reset() noexcept {
        if (handle_ != nullptr) CloseHandle(handle_);
        handle_ = nullptr;
    }
    HANDLE handle_;
};

struct LocalFreeDeleter final {
    void* value = nullptr;
    ~LocalFreeDeleter() {
        if (value != nullptr) LocalFree(value);
    }
};

[[nodiscard]] DWORD RemainingMilliseconds(ULONGLONG deadline) noexcept {
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline) return 0;
    const ULONGLONG left = deadline - now;
    return left > static_cast<ULONGLONG>(kAbsoluteMaxDeadlineMs) ? kAbsoluteMaxDeadlineMs
                                                                 : static_cast<DWORD>(left);
}

// Waits for one overlapped operation to finish within the timeout, honouring an
// optional external cancel event. On timeout or cancel the operation is
// cancelled and drained to completion before returning, so the OVERLAPPED and
// its buffer stay valid until the kernel is truly done with them.
void CompletePending(HANDLE pipe, OVERLAPPED& overlapped, HANDLE cancel, DWORD timeout,
                     DWORD& transferred, PipeFailure timeout_reason) {
    HANDLE waits[2];
    DWORD count = 0;
    waits[count++] = overlapped.hEvent;
    if (cancel != nullptr) waits[count++] = cancel;
    const DWORD result = WaitForMultipleObjects(count, waits, FALSE, timeout);
    const DWORD wait_error = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
    if (result == WAIT_OBJECT_0) {
        if (!GetOverlappedResult(pipe, &overlapped, &transferred, FALSE)) {
            const DWORD error = GetLastError();
            const bool gone = error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED ||
                              error == ERROR_NO_DATA;
            throw PipeError(gone ? PipeFailure::Disconnected : PipeFailure::IoFailed, error);
        }
        return;
    }
    CancelIoEx(pipe, &overlapped);
    DWORD drained = 0;
    (void)GetOverlappedResult(pipe, &overlapped, &drained, TRUE);
    if (result == WAIT_TIMEOUT) throw PipeError(timeout_reason);
    if (cancel != nullptr && result == WAIT_OBJECT_0 + 1) throw PipeError(PipeFailure::Cancelled);
    throw PipeError(PipeFailure::IoFailed, wait_error);
}

void ReadExact(HANDLE pipe, unsigned char* out, std::size_t total, ULONGLONG deadline, HANDLE cancel) {
    std::size_t done = 0;
    while (done < total) {
        const DWORD remaining = RemainingMilliseconds(deadline);
        if (remaining == 0) throw PipeError(PipeFailure::IoTimedOut);
        UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.Valid()) throw PipeError(PipeFailure::IoFailed, GetLastError());
        OVERLAPPED overlapped{};
        overlapped.hEvent = event.Get();
        const std::size_t left = total - done;
        const DWORD want = left > kIoChunkBytes ? kIoChunkBytes : static_cast<DWORD>(left);
        DWORD got = 0;
        const BOOL ok = ReadFile(pipe, out + done, want, &got, &overlapped);
        if (!ok) {
            const DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                CompletePending(pipe, overlapped, cancel, remaining, got, PipeFailure::IoTimedOut);
            } else if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                throw PipeError(PipeFailure::Disconnected, error);
            } else {
                throw PipeError(PipeFailure::IoFailed, error);
            }
        }
        if (got == 0) throw PipeError(PipeFailure::Disconnected);
        done += got;
    }
}

void WriteAllBytes(HANDLE pipe, const unsigned char* data, std::size_t total, ULONGLONG deadline,
                   HANDLE cancel) {
    std::size_t done = 0;
    while (done < total) {
        const DWORD remaining = RemainingMilliseconds(deadline);
        if (remaining == 0) throw PipeError(PipeFailure::IoTimedOut);
        UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.Valid()) throw PipeError(PipeFailure::IoFailed, GetLastError());
        OVERLAPPED overlapped{};
        overlapped.hEvent = event.Get();
        const std::size_t left = total - done;
        const DWORD want = left > kIoChunkBytes ? kIoChunkBytes : static_cast<DWORD>(left);
        DWORD wrote = 0;
        const BOOL ok = WriteFile(pipe, data + done, want, &wrote, &overlapped);
        if (!ok) {
            const DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                CompletePending(pipe, overlapped, cancel, remaining, wrote, PipeFailure::IoTimedOut);
            } else if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA ||
                       error == ERROR_PIPE_NOT_CONNECTED) {
                throw PipeError(PipeFailure::Disconnected, error);
            } else {
                throw PipeError(PipeFailure::IoFailed, error);
            }
        }
        if (wrote == 0) throw PipeError(PipeFailure::Disconnected);
        done += wrote;
    }
}

[[nodiscard]] std::vector<unsigned char> ReadFramedMessage(HANDLE pipe, const PipeLimits& limits,
                                                           HANDLE cancel) {
    const ULONGLONG deadline = GetTickCount64() + limits.deadline_ms;
    unsigned char header[4] = {};
    ReadExact(pipe, header, sizeof(header), deadline, cancel);
    const std::uint32_t size = static_cast<std::uint32_t>(header[0]) |
                               (static_cast<std::uint32_t>(header[1]) << 8) |
                               (static_cast<std::uint32_t>(header[2]) << 16) |
                               (static_cast<std::uint32_t>(header[3]) << 24);
    if (size > limits.max_message_bytes) throw PipeError(PipeFailure::MessageTooLarge);
    std::vector<unsigned char> payload(size);
    if (size != 0) ReadExact(pipe, payload.data(), size, deadline, cancel);
    return payload;
}

void WriteFramedMessage(HANDLE pipe, const unsigned char* data, std::size_t size,
                        const PipeLimits& limits, HANDLE cancel) {
    const std::uint32_t count = static_cast<std::uint32_t>(size);
    std::vector<unsigned char> frame(sizeof(std::uint32_t) + size);
    frame[0] = static_cast<unsigned char>(count & 0xFF);
    frame[1] = static_cast<unsigned char>((count >> 8) & 0xFF);
    frame[2] = static_cast<unsigned char>((count >> 16) & 0xFF);
    frame[3] = static_cast<unsigned char>((count >> 24) & 0xFF);
    if (size != 0) std::memcpy(frame.data() + sizeof(std::uint32_t), data, size);
    const ULONGLONG deadline = GetTickCount64() + limits.deadline_ms;
    WriteAllBytes(pipe, frame.data(), frame.size(), deadline, cancel);
}

// Queries a variable-size token class into a buffer trimmed to the returned
// length, with the kernel-reported size bounded before allocation.
[[nodiscard]] std::vector<unsigned char> QueryTokenBuffer(HANDLE token, TOKEN_INFORMATION_CLASS information,
                                                          std::size_t minimum_bytes, PipeFailure failure) {
    DWORD needed = 0;
    const BOOL initial = GetTokenInformation(token, information, nullptr, 0, &needed);
    const DWORD query_error = GetLastError();
    if (initial || query_error != ERROR_INSUFFICIENT_BUFFER || needed < minimum_bytes ||
        needed > kMaxTokenQueryBytes) {
        throw PipeError(failure, initial || query_error == ERROR_INSUFFICIENT_BUFFER ? ERROR_INVALID_DATA
                                                                                  : query_error);
    }
    std::vector<unsigned char> buffer(needed);
    DWORD returned = 0;
    if (!GetTokenInformation(token, information, buffer.data(), needed, &returned)) {
        throw PipeError(failure, GetLastError());
    }
    if (returned < minimum_bytes || returned > buffer.size()) {
        throw PipeError(failure, ERROR_INVALID_DATA);
    }
    buffer.resize(returned);
    return buffer;
}

// Copies the SID only after bounding every token-supplied pointer and length.
[[nodiscard]] std::vector<unsigned char> CopyBoundedSid(PSID sid, const std::vector<unsigned char>& buffer,
                                                        PipeFailure failure) {
    const auto begin = reinterpret_cast<std::uintptr_t>(buffer.data());
    const auto address = reinterpret_cast<std::uintptr_t>(sid);
    constexpr std::size_t sid_header_bytes = offsetof(SID, SubAuthority);
    if (sid == nullptr || address < begin || address - begin > buffer.size() ||
        buffer.size() - (address - begin) < sid_header_bytes) {
        throw PipeError(failure, ERROR_INVALID_SID);
    }
    const auto offset = static_cast<std::size_t>(address - begin);
    const std::size_t sid_bytes = sid_header_bytes +
        static_cast<std::size_t>(buffer[offset + offsetof(SID, SubAuthorityCount)]) * sizeof(DWORD);
    if (sid_bytes > buffer.size() - offset || !IsValidSid(sid) || GetLengthSid(sid) != sid_bytes) {
        throw PipeError(failure, ERROR_INVALID_SID);
    }
    std::vector<unsigned char> copy(sid_bytes);
    if (!CopySid(static_cast<DWORD>(sid_bytes), copy.data(), sid)) {
        throw PipeError(failure, GetLastError());
    }
    return copy;
}

[[nodiscard]] std::vector<unsigned char> ReadUserSid(HANDLE token, PipeFailure failure) {
    const auto buffer = QueryTokenBuffer(token, TokenUser, sizeof(TOKEN_USER), failure);
    TOKEN_USER user{};
    std::memcpy(&user, buffer.data(), sizeof(user));
    return CopyBoundedSid(user.User.Sid, buffer, failure);
}

// Every group the kernel marks as a logon identifier, in token order. Neither
// presence nor enablement is judged here; this is only a comparable fact.
[[nodiscard]] std::vector<std::vector<unsigned char>> ReadLogonSids(HANDLE token, PipeFailure failure) {
    constexpr std::size_t groups_offset = offsetof(TOKEN_GROUPS, Groups);
    const auto buffer = QueryTokenBuffer(token, TokenGroups, groups_offset, failure);
    DWORD count = 0;
    std::memcpy(&count, buffer.data(), sizeof(count));
    if (count > (buffer.size() - groups_offset) / sizeof(SID_AND_ATTRIBUTES)) {
        throw PipeError(failure, ERROR_INVALID_DATA);
    }
    std::vector<std::vector<unsigned char>> logon_sids;
    for (DWORD index = 0; index < count; ++index) {
        SID_AND_ATTRIBUTES group{};
        std::memcpy(&group, buffer.data() + groups_offset + static_cast<std::size_t>(index) * sizeof(group),
                    sizeof(group));
        if ((group.Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID) {
            logon_sids.push_back(CopyBoundedSid(group.Sid, buffer, failure));
        }
    }
    return logon_sids;
}

[[nodiscard]] DWORD ReadSessionId(HANDLE token, PipeFailure failure) {
    DWORD session = 0;
    DWORD returned = 0;
    if (!GetTokenInformation(token, TokenSessionId, &session, sizeof(session), &returned)) {
        throw PipeError(failure, GetLastError());
    }
    if (returned != sizeof(session)) throw PipeError(failure, ERROR_INVALID_DATA);
    return session;
}

// Builds a self-relative security descriptor owned by the creating user whose
// single allowed DACL entry grants the chosen principal the explicit minimal
// client access (never a generic mask). The creating process keeps full control
// through the handle it already holds; the DACL only constrains who may open
// the pipe afterwards. A configured logon policy is also returned in binary
// form so that the accepted peer's primary token can be held to it.
[[nodiscard]] PSECURITY_DESCRIPTOR CreateClientAccessDescriptor(const std::wstring& allowed_sid,
                                                                std::vector<unsigned char>& policy_sid) {
    HANDLE raw_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token)) {
        throw PipeError(PipeFailure::SecurityDescriptorFailed, GetLastError());
    }
    const UniqueHandle token(raw_token);
    const auto owner_sid = ReadUserSid(token.Get(), PipeFailure::SecurityDescriptorFailed);
    LPWSTR owner_text = nullptr;
    if (!ConvertSidToStringSidW(const_cast<unsigned char*>(owner_sid.data()), &owner_text)) {
        throw PipeError(PipeFailure::SecurityDescriptorFailed, GetLastError());
    }
    const LocalFreeDeleter owner_guard{owner_text};

    std::wstring allowed(owner_text);
    // This is trusted server policy, never a principal supplied by a message.
    // Restrict the optional policy to a canonical logon SID, not broad groups.
    if (!allowed_sid.empty()) {
        if (allowed_sid.find(L'\0') != std::wstring::npos) {
            throw PipeError(PipeFailure::SecurityDescriptorFailed, ERROR_INVALID_SID);
        }
        PSID parsed = nullptr;
        if (!ConvertStringSidToSidW(allowed_sid.c_str(), &parsed)) {
            throw PipeError(PipeFailure::SecurityDescriptorFailed, GetLastError());
        }
        const LocalFreeDeleter parsed_guard{parsed};
        if (!IsWellKnownSid(parsed, WinLogonIdsSid)) {
            throw PipeError(PipeFailure::SecurityDescriptorFailed, ERROR_INVALID_SID);
        }
        LPWSTR canonical = nullptr;
        if (!ConvertSidToStringSidW(parsed, &canonical)) {
            throw PipeError(PipeFailure::SecurityDescriptorFailed, GetLastError());
        }
        const LocalFreeDeleter canonical_guard{canonical};
        allowed.assign(canonical);
        const auto* bytes = static_cast<const unsigned char*>(parsed);
        policy_sid.assign(bytes, bytes + GetLengthSid(parsed));
    }

    std::wstring descriptor = L"O:";
    descriptor += owner_text;
    descriptor += L"G:";
    descriptor += owner_text;
    descriptor += L"D:P(A;;0x00100083;;;";
    descriptor += allowed;
    descriptor += L")";
    PSECURITY_DESCRIPTOR security = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(), SDDL_REVISION_1,
                                                              &security, nullptr)) {
        throw PipeError(PipeFailure::SecurityDescriptorFailed, GetLastError());
    }
    return security;
}

[[nodiscard]] LUID ReadLogonId(HANDLE token) {
    TOKEN_STATISTICS statistics{};
    DWORD returned = 0;
    if (!GetTokenInformation(token, TokenStatistics, &statistics, sizeof(statistics), &returned)) {
        throw PipeError(PipeFailure::PeerQueryFailed, GetLastError());
    }
    if (returned != sizeof(statistics)) throw PipeError(PipeFailure::PeerQueryFailed, ERROR_INVALID_DATA);
    return statistics.AuthenticationId;
}

[[nodiscard]] bool SameLuid(const LUID& a, const LUID& b) noexcept {
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

void ValidateName(const std::wstring& name) {
    static const std::wstring prefix = L"\\\\.\\pipe\\";
    if (name.size() <= prefix.size() || name.size() > 256 || name.rfind(prefix, 0) != 0 ||
        name.find(L'\0') != std::wstring::npos) {
        throw PipeError(PipeFailure::NameInvalid);
    }
    // Require exactly one path component after the prefix so the name cannot
    // normalize out of the \\.\pipe\ device namespace. A separator or a "."/".."
    // segment (e.g. "\\.\pipe\..\x" -> "\\.\x") would resolve to a different
    // device object; a trailing dot or space is also altered by Windows path
    // normalization. This is a pure text contract: it opens nothing.
    const std::wstring component = name.substr(prefix.size());
    if (component == L"." || component == L"..") throw PipeError(PipeFailure::NameInvalid);
    for (const wchar_t c : component) {
        if (c == L'\\' || c == L'/' || c < 0x20) throw PipeError(PipeFailure::NameInvalid);
    }
    const wchar_t last = component.back();
    if (last == L'.' || last == L' ') throw PipeError(PipeFailure::NameInvalid);
}

void ValidateLimits(const PipeLimits& limits) {
    if (limits.max_message_bytes == 0 || limits.max_message_bytes > kAbsoluteMaxMessageBytes ||
        limits.deadline_ms == 0 || limits.deadline_ms > kAbsoluteMaxDeadlineMs) {
        throw PipeError(PipeFailure::LimitsInvalid);
    }
}

} // namespace

PipeError::PipeError(PipeFailure reason, DWORD win32_error)
    : std::runtime_error("Authenticated local pipe operation failed."),
      reason_(reason),
      win32_error_(win32_error) {}

PipeFailure PipeError::Reason() const noexcept { return reason_; }
DWORD PipeError::Win32Error() const noexcept { return win32_error_; }

AuthenticatedPipeServer::AuthenticatedPipeServer(HANDLE pipe, PipeLimits limits,
                                                 std::vector<unsigned char> policy_sid) noexcept
    : pipe_(pipe), limits_(limits), policy_sid_(std::move(policy_sid)), state_(State::Fresh) {}

AuthenticatedPipeServer::AuthenticatedPipeServer(AuthenticatedPipeServer&& other) noexcept
    : pipe_(other.pipe_), limits_(other.limits_), policy_sid_(std::move(other.policy_sid_)),
      state_(other.state_) {
    other.pipe_ = INVALID_HANDLE_VALUE;
    other.state_ = State::Closed;
}

AuthenticatedPipeServer& AuthenticatedPipeServer::operator=(AuthenticatedPipeServer&& other) noexcept {
    if (this != &other) {
        if (pipe_ != INVALID_HANDLE_VALUE && pipe_ != nullptr) CloseHandle(pipe_);
        pipe_ = other.pipe_;
        limits_ = other.limits_;
        policy_sid_ = std::move(other.policy_sid_);
        state_ = other.state_;
        other.pipe_ = INVALID_HANDLE_VALUE;
        other.state_ = State::Closed;
    }
    return *this;
}

AuthenticatedPipeServer::~AuthenticatedPipeServer() {
    if (pipe_ != INVALID_HANDLE_VALUE && pipe_ != nullptr) CloseHandle(pipe_);
}

void AuthenticatedPipeServer::FailClosed() noexcept {
    if (pipe_ != INVALID_HANDLE_VALUE && pipe_ != nullptr) {
        (void)DisconnectNamedPipe(pipe_);
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    state_ = State::Closed;
}

AuthenticatedPipeServer AuthenticatedPipeServer::Create(const std::wstring& name, PipeLimits limits,
                                                        const std::wstring& allowed_client_sid) {
    ValidateName(name);
    ValidateLimits(limits);
    std::vector<unsigned char> policy_sid;
    PSECURITY_DESCRIPTOR descriptor = CreateClientAccessDescriptor(allowed_client_sid, policy_sid);
    const LocalFreeDeleter descriptor_guard{descriptor};
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
    const HANDLE pipe = CreateNamedPipeW(
        name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, limits.max_message_bytes, limits.max_message_bytes, 0, &attributes);
    if (pipe == INVALID_HANDLE_VALUE) throw PipeError(PipeFailure::CreateFailed, GetLastError());
    return AuthenticatedPipeServer(pipe, limits, std::move(policy_sid));
}

VerifiedIdentity AuthenticatedPipeServer::AcceptAndVerify(HANDLE cancel) {
    if (state_ != State::Fresh) throw PipeError(PipeFailure::NotVerified);
    try {
        // Wait for exactly one client connection within the deadline.
        UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.Valid()) throw PipeError(PipeFailure::ConnectFailed, GetLastError());
        OVERLAPPED overlapped{};
        overlapped.hEvent = event.Get();
        if (!ConnectNamedPipe(pipe_, &overlapped)) {
            const DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                DWORD transferred = 0;
                CompletePending(pipe_, overlapped, cancel, limits_.deadline_ms, transferred,
                                PipeFailure::ConnectTimedOut);
            } else if (error != ERROR_PIPE_CONNECTED) {
                throw PipeError(PipeFailure::ConnectFailed, error);
            }
        }
        // The real client existed before this instant; see the creation check.
        FILETIME connected_at{};
        GetSystemTimePreciseAsFileTime(&connected_at);

        // The kernel attests which process owns the client end of this instance.
        ULONG peer_pid = 0;
        if (!GetNamedPipeClientProcessId(pipe_, &peer_pid)) {
            throw PipeError(PipeFailure::PeerQueryFailed, GetLastError());
        }
        if (peer_pid <= 4) throw PipeError(PipeFailure::PeerBindingMismatch);

        // Capture the connected client's logon facts by impersonating it. The
        // calling thread must not already be impersonating, so a pre-existing
        // thread token is never silently discarded. The impersonation is
        // reverted explicitly with a checked result, and a scope net reverts on
        // any throw in between.
        {
            HANDLE existing = nullptr;
            if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &existing)) {
                CloseHandle(existing);
                throw PipeError(PipeFailure::ThreadAlreadyImpersonating);
            }
            const DWORD error = GetLastError();
            if (error != ERROR_NO_TOKEN) {
                // An unreadable token is not evidence that no token exists.
                throw PipeError(PipeFailure::ThreadAlreadyImpersonating, error);
            }
        }
        // Declared before impersonating: nothing may throw between the
        // impersonation and the construction of the revert net below.
        LUID client_logon{};
        DWORD client_session = 0;
        std::vector<unsigned char> client_user_sid;
        std::vector<std::vector<unsigned char>> client_logon_sids;
        bool reverted = false;
        if (!ImpersonateNamedPipeClient(pipe_)) {
            throw PipeError(PipeFailure::ImpersonationFailed, GetLastError());
        }
        struct RevertNet {
            bool* done;
            ~RevertNet() noexcept {
                if (!*done && !RevertToSelf()) {
                    // Continuing under a client's context is unsafe. A failed
                    // restore is terminal, including during exception cleanup.
                    RaiseFailFastException(nullptr, nullptr, 0);
                    std::terminate();
                }
            }
        } revert_net{&reverted};
        {
            HANDLE raw_thread_token = nullptr;
            if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw_thread_token)) {
                throw PipeError(PipeFailure::ImpersonationFailed, GetLastError());
            }
            const UniqueHandle thread_token(raw_thread_token);
            client_logon = ReadLogonId(thread_token.Get());
            client_session = ReadSessionId(thread_token.Get(), PipeFailure::ImpersonationFailed);
            client_user_sid = ReadUserSid(thread_token.Get(), PipeFailure::ImpersonationFailed);
            client_logon_sids = ReadLogonSids(thread_token.Get(), PipeFailure::ImpersonationFailed);
        }
        if (!RevertToSelf()) throw PipeError(PipeFailure::ImpersonationRevertFailed, GetLastError());
        reverted = true;

        // Open the attested peer process (as ourselves) and read its real
        // primary token. Reject a process that has already exited.
        const UniqueHandle process(
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, peer_pid));
        if (!process.Valid()) throw PipeError(PipeFailure::PeerProcessUnavailable, GetLastError());
        if (WaitForSingleObject(process.Get(), 0) != WAIT_TIMEOUT) {
            throw PipeError(PipeFailure::PeerProcessUnavailable);
        }
        // A process created after the connection was observed cannot be the
        // client, so its id was reused. This is a wall-clock heuristic, not a
        // proof: a clock adjustment between the observation and a reused-id
        // process's creation can both falsely refuse a legitimate peer and miss
        // reuse -- a backward step can make a later process read as earlier. The
        // user/logon binding below, not this check, is the real protection.
        FILETIME created{};
        FILETIME exited{};
        FILETIME kernel_time{};
        FILETIME user_time{};
        if (!GetProcessTimes(process.Get(), &created, &exited, &kernel_time, &user_time)) {
            throw PipeError(PipeFailure::PeerProcessUnavailable, GetLastError());
        }
        if (CompareFileTime(&created, &connected_at) > 0) {
            throw PipeError(PipeFailure::PeerBindingMismatch);
        }
        HANDLE raw_primary = nullptr;
        if (!OpenProcessToken(process.Get(), TOKEN_QUERY | TOKEN_DUPLICATE, &raw_primary)) {
            throw PipeError(PipeFailure::PeerTokenUnavailable, GetLastError());
        }
        const UniqueHandle primary(raw_primary);

        // Bind the connection to the process over every fact the returned
        // identity carries: the context that opened the pipe and the opened
        // process must share user SID, AuthenticationId, session id and the
        // kernel-marked logon SIDs. This narrows, but does not by itself
        // eliminate, a PID-reuse race.
        const LUID process_logon = ReadLogonId(primary.Get());
        const DWORD process_session = ReadSessionId(primary.Get(), PipeFailure::PeerBindingMismatch);
        const auto process_user_sid = ReadUserSid(primary.Get(), PipeFailure::PeerBindingMismatch);
        const auto process_logon_sids = ReadLogonSids(primary.Get(), PipeFailure::PeerBindingMismatch);
        if (!SameLuid(client_logon, process_logon) || client_session != process_session ||
            client_user_sid != process_user_sid || client_logon_sids != process_logon_sids) {
            throw PipeError(PipeFailure::PeerBindingMismatch);
        }

        // The identity module enforces its own seat contract (interactive,
        // primary, session != 0, non-service, exactly one enabled logon SID).
        // It throws IdentityError for a non-seat token, which callers receive
        // unchanged.
        VerifiedIdentity identity = VerifiedIdentity::FromPrimaryToken(primary.Get());

        // The DACL applied a configured logon policy to whichever token opened
        // the pipe. Hold the primary token the identity came from to it too.
        if (!policy_sid_.empty() &&
            (process_logon_sids.size() != 1 || process_logon_sids.front() != policy_sid_)) {
            throw PipeError(PipeFailure::PeerPolicyMismatch);
        }
        state_ = State::Verified;
        return identity;
    } catch (...) {
        FailClosed();
        throw;
    }
}

std::vector<unsigned char> AuthenticatedPipeServer::ReadMessage(HANDLE cancel) {
    if (state_ != State::Verified) throw PipeError(PipeFailure::NotVerified);
    try {
        return ReadFramedMessage(pipe_, limits_, cancel);
    } catch (...) {
        FailClosed();
        throw;
    }
}

void AuthenticatedPipeServer::WriteMessage(const unsigned char* data, std::size_t size, HANDLE cancel) {
    if (size > limits_.max_message_bytes) throw PipeError(PipeFailure::MessageTooLarge);
    if (size > 0 && data == nullptr) throw PipeError(PipeFailure::InvalidArgument);
    if (state_ != State::Verified) throw PipeError(PipeFailure::NotVerified);
    try {
        WriteFramedMessage(pipe_, data, size, limits_, cancel);
    } catch (...) {
        FailClosed();
        throw;
    }
}

AuthenticatedPipeClient::AuthenticatedPipeClient(HANDLE pipe, PipeLimits limits) noexcept
    : pipe_(pipe), limits_(limits), closed_(false) {}

AuthenticatedPipeClient::AuthenticatedPipeClient(AuthenticatedPipeClient&& other) noexcept
    : pipe_(other.pipe_), limits_(other.limits_), closed_(other.closed_) {
    other.pipe_ = INVALID_HANDLE_VALUE;
    other.closed_ = true;
}

AuthenticatedPipeClient& AuthenticatedPipeClient::operator=(AuthenticatedPipeClient&& other) noexcept {
    if (this != &other) {
        if (pipe_ != INVALID_HANDLE_VALUE && pipe_ != nullptr) CloseHandle(pipe_);
        pipe_ = other.pipe_;
        limits_ = other.limits_;
        closed_ = other.closed_;
        other.pipe_ = INVALID_HANDLE_VALUE;
        other.closed_ = true;
    }
    return *this;
}

AuthenticatedPipeClient::~AuthenticatedPipeClient() {
    if (pipe_ != INVALID_HANDLE_VALUE && pipe_ != nullptr) CloseHandle(pipe_);
}

void AuthenticatedPipeClient::FailClosed() noexcept {
    if (pipe_ != INVALID_HANDLE_VALUE && pipe_ != nullptr) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    closed_ = true;
}

AuthenticatedPipeClient AuthenticatedPipeClient::Connect(const std::wstring& name, PipeLimits limits) {
    ValidateName(name);
    ValidateLimits(limits);
    const ULONGLONG deadline = GetTickCount64() + limits.deadline_ms;
    for (;;) {
        const HANDLE pipe = CreateFileW(
            name.c_str(), kClientPipeAccess, 0, nullptr, OPEN_EXISTING,
            SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION | FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) return AuthenticatedPipeClient(pipe, limits);
        const DWORD error = GetLastError();
        if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND) {
            throw PipeError(PipeFailure::ClientConnectFailed, error);
        }
        const DWORD remaining = RemainingMilliseconds(deadline);
        if (remaining == 0) throw PipeError(PipeFailure::ConnectTimedOut, error);
        if (error == ERROR_PIPE_BUSY) {
            (void)WaitNamedPipeW(name.c_str(), remaining > 250 ? 250 : remaining);
        } else {
            Sleep(remaining > 15 ? 15 : remaining);
        }
    }
}

std::vector<unsigned char> AuthenticatedPipeClient::ReadMessage(HANDLE cancel) {
    if (closed_) throw PipeError(PipeFailure::Disconnected);
    try {
        return ReadFramedMessage(pipe_, limits_, cancel);
    } catch (...) {
        FailClosed();
        throw;
    }
}

void AuthenticatedPipeClient::WriteMessage(const unsigned char* data, std::size_t size, HANDLE cancel) {
    if (size > limits_.max_message_bytes) throw PipeError(PipeFailure::MessageTooLarge);
    if (size > 0 && data == nullptr) throw PipeError(PipeFailure::InvalidArgument);
    if (closed_) throw PipeError(PipeFailure::Disconnected);
    try {
        WriteFramedMessage(pipe_, data, size, limits_, cancel);
    } catch (...) {
        FailClosed();
        throw;
    }
}

} // namespace multiseat::native::ipc
