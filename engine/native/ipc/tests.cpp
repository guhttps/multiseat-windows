// Exercises the authenticated local IPC layer with a real named pipe and a real
// child process acting as the client. Nothing here creates an account, session,
// terminal, service or driver, and no SID, token value or path is printed.
//
// A non-interactive CI runner may have no interactive seat identity. In that
// case AcceptAndVerify reaches the identity step and the identity module raises
// its EXACT non-seat rejection (checked against an independent observation of
// the current process token), the endpoint fail-closes and the child client
// must observe that closure. The server data plane is then NOT exercised; the
// shared framing is still covered from the client side against a raw pipe.

#include "ipc/authenticated-pipe.h"
#include "identity.h"

#include <sddl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using multiseat::native::IdentityError;
using multiseat::native::IdentityFailure;
using multiseat::native::VerifiedIdentity;
using multiseat::native::ipc::AuthenticatedPipeClient;
using multiseat::native::ipc::AuthenticatedPipeServer;
using multiseat::native::ipc::PipeError;
using multiseat::native::ipc::PipeFailure;
using multiseat::native::ipc::PipeLimits;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Independent observation of whether this process has an interactive seat
// identity, and if not, the exact category the identity module must report.
[[nodiscard]] std::optional<IdentityFailure> NonSeatCategory() {
    try {
        (void)VerifiedIdentity::CurrentProcess();
        return std::nullopt;
    } catch (const IdentityError& error) {
        return error.Reason();
    }
}

// The pipe name family is fixed: \\.\pipe\ms-native-ipc-<pid>-<sequence>. A child
// never receives a name on its command line; it receives only the parent's PID and
// a numeric sequence and rebuilds the name here, so no command-line string can flow
// into CreateFile.
[[nodiscard]] std::wstring MakeNameFor(unsigned pid, unsigned sequence) {
    std::wstring name = L"\\\\.\\pipe\\ms-native-ipc-";
    name += std::to_wstring(pid);
    name += L'-';
    name += std::to_wstring(sequence);
    return name;
}
[[nodiscard]] std::wstring MakeName(unsigned sequence) {
    return MakeNameFor(GetCurrentProcessId(), sequence);
}
// Strict, bounded decimal parse (digits only, <= 2^32-1). A non-numeric or
// out-of-range token is rejected, so an argument can only ever contribute a
// bounded integer to the name, never a path.
[[nodiscard]] bool ParseBoundedUint(const wchar_t* text, unsigned& out) {
    if (text == nullptr || text[0] == L'\0') return false;
    unsigned long long value = 0;
    for (const wchar_t* p = text; *p != L'\0'; ++p) {
        if (*p < L'0' || *p > L'9') return false;
        value = value * 10ull + static_cast<unsigned long long>(*p - L'0');
        if (value > 0xFFFFFFFFull) return false;
    }
    out = static_cast<unsigned>(value);
    return true;
}

// Owns a spawned client process. If it is never waited (for example because an
// assertion threw first), the destructor terminates and reaps it, so a failing
// run never leaves an orphan process behind.
class Child final {
public:
    explicit Child(HANDLE process) noexcept : process_(process) {}
    Child(Child&& other) noexcept : process_(other.process_), reaped_(other.reaped_) {
        other.process_ = nullptr;
        other.reaped_ = true;
    }
    Child& operator=(const Child&) = delete;
    Child(const Child&) = delete;
    ~Child() {
        if (process_ == nullptr) return;
        if (!reaped_) {
            TerminateProcess(process_, 0xDEAD);
            (void)WaitForSingleObject(process_, 2000);
        }
        CloseHandle(process_);
    }

    [[nodiscard]] DWORD Wait(DWORD timeout) {
        if (WaitForSingleObject(process_, timeout) != WAIT_OBJECT_0) {
            TerminateProcess(process_, 0xDEAD);
            (void)WaitForSingleObject(process_, 2000);
            reaped_ = true;
            return 0xFFFFFFFF;
        }
        reaped_ = true;
        DWORD code = 0;
        (void)GetExitCodeProcess(process_, &code);
        return code;
    }

private:
    HANDLE process_;
    bool reaped_ = false;
};

