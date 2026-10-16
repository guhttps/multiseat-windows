using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Principal;

// Queries machine-wide COM descriptors and checks a service process token against them.
// Does not impersonate a service, attach or change descriptors. When required, enables
// SeDebug only on this diagnostic's token and restores its prior state in finally.
public static class ComSecurityDiagnostics
{
    [StructLayout(LayoutKind.Sequential)]
    private struct GenericMapping { public uint Read, Write, Execute, All; }
    [StructLayout(LayoutKind.Sequential)]
    private struct Luid { public uint Low; public int High; }
    [StructLayout(LayoutKind.Sequential)]
    private struct TokenPrivilege { public uint Count; public Luid Id; public uint Attributes; }
    public sealed class DescriptorResult
    {
        public int Type;
        public string Sddl;
        public string ProcessUserSid;
        public uint DesiredAccess = 3; // COM_RIGHTS_EXECUTE | COM_RIGHTS_EXECUTE_LOCAL
        public bool? AccessAllowed;
        public uint? GrantedAccess;
        public string TokenCheckError;
        public int? TokenCheckWin32Error;
        public string TokenCheckStatus = "not_requested";
        public bool DebugPrivilegeRequested;
        public bool DebugPrivilegeRestored = true;
    }
    [DllImport("combase.dll")]
    private static extern int CoGetSystemSecurityPermissions(int type, out IntPtr descriptor);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool ConvertSecurityDescriptorToStringSecurityDescriptorW(
        IntPtr descriptor, uint revision, uint information, out IntPtr text, out uint length);
    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr memory);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint access, bool inherit, uint processId);
    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);
    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool DuplicateToken(IntPtr token, int level, out IntPtr duplicate);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool LookupPrivilegeValueW(string system, string name, out Luid id);
    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll,
        ref TokenPrivilege state, uint size, out TokenPrivilege previous, out uint returned);
    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool AccessCheck(IntPtr descriptor, IntPtr token, uint desired,
        ref GenericMapping mapping, IntPtr privileges, ref uint length, out uint granted, out bool allowed);

    public static DescriptorResult Read(int type, uint serviceProcessId)
    {
        if (type < 0 || type > 3) throw new ArgumentOutOfRangeException("type");
        IntPtr descriptor = IntPtr.Zero, text = IntPtr.Zero;
        try
        {
            int result = CoGetSystemSecurityPermissions(type, out descriptor);
            if (result < 0) Marshal.ThrowExceptionForHR(result);
            if (descriptor == IntPtr.Zero) throw new InvalidOperationException("COM returned no descriptor.");
            uint length;
            if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor, 1, 7, out text, out length))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            var entry = new DescriptorResult { Type = type, Sddl = Marshal.PtrToStringUni(text) };
            if (serviceProcessId != 0 && (type == 1 || type == 3)) CheckToken(entry, descriptor, serviceProcessId);
            return entry;
        }
        finally
        {
            if (text != IntPtr.Zero) LocalFree(text);
            if (descriptor != IntPtr.Zero) LocalFree(descriptor);
        }
    }
    private static void CheckToken(DescriptorResult entry, IntPtr descriptor, uint processId)
    {
        entry.TokenCheckStatus = "unavailable";
        IntPtr process = IntPtr.Zero, token = IntPtr.Zero, duplicate = IntPtr.Zero, privileges = IntPtr.Zero;
        IntPtr ownToken = IntPtr.Zero;
        TokenPrivilege previous = new TokenPrivilege();
        bool privilegeChanged = false;
        try
        {
            process = OpenProcess(0x1000, false, processId); // QUERY_LIMITED_INFORMATION
            if (process == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "Process query failed.");
            if (!OpenProcessToken(process, 8, out token)) // QUERY first, no duplication needed for SID.
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Token query failed.");
            using (var identity = new WindowsIdentity(token)) { entry.ProcessUserSid = identity.User.Value; }
            CloseHandle(token);
            token = IntPtr.Zero;
            if (!OpenProcessToken(process, 0xa, out token)) // QUERY | DUPLICATE for AccessCheck.
            {
                int error = Marshal.GetLastWin32Error();
                if (error != 5) throw new Win32Exception(error, "Token duplication access unavailable.");
                // No rights assignment or persistent change; only use an existing privilege.
                entry.DebugPrivilegeRequested = true;
                if (!OpenProcessToken(new IntPtr(-1), 0x28, out ownToken))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "Own token query failed.");
                Luid debug;
                if (!LookupPrivilegeValueW(null, "SeDebugPrivilege", out debug))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "Privilege lookup failed.");
                var requested = new TokenPrivilege { Count = 1, Id = debug, Attributes = 2 };
                uint returned;
                bool adjusted = AdjustTokenPrivileges(ownToken, false, ref requested,
                    (uint)Marshal.SizeOf(typeof(TokenPrivilege)), out previous, out returned);
                int adjustmentError = Marshal.GetLastWin32Error();
                privilegeChanged = adjusted && previous.Count != 0;
                if (!adjusted || adjustmentError != 0)
                    throw new Win32Exception(adjustmentError, "Existing debug privilege unavailable.");
                if (!OpenProcessToken(process, 0xa, out token))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "Token duplication access unavailable after scoped privilege.");
            }
            if (!DuplicateToken(token, 1, out duplicate)) // SecurityIdentification, no impersonation
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Token duplication failed.");
            uint size = 1024;
            privileges = Marshal.AllocHGlobal((int)size);
            var mapping = new GenericMapping(); // COM masks are specific rights, not GENERIC_*.
            uint granted;
            bool allowed;
            bool success = AccessCheck(descriptor, duplicate, entry.DesiredAccess, ref mapping, privileges, ref size, out granted, out allowed);
            if (!success)
            {
                int error = Marshal.GetLastWin32Error();
                if (error != 122 || size > 65536) throw new Win32Exception(error, "AccessCheck failed.");
                Marshal.FreeHGlobal(privileges);
                privileges = IntPtr.Zero;
                privileges = Marshal.AllocHGlobal((int)size);
                if (!AccessCheck(descriptor, duplicate, entry.DesiredAccess, ref mapping, privileges, ref size, out granted, out allowed))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "AccessCheck failed.");
            }
            entry.AccessAllowed = allowed;
            entry.GrantedAccess = granted;
            entry.TokenCheckStatus = "checked";
        }
        catch (Exception exception)
        {
            Exception cause = exception.GetBaseException();
            entry.TokenCheckError = cause.Message;
            var native = cause as Win32Exception;
            if (native != null) entry.TokenCheckWin32Error = native.NativeErrorCode;
        }
        finally
        {
            if (privilegeChanged)
            {
                TokenPrivilege ignored;
                uint returned;
                entry.DebugPrivilegeRestored = AdjustTokenPrivileges(ownToken, false, ref previous,
                    (uint)Marshal.SizeOf(typeof(TokenPrivilege)), out ignored, out returned);
                entry.DebugPrivilegeRestored = entry.DebugPrivilegeRestored && Marshal.GetLastWin32Error() == 0;
            }
            if (ownToken != IntPtr.Zero) CloseHandle(ownToken);
            if (privileges != IntPtr.Zero) Marshal.FreeHGlobal(privileges);
            if (duplicate != IntPtr.Zero) CloseHandle(duplicate);
            if (token != IntPtr.Zero) CloseHandle(token);
            if (process != IntPtr.Zero) CloseHandle(process);
        }
    }
}