// Spawns the test binary with a mode and exactly two command-line tokens. Normal
// scenarios pass numeric tokens via SpawnSelf; the invalid-argument regression
// passes inert tokens directly to prove they are refused before any I/O.
[[nodiscard]] Child SpawnSelfTokens(const wchar_t* mode, const std::wstring& first,
                                    const std::wstring& second) {
    wchar_t self[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, self, MAX_PATH);
    Require(length != 0 && length < MAX_PATH, "ipc test: resolving the own module path failed.");
    std::wstring command = L"\"";
    command += self;
    command += L"\" ";
    command += mode;
    command += L" ";
    command += first;
    command += L" ";
    command += second;
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(self, mutable_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &startup, &info)) {
        throw std::runtime_error("ipc test: spawning the client child process failed.");
    }
    CloseHandle(info.hThread);
    // Ownership of the process handle moves into Child, which closes it.
#pragma warning(suppress : 6335)
    return Child(info.hProcess);
}
// The child receives only the parent's PID and a numeric scenario; it rebuilds the
// pipe name inside the fixed family, so no path from the command line reaches
// CreateFile.
[[nodiscard]] Child SpawnSelf(const wchar_t* mode, unsigned sequence) {
    return SpawnSelfTokens(mode, std::to_wstring(GetCurrentProcessId()), std::to_wstring(sequence));
}

// Child exit codes. Each scenario requires exactly one expected outcome.
constexpr int kChildOk = 0;
constexpr int kChildFailed = 2;
constexpr int kChildUnexpected = 3;
constexpr int kChildSawClose = 4;
constexpr int kChildDenied = 5;
constexpr int kChildBadArg = 6;

[[nodiscard]] std::vector<unsigned char> FrameHeader(std::uint32_t declared) {
    return {static_cast<unsigned char>(declared & 0xFF), static_cast<unsigned char>((declared >> 8) & 0xFF),
            static_cast<unsigned char>((declared >> 16) & 0xFF),
            static_cast<unsigned char>((declared >> 24) & 0xFF)};
}

// A truncated frame: the header declares 32 bytes but only 5 follow.
[[nodiscard]] std::vector<unsigned char> TruncatedFrame() {
    auto frame = FrameHeader(32);
    frame.insert(frame.end(), {'p', 'a', 'r', 't', 'l'});
    return frame;
}

[[nodiscard]] bool IsGone(DWORD error) noexcept {
    return error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA || error == ERROR_PIPE_NOT_CONNECTED;
}

// A raw, non-authenticating local server end owned by the test. It feeds the
// shared framing malformed input on any runner and observes, from the other
// end, whether the client really released its handle after failing closed.
class RawServer final {
public:
    explicit RawServer(const std::wstring& name)
        : pipe_(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                 1, 4096, 4096, 0, nullptr)) {
        Require(pipe_ != INVALID_HANDLE_VALUE, "ipc test: creating the raw fixture pipe failed.");
    }
    RawServer(const RawServer&) = delete;
    RawServer& operator=(const RawServer&) = delete;
    ~RawServer() { Close(); }

    // Call only after the client connected; it then returns immediately.
    void AwaitClient() {
        Require(ConnectNamedPipe(pipe_, nullptr) != FALSE || GetLastError() == ERROR_PIPE_CONNECTED,
                "ipc test: the raw fixture did not observe the connected client.");
    }
    void Send(const std::vector<unsigned char>& bytes) {
        DWORD wrote = 0;
        Require(WriteFile(pipe_, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr) != FALSE &&
                    wrote == bytes.size(),
                "ipc test: the raw fixture could not send its frame bytes.");
    }
    // True when the other end has closed its handle.
    [[nodiscard]] bool PeerReleased() {
        const unsigned char probe = 0;
        DWORD wrote = 0;
        return WriteFile(pipe_, &probe, 1, &wrote, nullptr) == FALSE && IsGone(GetLastError());
    }
    void Close() noexcept {
        if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE pipe_;
};

[[nodiscard]] std::optional<PipeFailure> ClientReadFailure(AuthenticatedPipeClient& client,
                                                           HANDLE cancel = nullptr) {
    try {
        (void)client.ReadMessage(cancel);
    } catch (const PipeError& error) {
        return error.Reason();
    }
    return std::nullopt;
}

// Only ERROR_NO_TOKEN proves the thread has no token. Any other failure, such
// as ERROR_CANT_OPEN_ANONYMOUS for an anonymous-level residue, counts as one.
[[nodiscard]] bool ThreadIsImpersonating() {
    HANDLE token = nullptr;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) {
        CloseHandle(token);
        return true;
    }
    return GetLastError() != ERROR_NO_TOKEN;
}

// Kernel identity of the current thread token instance and its modification
// state. Held only for in-process comparison; never printed.
struct TokenStamp final {
    LUID token_id{};
    LUID modified_id{};
};

[[nodiscard]] bool SameLuid(const LUID& a, const LUID& b) noexcept {
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

[[nodiscard]] std::optional<TokenStamp> ThreadTokenStamp() {
    HANDLE token = nullptr;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) return std::nullopt;
    TOKEN_STATISTICS statistics{};
    DWORD returned = 0;
    const BOOL queried =
        GetTokenInformation(token, TokenStatistics, &statistics, sizeof(statistics), &returned);
    CloseHandle(token);
    if (!queried || returned != sizeof(statistics)) return std::nullopt;
    return TokenStamp{statistics.TokenId, statistics.ModifiedId};
}

[[nodiscard]] bool ReadMessageRefused(AuthenticatedPipeServer& server) {
    try {
        (void)server.ReadMessage();
    } catch (const PipeError& error) {
        return error.Reason() == PipeFailure::NotVerified;
    }
    return false;
}

// Trusted policy is obtained from the test's own kernel token, never a message.
[[nodiscard]] std::wstring CurrentLogonPolicy() {
    HANDLE token = nullptr;
    Require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE,
            "ipc test: opening the own token for logon policy failed.");
    struct CloseToken final { HANDLE value; ~CloseToken() { CloseHandle(value); } } close{token};
    DWORD needed = 0;
    (void)GetTokenInformation(token, TokenLogonSid, nullptr, 0, &needed);
    Require(GetLastError() == ERROR_INSUFFICIENT_BUFFER && needed >= sizeof(TOKEN_GROUPS) &&
            needed <= 65536U, "ipc test: querying the own logon policy size failed.");
    std::vector<unsigned char> buffer(needed);
    Require(GetTokenInformation(token, TokenLogonSid, buffer.data(), needed, &needed) != FALSE,
            "ipc test: querying the own logon policy failed.");
    TOKEN_GROUPS groups{};
    std::memcpy(&groups, buffer.data(), sizeof(groups));
    Require(groups.GroupCount == 1 && IsWellKnownSid(groups.Groups[0].Sid, WinLogonIdsSid),
            "ipc test: an interactive token lacked the own logon SID.");
    LPWSTR text = nullptr;
    Require(ConvertSidToStringSidW(groups.Groups[0].Sid, &text) != FALSE,
            "ipc test: formatting the own logon policy failed.");
    std::wstring result;
    try { result.assign(text); } catch (...) { LocalFree(text); throw; }
    LocalFree(text);
    return result;
}

// Scenario: before a successful AcceptAndVerify the data plane is closed; a read
// or a well-formed write is refused without touching the pipe.
void StateGateBeforeVerify() {
    const std::wstring name = MakeName(10);
    auto server = AuthenticatedPipeServer::Create(name);
    bool read_blocked = false;
    try {
        (void)server.ReadMessage();
    } catch (const PipeError& error) {
        read_blocked = error.Reason() == PipeFailure::NotVerified;
    }
    bool write_blocked = false;
    const unsigned char payload[] = {'a'};
    try {
        server.WriteMessage(payload, sizeof(payload));
    } catch (const PipeError& error) {
        write_blocked = error.Reason() == PipeFailure::NotVerified;
    }
    Require(read_blocked && write_blocked,
            "ipc test: the data plane was usable before a verified accept.");
}

// Scenario: argument bounds are enforced independently of connection state.
void ArgumentBoundsEnforced() {
    const std::wstring name = MakeName(11);
    PipeLimits limits;
    limits.max_message_bytes = 1000;
    auto server = AuthenticatedPipeServer::Create(name, limits);
    bool too_large = false;
    const std::vector<unsigned char> big(2000, static_cast<unsigned char>('x'));
    try {
        server.WriteMessage(big.data(), big.size());
    } catch (const PipeError& error) {
        too_large = error.Reason() == PipeFailure::MessageTooLarge;
    }
    bool null_rejected = false;
    try {
        server.WriteMessage(nullptr, 8);
    } catch (const PipeError& error) {
        null_rejected = error.Reason() == PipeFailure::InvalidArgument;
    }
    Require(too_large && null_rejected,
            "ipc test: oversize or null-with-size writes were not rejected by the bounds.");
}

// Scenario: the deadline has a finite ceiling; INFINITE is refused.
void InfiniteDeadlineRejected() {
    bool capped = false;
    PipeLimits limits;
    limits.deadline_ms = 0xFFFFFFFFU;
    try {
        auto server = AuthenticatedPipeServer::Create(MakeName(12), limits);
        (void)server;
    } catch (const PipeError& error) {
        capped = error.Reason() == PipeFailure::LimitsInvalid;
    }
    Require(capped, "ipc test: an infinite deadline was accepted.");
}

// Broad groups and embedded terminators cannot replace trusted per-logon policy.
void PolicyAndNameRejected() {
    unsigned char world[SECURITY_MAX_SID_SIZE] = {};
    DWORD world_bytes = sizeof(world);
    Require(CreateWellKnownSid(WinWorldSid, nullptr, world, &world_bytes) != FALSE,
            "ipc test: constructing the inert well-known SID failed.");
    LPWSTR text = nullptr;
    Require(ConvertSidToStringSidW(world, &text) != FALSE,
            "ipc test: formatting the inert well-known SID failed.");
    std::wstring broad;
    try { broad.assign(text); } catch (...) { LocalFree(text); throw; }
    LocalFree(text);
    bool policy_rejected = false;
    try {
        (void)AuthenticatedPipeServer::Create(MakeName(13), {}, broad);
    } catch (const PipeError& error) {
        policy_rejected = error.Reason() == PipeFailure::SecurityDescriptorFailed;
    }
    auto embedded = MakeName(14);
    embedded.push_back(L'\0');
    embedded += L"ignored-suffix";
    bool name_rejected = false;
    try {
        (void)AuthenticatedPipeServer::Create(embedded);
    } catch (const PipeError& error) {
        name_rejected = error.Reason() == PipeFailure::NameInvalid;
    }
    Require(policy_rejected && name_rejected,
            "ipc test: broad policy or ambiguous pipe name was accepted.");
}

// Scenario: names that would normalize out of the \\.\pipe\ namespace, or carry
// a separator or a "."/".." or trailing-dot/space component, are rejected by both
// Create and Connect BEFORE any I/O. Only fictitious names with the pipe prefix
// are used; none is ever opened (validation throws first).
void NamespaceEscapeNamesRejected() {
    const wchar_t* const names[] = {
        L"\\\\.\\pipe\\..\\ms-namespace-probe",
        L"\\\\.\\pipe\\../ms-namespace-probe",
        L"\\\\.\\pipe\\..",
        L"\\\\.\\pipe\\.",
        L"\\\\.\\pipe\\seat\\child",
        L"\\\\.\\pipe\\seat/child",
        L"\\\\.\\pipe\\trailing.",
        L"\\\\.\\pipe\\trailing ",
    };
    for (const wchar_t* const raw : names) {
        const std::wstring name = raw;
        bool create_rejected = false;
        try {
            auto server = AuthenticatedPipeServer::Create(name);
            (void)server;
        } catch (const PipeError& error) {
            create_rejected = error.Reason() == PipeFailure::NameInvalid;
        }
        bool connect_rejected = false;
        try {
            auto client = AuthenticatedPipeClient::Connect(name);
            (void)client;
        } catch (const PipeError& error) {
            connect_rejected = error.Reason() == PipeFailure::NameInvalid;
        }
        Require(create_rejected && connect_rejected,
                "ipc test: a namespace-escaping or multi-component pipe name was not rejected before I/O.");
    }
    // A legitimate single-component name still validates and opens.
    auto server = AuthenticatedPipeServer::Create(MakeName(30));
    (void)server;
}

// Reject an existing thread context without erasing, replacing or modifying
// it: the very same token instance (TokenId) must remain, unmodified
// (ModifiedId). Both are same-user here, so mere presence would prove nothing.
void ExistingImpersonationPreserved() {
    auto server = AuthenticatedPipeServer::Create(MakeName(15));
    auto client = AuthenticatedPipeClient::Connect(MakeName(15));
    Require(ImpersonateSelf(SecurityIdentification) != FALSE,
            "ipc test: establishing the own-thread test context failed.");
    // Reverts on every exit, including a failed Require below; a failed
    // restore must not let the test continue under the fixture context.
    struct Restore final {
        ~Restore() noexcept {
            if (!RevertToSelf()) {
                RaiseFailFastException(nullptr, nullptr, 0);
                std::terminate();
            }
        }
    } restore;
    const auto before = ThreadTokenStamp();
    Require(before.has_value(), "ipc test: reading the own-thread fixture token failed.");
    bool rejected = false;
    try {
        (void)server.AcceptAndVerify();
    } catch (const PipeError& error) {
        rejected = error.Reason() == PipeFailure::ThreadAlreadyImpersonating;
    }
    const auto after = ThreadTokenStamp();
    Require(rejected && after.has_value() && SameLuid(before->token_id, after->token_id) &&
                SameLuid(before->modified_id, after->modified_id) && ReadMessageRefused(server),
            "ipc test: an existing thread context was replaced, modified or lost.");
}

// Exercise cancellation with a real pending accept, not just an argument check.
// Cancellation is reported distinctly from a deadline expiry.
void AcceptCancelled() {
    auto server = AuthenticatedPipeServer::Create(MakeName(16));
    HANDLE cancel = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    if (cancel == nullptr) throw std::runtime_error("ipc test: creating the cancellation event failed.");
    bool cancelled = false;
    try {
        (void)server.AcceptAndVerify(cancel);
    } catch (const PipeError& error) {
        cancelled = error.Reason() == PipeFailure::Cancelled;
    } catch (...) {
        CloseHandle(cancel);
        throw;
    }
    CloseHandle(cancel);
    Require(cancelled && ReadMessageRefused(server),
            "ipc test: cancellation did not close the pending accept.");
}

// Accepts the spawned child. On a seat runner the peer must verify; otherwise
// only the exact independently observed identity category is acceptable, and
// any other identity or transport failure propagates as a test failure.
[[nodiscard]] std::optional<VerifiedIdentity> AcceptChild(AuthenticatedPipeServer& server, bool seat,
                                                          std::optional<IdentityFailure> non_seat) {
    std::optional<VerifiedIdentity> peer;
    try {
        peer.emplace(server.AcceptAndVerify());
    } catch (const IdentityError& error) {
        if (seat) throw; // A seat machine must verify its own-user peer.
        Require(non_seat.has_value() && error.Reason() == *non_seat,
                "ipc test: the non-seat peer rejection was not the exact observed category.");
    }
    Require(!ThreadIsImpersonating(),
            "ipc test: the server thread kept the client's impersonation context.");
    Require(seat == peer.has_value(),
            "ipc test: the accept outcome did not match the observed seat identity.");
    if (!seat) {
        Require(ReadMessageRefused(server), "ipc test: a failed accept did not fail-close the endpoint.");
    }
    return peer;
}

// Scenario: a real peer is authenticated from its process token, the declared
// payload is never an identity authority, impersonation is reverted, and a
// bounded request/reply round trip completes with a clean child exit. On a
// non-seat runner the exact identity category is required, the endpoint is
// fail-closed and the child must observe the closure instead of a reply.
void PositivePeerExchange(bool seat, std::optional<IdentityFailure> non_seat) {
    const std::wstring name = MakeName(1);
    auto server = AuthenticatedPipeServer::Create(name, {}, seat ? CurrentLogonPolicy() : L"");
    auto child = SpawnSelf(L"--ipc-client", 1);
    const auto peer = AcceptChild(server, seat, non_seat);

    if (seat) {
        const auto self = VerifiedIdentity::CurrentProcess();
        Require(peer->SameUser(self) && peer->SameLogon(self) && peer->SessionId() == self.SessionId(),
                "ipc test: the verified peer identity did not match the real own-user logon.");
        // The request payload is an inert untrusted claim, read only after the
        // identity above was fixed from the kernel peer.
        const auto request = server.ReadMessage();
        Require(!request.empty(), "ipc test: the client request frame was empty.");
        const unsigned char reply[] = {'o', 'k'};
        server.WriteMessage(reply, sizeof(reply));
        Require(child.Wait(15000) == kChildOk,
                "ipc test: the client child did not complete the exchange cleanly.");
    } else {
        Require(child.Wait(15000) == kChildSawClose,
                "ipc test: the client child did not observe the server's fail-close.");
    }
}

// Scenario: the client side of the shared framing fails closed on a stalled
// truncated frame, on a truncated frame followed by disconnect, and on a
// cancelled pending read. Each failure must also release the client handle, as
// observed from the raw server end. Runs on any runner.
void ClientFramingFailsClosed() {
    {
        const std::wstring name = MakeName(20);
        RawServer raw(name);
        PipeLimits limits;
        limits.deadline_ms = 300;
        auto client = AuthenticatedPipeClient::Connect(name, limits);
        raw.AwaitClient();
        raw.Send(TruncatedFrame());
        Require(ClientReadFailure(client) == PipeFailure::IoTimedOut,
                "ipc test: a stalled truncated frame did not expire as a timeout.");
        Require(ClientReadFailure(client) == PipeFailure::Disconnected && raw.PeerReleased(),
                "ipc test: a stalled frame did not fail-close the client endpoint.");
    }
    {
        const std::wstring name = MakeName(21);
        RawServer raw(name);
        auto client = AuthenticatedPipeClient::Connect(name);
        raw.AwaitClient();
        raw.Send(TruncatedFrame());
        raw.Close();
        const ULONGLONG started = GetTickCount64();
        Require(ClientReadFailure(client) == PipeFailure::Disconnected && GetTickCount64() - started < 5000,
                "ipc test: a truncated frame and disconnect was not reported promptly.");
        Require(ClientReadFailure(client) == PipeFailure::Disconnected,
                "ipc test: a disconnected endpoint accepted another read.");
    }
    {
        const std::wstring name = MakeName(22);
        RawServer raw(name);
        auto client = AuthenticatedPipeClient::Connect(name);
        raw.AwaitClient();
        HANDLE cancel = CreateEventW(nullptr, TRUE, TRUE, nullptr);
        if (cancel == nullptr) throw std::runtime_error("ipc test: creating the cancellation event failed.");
        const auto reason = ClientReadFailure(client, cancel);
        CloseHandle(cancel);
        Require(reason == PipeFailure::Cancelled && raw.PeerReleased(),
                "ipc test: a cancelled pending read did not fail-close the client endpoint.");
    }
}

// Scenario: a verified peer that sends a truncated frame and stalls makes the
// server read expire within its deadline, fail-close, and release the peer,
// which the child must observe. On a non-seat runner only the exact identity
// rejection and the child's observed closure are asserted.
void ServerStalledFrameFailsClosed(bool seat, std::optional<IdentityFailure> non_seat) {
    const std::wstring name = MakeName(5);
    PipeLimits limits;
    limits.deadline_ms = 3000;
    auto server = AuthenticatedPipeServer::Create(name, limits);
    auto child = SpawnSelf(L"--ipc-client-stall", 5);
    (void)AcceptChild(server, seat, non_seat);
    if (seat) {
        bool timed_out = false;
        try {
            (void)server.ReadMessage();
        } catch (const PipeError& error) {
            timed_out = error.Reason() == PipeFailure::IoTimedOut;
        }
        Require(timed_out && ReadMessageRefused(server),
                "ipc test: a stalled truncated frame did not time out and fail-close the server.");
    }
    Require(child.Wait(15000) == kChildSawClose,
            "ipc test: the stalled child did not observe the server's fail-close.");
}

// Scenario: a real own child that grants only anonymous impersonation is
// refused at the impersonation step, before any identity is derived, on any
// runner. The documented anonymous-level refusal of OpenThreadToken must be
// the observed cause; no impersonation may remain, the endpoint must be
// unusable and the child must observe the closure and exit cleanly. An
// IdentityError here would mean the identity step was reached, and propagates
// as a test failure.
void AnonymousPeerRejected() {
    const std::wstring name = MakeName(6);
    auto server = AuthenticatedPipeServer::Create(name);
    auto child = SpawnSelf(L"--ipc-client-anonymous", 6);
    std::optional<PipeFailure> reason;
    DWORD win32 = ERROR_SUCCESS;
    try {
        (void)server.AcceptAndVerify();
    } catch (const PipeError& error) {
        reason = error.Reason();
        win32 = error.Win32Error();
    }
    Require(reason == PipeFailure::ImpersonationFailed && win32 == ERROR_CANT_OPEN_ANONYMOUS,
            "ipc test: an anonymous-level peer was not refused at impersonation.");
    Require(!ThreadIsImpersonating(), "ipc test: an anonymous refusal left the thread impersonating.");
    Require(ReadMessageRefused(server), "ipc test: an anonymous refusal did not fail-close the endpoint.");
    Require(child.Wait(15000) == kChildSawClose,
            "ipc test: the anonymous child did not observe the server's fail-close.");
}

// Scenario: a configured logon policy that the child's token does not carry
// denies the open in the kernel (ERROR_ACCESS_DENIED), although the child is
// the creating user; nothing ever connects and the accept expires.
void ForeignLogonPolicyDenied() {
    const std::wstring name = MakeName(7);
    PipeLimits limits;
    limits.deadline_ms = 300;
    // Well-formed logon SID shape; the zero logon sequence is not the child's.
    auto server = AuthenticatedPipeServer::Create(name, limits, L"S-1-5-5-0-0");
    auto child = SpawnSelf(L"--ipc-client-denied", 7);
    Require(child.Wait(15000) == kChildDenied,
            "ipc test: a client outside the configured logon policy was not denied at open.");
    bool timed_out = false;
    try {
        (void)server.AcceptAndVerify();
    } catch (const PipeError& error) {
        timed_out = error.Reason() == PipeFailure::ConnectTimedOut;
    }
    Require(timed_out && ReadMessageRefused(server),
            "ipc test: a denied client still reached the policy-restricted endpoint.");
}

// Scenario: with no client, the accept deadline elapses and is reported.
void AcceptTimesOut() {
    const std::wstring name = MakeName(2);
    PipeLimits limits;
    limits.deadline_ms = 300;
    auto server = AuthenticatedPipeServer::Create(name, limits);
    bool timed_out = false;
    try {
        (void)server.AcceptAndVerify();
    } catch (const PipeError& error) {
        timed_out = error.Reason() == PipeFailure::ConnectTimedOut;
    }
    Require(timed_out, "ipc test: accept did not time out without a client.");
}

// Scenario: the pipe is single-instance; a second create on the same name is
// refused by the kernel.
void SecondInstanceRejected() {
    const std::wstring name = MakeName(3);
    auto first = AuthenticatedPipeServer::Create(name);
    bool rejected = false;
    try {
        auto second = AuthenticatedPipeServer::Create(name);
        (void)second;
    } catch (const PipeError& error) {
        rejected = error.Reason() == PipeFailure::CreateFailed;
    }
    Require(rejected, "ipc test: a second instance of the same pipe name was allowed.");
}

// Scenario: a client frame larger than the server's configured maximum is
// rejected on read without allocating the declared size, and the framing error
// fail-closes the endpoint. On a non-seat runner only the exact identity
// rejection is asserted.
void OversizeReadRejected(bool seat, std::optional<IdentityFailure> non_seat) {
    const std::wstring name = MakeName(4);
    PipeLimits limits;
    limits.max_message_bytes = 1000;
    auto server = AuthenticatedPipeServer::Create(name, limits);
    auto child = SpawnSelf(L"--ipc-client-big", 4);
    (void)AcceptChild(server, seat, non_seat);
    if (seat) {
        bool too_large = false;
        try {
            (void)server.ReadMessage();
        } catch (const PipeError& error) {
            too_large = error.Reason() == PipeFailure::MessageTooLarge;
        }
        Require(too_large, "ipc test: an oversize client frame was not rejected on read.");
        Require(ReadMessageRefused(server),
                "ipc test: a framing error did not fail-close the endpoint.");
    }
    Require(child.Wait(15000) == kChildSawClose,
            "ipc test: the oversize child did not observe the server's fail-close.");
}

// Every client mode stays alive until it observes the server's reply or its
// fail-close, so it cannot exit before the server has inspected the process.
[[nodiscard]] int RunClient(const std::wstring& name, bool oversize) {
    try {
        auto client = AuthenticatedPipeClient::Connect(name);
        if (oversize) {
            // The client's own maximum permits this; the server's smaller bound
            // must reject it and close the connection.
            const std::vector<unsigned char> payload(2000, static_cast<unsigned char>('x'));
            client.WriteMessage(payload.data(), payload.size());
            (void)client.ReadMessage();
            return kChildUnexpected;
        }
        // An inert untrusted claim with no literal identifier. The server must
        // ignore it entirely for identity purposes.
        const char claim[] = "untrusted-claim=ignored-by-server";
        client.WriteMessage(reinterpret_cast<const unsigned char*>(claim), std::strlen(claim));
        const auto reply = client.ReadMessage();
        return reply.size() == 2 && reply[0] == 'o' && reply[1] == 'k' ? kChildOk : kChildUnexpected;
    } catch (const PipeError& error) {
        return error.Reason() == PipeFailure::Disconnected ? kChildSawClose : kChildFailed;
    } catch (const std::exception&) {
        return kChildFailed;
    }
}

// The client must be refused by the kernel at open, not later by the server.
[[nodiscard]] int RunDeniedClient(const std::wstring& name) {
    try {
        auto client = AuthenticatedPipeClient::Connect(name);
        (void)client;
        return kChildUnexpected;
    } catch (const PipeError& error) {
        return error.Reason() == PipeFailure::ClientConnectFailed && error.Win32Error() == ERROR_ACCESS_DENIED
                   ? kChildDenied
                   : kChildFailed;
    } catch (const std::exception&) {
        return kChildFailed;
    }
}

// Raw client with an explicit impersonation level. It optionally sends a
// truncated frame, then blocks until the server closes the connection. The
// parent bounds its lifetime.
[[nodiscard]] int RunRawClient(const std::wstring& name, DWORD sqos_level, bool send_truncated) {
    const HANDLE pipe = CreateFileW(name.c_str(), multiseat::native::ipc::kClientPipeAccess, 0, nullptr,
                                    OPEN_EXISTING, SECURITY_SQOS_PRESENT | sqos_level, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return kChildFailed;
    int result = kChildFailed;
    bool sent = true;
    if (send_truncated) {
        const auto frame = TruncatedFrame();
        DWORD wrote = 0;
        sent = WriteFile(pipe, frame.data(), static_cast<DWORD>(frame.size()), &wrote, nullptr) != FALSE &&
               wrote == frame.size();
    }
    if (sent) {
        unsigned char byte = 0;
        DWORD got = 0;
        const BOOL read = ReadFile(pipe, &byte, 1, &got, nullptr);
        result = read == FALSE && IsGone(GetLastError()) ? kChildSawClose : kChildUnexpected;
    }
    CloseHandle(pipe);
    return result;
}

// Regression: a child invoked with a non-numeric identifier must refuse before
// any I/O — it never builds a name or opens an object. Inert tokens only.
void CliRejectsInvalidIdentifiers() {
    auto child = SpawnSelfTokens(L"--ipc-client", L"not-a-number", L"0");
    Require(child.Wait(10000) == static_cast<DWORD>(kChildBadArg),
            "ipc test: a child with a non-numeric identifier was not refused before I/O.");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    // Child invocation: argv[1] is a mode in the --ipc-client family, argv[2] is
    // the parent PID and argv[3] a numeric scenario. The pipe name is rebuilt here
    // from those bounded integers, and a missing or non-numeric identifier is
    // refused before any I/O, so no command-line string ever reaches CreateFile.
    if (argc >= 2 && std::wcsncmp(argv[1], L"--ipc-client", 12) == 0) {
        unsigned pid = 0;
        unsigned sequence = 0;
        if (argc < 4 || !ParseBoundedUint(argv[2], pid) || !ParseBoundedUint(argv[3], sequence)) {
            return kChildBadArg;
        }
        const std::wstring name = MakeNameFor(pid, sequence);
        if (std::wcscmp(argv[1], L"--ipc-client") == 0) return RunClient(name, false);
        if (std::wcscmp(argv[1], L"--ipc-client-big") == 0) return RunClient(name, true);
        if (std::wcscmp(argv[1], L"--ipc-client-stall") == 0) return RunRawClient(name, SECURITY_IDENTIFICATION, true);
        if (std::wcscmp(argv[1], L"--ipc-client-anonymous") == 0) return RunRawClient(name, SECURITY_ANONYMOUS, false);
        if (std::wcscmp(argv[1], L"--ipc-client-denied") == 0) return RunDeniedClient(name);
        return kChildBadArg;
    }
    try {
        const auto non_seat = NonSeatCategory();
        const bool seat = !non_seat.has_value();
        StateGateBeforeVerify();
        ArgumentBoundsEnforced();
        InfiniteDeadlineRejected();
        PolicyAndNameRejected();
        NamespaceEscapeNamesRejected();
        CliRejectsInvalidIdentifiers();
        ExistingImpersonationPreserved();
        Require(!ThreadIsImpersonating(), "ipc test: the own-thread fixture was not restored.");
        AcceptCancelled();
        AnonymousPeerRejected();
        ForeignLogonPolicyDenied();
        PositivePeerExchange(seat, non_seat);
        AcceptTimesOut();
        SecondInstanceRejected();
        OversizeReadRejected(seat, non_seat);
        ServerStalledFrameFailsClosed(seat, non_seat);
        ClientFramingFailsClosed();
        Require(!ThreadIsImpersonating(), "ipc test: a scenario left the thread impersonating.");
        if (seat) {
            std::puts("PASS: authenticated local IPC verified a real own-user peer from its process "
                      "token, ignored the declared payload, reverted impersonation and fail-closed "
                      "on oversize, stalled and truncated frames.");
        } else {
            std::puts("PASS: authenticated local IPC reported the exact non-seat identity category "
                      "and fail-closed; children observed the closure. The server data plane was "
                      "NOT exercised on this non-seat runner.");
        }
        std::puts("One-shot state, bounded size/finite deadline, cancellation, preserved prior thread "
                  "token, anonymous-peer refusal, foreign-logon DACL denial, client-side framing "
                  "fail-close, single instance and child exits held. No native terminal, session, "
                  "service or driver was created.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
