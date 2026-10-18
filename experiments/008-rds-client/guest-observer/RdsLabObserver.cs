using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Management;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Security.Principal;
using System.Text;
using System.Threading;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

namespace Multiseat.RdsLab
{
    [DataContract]
    internal sealed class LabMarker
    {
        [DataMember(Name = "schemaVersion")] public int SchemaVersion { get; set; }
        [DataMember(Name = "purpose")] public string Purpose { get; set; }
        [DataMember(Name = "runId")] public string RunId { get; set; }
    }

    [DataContract]
    internal sealed class RectangleEvidence
    {
        [DataMember(Name = "left")] public int Left;
        [DataMember(Name = "top")] public int Top;
        [DataMember(Name = "right")] public int Right;
        [DataMember(Name = "bottom")] public int Bottom;
    }

    [DataContract]
    internal sealed class Evidence
    {
        [DataMember(Name = "schema_version")] public int SchemaVersion = 1;
        [DataMember(Name = "run_id")] public string RunId;
        [DataMember(Name = "seat")] public string Seat;
        [DataMember(Name = "observed_utc")] public string ObservedUtc;
        [DataMember(Name = "observer_pid")] public int ObserverPid;
        [DataMember(Name = "session_id")] public int SessionId;
        [DataMember(Name = "token_sid")] public string TokenSid;
        [DataMember(Name = "notepad_pid")] public int NotepadPid;
        [DataMember(Name = "notepad_session_id")] public int NotepadSessionId;
        [DataMember(Name = "notepad_token_sid")] public string NotepadTokenSid;
        [DataMember(Name = "owner_validated")] public bool OwnerValidated;
        [DataMember(Name = "window_validated")] public bool WindowValidated;
        [DataMember(Name = "edit_validated")] public bool EditValidated;
        [DataMember(Name = "foreground_is_notepad")] public bool ForegroundIsNotepad;
        [DataMember(Name = "dpi_aware")] public bool DpiAware;
        [DataMember(Name = "text_query_succeeded")] public bool TextQuerySucceeded;
        [DataMember(Name = "exact_fixture_now")] public bool ExactFixtureNow;
        [DataMember(Name = "exact_fixture_seen")] public bool ExactFixtureSeen;
        [DataMember(Name = "other_fixture_now")] public bool OtherFixtureNow;
        [DataMember(Name = "other_fixture_seen")] public bool OtherFixtureSeen;
        [DataMember(Name = "notepad_exited")] public bool NotepadExited;
        [DataMember(Name = "observer_finished")] public bool ObserverFinished;
        [DataMember(Name = "window_rect")] public RectangleEvidence WindowRect;
        [DataMember(Name = "edit_rect")] public RectangleEvidence EditRect;
        [DataMember(Name = "error_code")] public string ErrorCode;
    }

    internal static class Native
    {
        internal const uint QueryProcess = 0x1000;
        internal const uint Synchronize = 0x00100000;
        internal const uint TokenQuery = 0x0008;
        internal const uint WaitTimeout = 258;
        internal const uint MessageFlags = 0x0002 | 0x0020; // ABORTIFHUNG | ERRORONEXIT
        internal const uint MessageTimeout = 250;
        internal delegate bool EnumWindowsCallback(IntPtr window, IntPtr parameter);

        [StructLayout(LayoutKind.Sequential)]
        internal struct Rect { internal int Left, Top, Right, Bottom; }

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern SafeProcessHandle OpenProcess(uint access, bool inherit, uint processId);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern uint GetProcessId(SafeProcessHandle process);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern uint WaitForSingleObject(SafeProcessHandle process, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool ProcessIdToSessionId(uint processId, out uint sessionId);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern bool QueryFullProcessImageName(SafeProcessHandle process, int flags, StringBuilder name, ref int size);
        [DllImport("advapi32.dll", SetLastError = true)]
        internal static extern bool OpenProcessToken(SafeProcessHandle process, uint access, out SafeAccessTokenHandle token);
        [DllImport("user32.dll")]
        internal static extern bool SetProcessDpiAwarenessContext(IntPtr context);
        [DllImport("user32.dll")]
        internal static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        internal static extern int GetClassName(IntPtr window, StringBuilder name, int capacity);
        [DllImport("user32.dll")]
        internal static extern bool EnumChildWindows(IntPtr parent, EnumWindowsCallback callback, IntPtr parameter);
        [DllImport("user32.dll")]
        internal static extern bool IsWindowVisible(IntPtr window);
        [DllImport("user32.dll")]
        internal static extern bool IsWindowEnabled(IntPtr window);
        [DllImport("user32.dll")]
        internal static extern bool GetWindowRect(IntPtr window, out Rect rectangle);
        [DllImport("user32.dll")]
        internal static extern bool ShowWindow(IntPtr window, int command);
        [DllImport("user32.dll")]
        internal static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
        [DllImport("user32.dll")]
        internal static extern bool SetForegroundWindow(IntPtr window);
        [DllImport("user32.dll")]
        internal static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")]
        internal static extern int GetSystemMetrics(int index);
        [DllImport("user32.dll", EntryPoint = "SendMessageTimeoutW", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern IntPtr GetTextLength(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam,
            uint flags, uint timeout, out UIntPtr result);
        [DllImport("user32.dll", EntryPoint = "SendMessageTimeoutW", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern IntPtr GetText(IntPtr window, uint message, UIntPtr capacity, StringBuilder text,
            uint flags, uint timeout, out UIntPtr result);
    }

    internal static class Program
    {
        private const string Purpose = "MultiseatRdsLab.RdsAuxiliary";
        private const string FixtureA = "rdp-seat-a";
        private const string FixtureB = "rdp-seat-b";
        private const string MarkerPath = @"C:\ProgramData\MultiseatRdsLab\lab-marker.json";

        [STAThread]
        private static int Main(string[] arguments)
        {
            if (arguments.Length == 1 && arguments[0] == "--self-test")
            {
                try { return SelfTest() ? 0 : 1; } catch { return 1; }
            }
            string stage = "arguments";
            Evidence evidence = null;
            string reportPath = null;
            Mutex instance = null;
            try
            {
                Guid run;
                if (arguments.Length != 2 || arguments[0] != "--run-id" ||
                    !Guid.TryParseExact(arguments[1], "D", out run) || run == Guid.Empty)
                    throw new InvalidOperationException();

                stage = "guest_guard";
                string seat;
                string sid;
                ValidateGuest(out seat, out sid);
                stage = "marker_guard";
                ValidateMarker(run);

                // A local mutex prevents duplicate observers in one interactive
                // session; another account/session has a separate namespace.
                bool created;
                instance = new Mutex(true, @"Local\MultiseatRdsLabObserver_" + run.ToString("N"), out created);
                if (!created) return 0;
                stage = "report_directory";
                int session = Process.GetCurrentProcess().SessionId;
                reportPath = PrepareReportPath(run);
                evidence = new Evidence {
                    RunId = run.ToString("D"), Seat = seat, ObserverPid = Process.GetCurrentProcess().Id,
                    SessionId = session, TokenSid = sid, NotepadSessionId = -1, NotepadTokenSid = String.Empty
                };
                stage = "dpi_context";
                if (!Native.SetProcessDpiAwarenessContext(new IntPtr(-4))) throw new InvalidOperationException();
                evidence.DpiAware = true;
                SaveEvidence(reportPath, evidence);

                stage = "notepad_start";
                string notepadPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "notepad.exe");
                RejectReparsePoints(notepadPath);
                ProcessStartInfo start = new ProcessStartInfo(notepadPath);
                start.UseShellExecute = false;
                start.WorkingDirectory = Path.GetDirectoryName(reportPath);
                start.WindowStyle = ProcessWindowStyle.Normal;
                using (Process notepad = Process.Start(start))
                {
                    if (notepad == null) throw new InvalidOperationException();
                    evidence.NotepadPid = notepad.Id;
                    stage = "notepad_owner";
                    using (SafeProcessHandle handle = Native.OpenProcess(Native.QueryProcess | Native.Synchronize, false, (uint)notepad.Id))
                    {
                        ValidateNotepadOwner(handle, notepadPath, evidence);
                        SaveEvidence(reportPath, evidence);
                        stage = "notepad_window";
                        IntPtr window = WaitForOwnWindow(notepad, handle);
                        if (window == IntPtr.Zero) throw new InvalidOperationException();
                        evidence.WindowValidated = true;
                        ShowOwnWindow(window);
                        IntPtr edit = FindOwnEdit(window, (uint)notepad.Id);
                        if (edit == IntPtr.Zero) throw new InvalidOperationException();
                        evidence.EditValidated = true;
                        stage = "observe";
                        Stopwatch lifetime = Stopwatch.StartNew();
                        Stopwatch sinceWrite = Stopwatch.StartNew();
                        while (lifetime.Elapsed < TimeSpan.FromHours(2))
                        {
                            if (Native.WaitForSingleObject(handle, 0) != Native.WaitTimeout)
                            {
                                evidence.NotepadExited = true;
                                break;
                            }
                            // Validate the retained process and exact HWND owners
                            // before every message. Never discover another app.
                            if (!OwnedWindow(window, (uint)notepad.Id, "Notepad") ||
                                !OwnedWindow(edit, (uint)notepad.Id, "Edit"))
                                throw new InvalidOperationException();
                            bool previousOwn = evidence.ExactFixtureNow;
                            bool previousOther = evidence.OtherFixtureNow;
                            ObserveExactFixture(edit, seat, evidence);
                            uint foregroundPid;
                            evidence.ForegroundIsNotepad = Native.GetWindowThreadProcessId(Native.GetForegroundWindow(), out foregroundPid) != 0 &&
                                foregroundPid == (uint)notepad.Id;
                            evidence.WindowRect = ReadRectangle(window);
                            evidence.EditRect = ReadRectangle(edit);
                            if (sinceWrite.ElapsedMilliseconds >= 1000 || previousOwn != evidence.ExactFixtureNow || previousOther != evidence.OtherFixtureNow)
                            {
                                SaveEvidence(reportPath, evidence);
                                sinceWrite.Restart();
                            }
                            Thread.Sleep(100);
                        }
                    }
                }
                evidence.ObserverFinished = true;
                SaveEvidence(reportPath, evidence);
                return 0;
            }
            catch
            {
                if (evidence != null && reportPath != null)
                {
                    evidence.ErrorCode = "RDS_OBSERVER_FAILED:" + stage;
                    evidence.ObserverFinished = true;
                    try { SaveEvidence(reportPath, evidence); } catch { }
                }
                // No exception message, account name, text or machine path is emitted.
                return 1;
            }
            finally
            {
                if (instance != null) instance.Dispose();
            }
        }

        private static void ValidateGuest(out string seat, out string sid)
        {
            seat = null;
            sid = null;
            if (!Environment.Is64BitProcess || Process.GetCurrentProcess().SessionId <= 0) throw new InvalidOperationException();
            using (WindowsIdentity identity = WindowsIdentity.GetCurrent())
            {
                if (identity.User == null || new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator))
                    throw new InvalidOperationException();
                if (String.Equals(identity.Name, Environment.MachineName + @"\LabSeatA", StringComparison.OrdinalIgnoreCase)) seat = "A";
                else if (String.Equals(identity.Name, Environment.MachineName + @"\LabSeatB", StringComparison.OrdinalIgnoreCase)) seat = "B";
                else throw new InvalidOperationException();
                sid = identity.User.Value;
            }
            using (RegistryKey current = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows NT\CurrentVersion", false))
            using (RegistryKey hyperv = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Virtual Machine\Guest\Parameters", false))
            {
                if (current == null || hyperv == null || !String.Equals(current.GetValue("InstallationType") as string, "Server", StringComparison.Ordinal))
                    throw new InvalidOperationException();
            }
            using (ManagementObjectSearcher search = new ManagementObjectSearcher("SELECT ProductType, BuildNumber, Caption FROM Win32_OperatingSystem"))
            using (ManagementObjectCollection rows = search.Get())
            {
                if (rows.Count != 1) throw new InvalidOperationException();
                foreach (ManagementObject os in rows)
                    if (Convert.ToInt32(os["ProductType"], CultureInfo.InvariantCulture) != 3 ||
                        Convert.ToInt32(os["BuildNumber"], CultureInfo.InvariantCulture) < 26100 ||
                        Convert.ToString(os["Caption"], CultureInfo.InvariantCulture).IndexOf("Windows Server 2025", StringComparison.OrdinalIgnoreCase) < 0)
                        throw new InvalidOperationException();
            }
            using (ManagementObjectSearcher search = new ManagementObjectSearcher("SELECT Manufacturer, Model, PartOfDomain FROM Win32_ComputerSystem"))
            using (ManagementObjectCollection rows = search.Get())
            {
                if (rows.Count != 1) throw new InvalidOperationException();
                foreach (ManagementObject computer in rows)
                    if (!String.Equals(computer["Manufacturer"] as string, "Microsoft Corporation", StringComparison.Ordinal) ||
                        !String.Equals(computer["Model"] as string, "Virtual Machine", StringComparison.Ordinal) || Convert.ToBoolean(computer["PartOfDomain"], CultureInfo.InvariantCulture))
                        throw new InvalidOperationException();
            }
        }

        private static void ValidateMarker(Guid run)
        {
            RejectReparsePoints(MarkerPath);
            if (new FileInfo(MarkerPath).Length > 4096) throw new InvalidOperationException();
            using (FileStream file = new FileStream(MarkerPath, FileMode.Open, FileAccess.Read, FileShare.Read))
                if (!MarkerMatches(file, run)) throw new InvalidOperationException();
        }

        private static bool MarkerMatches(Stream data, Guid run)
        {
            LabMarker marker = (LabMarker)new DataContractJsonSerializer(typeof(LabMarker)).ReadObject(data);
            Guid markerRun;
            return marker != null && marker.SchemaVersion == 1 && marker.Purpose == Purpose &&
                Guid.TryParseExact(marker.RunId, "D", out markerRun) && markerRun == run && run != Guid.Empty;
        }

        private static string PrepareReportPath(Guid run)
        {
            string basePath = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            if (String.IsNullOrEmpty(basePath)) throw new InvalidOperationException();
            string directory = Path.Combine(basePath, "MultiseatRdsLab", run.ToString("D"));
            RejectReparsePoints(directory);
            Directory.CreateDirectory(directory);
            RejectReparsePoints(directory);
            return Path.Combine(directory, "observer.json");
        }

        private static void RejectReparsePoints(string path)
        {
            string current = Path.GetFullPath(path);
            while (!String.IsNullOrEmpty(current))
            {
                if ((File.Exists(current) || Directory.Exists(current)) &&
                    (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) throw new InvalidOperationException();
                current = Path.GetDirectoryName(current);
            }
        }

        private static void ValidateNotepadOwner(SafeProcessHandle handle, string expectedPath, Evidence evidence)
        {
            if (handle == null || handle.IsInvalid || Native.GetProcessId(handle) != (uint)evidence.NotepadPid ||
                Native.WaitForSingleObject(handle, 0) != Native.WaitTimeout) throw new InvalidOperationException();
            StringBuilder image = new StringBuilder(32768);
            int size = image.Capacity;
            if (!Native.QueryFullProcessImageName(handle, 0, image, ref size) ||
                !String.Equals(Path.GetFullPath(image.ToString()), expectedPath, StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException();
            uint session;
            if (!Native.ProcessIdToSessionId((uint)evidence.NotepadPid, out session) || session != (uint)evidence.SessionId)
                throw new InvalidOperationException();
            SafeAccessTokenHandle token;
            if (!Native.OpenProcessToken(handle, Native.TokenQuery, out token)) throw new InvalidOperationException();
            using (token)
            using (WindowsIdentity identity = new WindowsIdentity(token.DangerousGetHandle()))
            {
                if (identity.User == null || !String.Equals(identity.User.Value, evidence.TokenSid, StringComparison.Ordinal))
                    throw new InvalidOperationException();
                evidence.NotepadTokenSid = identity.User.Value;
            }
            evidence.NotepadSessionId = (int)session;
            evidence.OwnerValidated = true;
        }

        private static bool OwnedWindow(IntPtr window, uint processId, string expectedClass)
        {
            uint owner;
            if (window == IntPtr.Zero || Native.GetWindowThreadProcessId(window, out owner) == 0 || owner != processId)
                return false;
            StringBuilder name = new StringBuilder(64);
            return Native.GetClassName(window, name, name.Capacity) > 0 && name.ToString() == expectedClass;
        }

        private static IntPtr WaitForOwnWindow(Process notepad, SafeProcessHandle handle)
        {
            Stopwatch wait = Stopwatch.StartNew();
            while (wait.ElapsedMilliseconds < 20000 && Native.WaitForSingleObject(handle, 0) == Native.WaitTimeout)
            {
                notepad.Refresh();
                IntPtr window = notepad.MainWindowHandle;
                if (OwnedWindow(window, (uint)notepad.Id, "Notepad")) return window;
                Thread.Sleep(100);
            }
            return IntPtr.Zero;
        }

        private static IntPtr FindOwnEdit(IntPtr window, uint processId)
        {
            IntPtr edit = IntPtr.Zero;
            int count = 0;
            Native.EnumWindowsCallback callback = delegate(IntPtr child, IntPtr ignored)
            {
                if (OwnedWindow(child, processId, "Edit") && Native.IsWindowVisible(child) && Native.IsWindowEnabled(child))
                {
                    edit = child;
                    count++;
                }
                return true;
            };
            Native.EnumChildWindows(window, callback, IntPtr.Zero);
            GC.KeepAlive(callback);
            return count == 1 ? edit : IntPtr.Zero;
        }

        private static void ShowOwnWindow(IntPtr window)
        {
            int width = Math.Min(720, Native.GetSystemMetrics(0) - 96);
            int height = Math.Min(500, Native.GetSystemMetrics(1) - 96);
            if (width < 300 || height < 200) throw new InvalidOperationException();
            Native.ShowWindow(window, 9); // SW_RESTORE
            if (!Native.SetWindowPos(window, IntPtr.Zero, 48, 48, width, height, 0x0040)) throw new InvalidOperationException();
            Native.SetForegroundWindow(window); // The subsequent RDP click can also establish focus.
        }

        private static RectangleEvidence ReadRectangle(IntPtr window)
        {
            Native.Rect rect;
            if (!Native.GetWindowRect(window, out rect) || rect.Right <= rect.Left || rect.Bottom <= rect.Top)
                throw new InvalidOperationException();
            return new RectangleEvidence { Left = rect.Left, Top = rect.Top, Right = rect.Right, Bottom = rect.Bottom };
        }

        private static bool IsExactFixture(string text, string seat)
        {
            return String.Equals(text, seat == "A" ? FixtureA : FixtureB, StringComparison.Ordinal);
        }

        private static void ObserveExactFixture(IntPtr edit, string seat, Evidence evidence)
        {
            evidence.TextQuerySucceeded = false;
            evidence.ExactFixtureNow = false;
            evidence.OtherFixtureNow = false;
            UIntPtr result;
            if (Native.GetTextLength(edit, 0x000E, UIntPtr.Zero, IntPtr.Zero, Native.MessageFlags, Native.MessageTimeout, out result) == IntPtr.Zero)
                return;
            evidence.TextQuerySucceeded = true;
            // Never retrieve an arbitrary document or an unbounded string. Both
            // fixed fixtures have exactly ten UTF-16 characters. No text is logged.
            if (result.ToUInt64() != (ulong)FixtureA.Length) return;
            StringBuilder text = new StringBuilder(FixtureA.Length + 1);
            if (Native.GetText(edit, 0x000D, new UIntPtr((uint)text.Capacity), text, Native.MessageFlags, Native.MessageTimeout, out result) == IntPtr.Zero)
            {
                evidence.TextQuerySucceeded = false;
                return;
            }
            // Recheck length to reject a truncated prefix if input changed in flight.
            UIntPtr lengthAfter;
            if (Native.GetTextLength(edit, 0x000E, UIntPtr.Zero, IntPtr.Zero, Native.MessageFlags, Native.MessageTimeout, out lengthAfter) == IntPtr.Zero ||
                result.ToUInt64() != (ulong)FixtureA.Length || lengthAfter.ToUInt64() != (ulong)FixtureA.Length)
            {
                evidence.TextQuerySucceeded = false;
                text.Clear();
                return;
            }
            string value = text.ToString();
            evidence.ExactFixtureNow = IsExactFixture(value, seat);
            evidence.OtherFixtureNow = IsExactFixture(value, seat == "A" ? "B" : "A");
            evidence.ExactFixtureSeen |= evidence.ExactFixtureNow;
            evidence.OtherFixtureSeen |= evidence.OtherFixtureNow;
            text.Clear();
        }

        private static byte[] SerializeEvidence(Evidence evidence)
        {
            using (MemoryStream memory = new MemoryStream())
            {
                new DataContractJsonSerializer(typeof(Evidence)).WriteObject(memory, evidence);
                return memory.ToArray();
            }
        }

        private static void SaveEvidence(string path, Evidence evidence)
        {
            RejectReparsePoints(path);
            evidence.ObservedUtc = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture);
            byte[] bytes = SerializeEvidence(evidence);
            string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try
            {
                using (FileStream file = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    file.Write(bytes, 0, bytes.Length);
                    file.Flush(true);
                }
                for (int attempt = 0; ; attempt++)
                {
                    try
                    {
                        if (File.Exists(path)) File.Replace(temporary, path, null);
                        else File.Move(temporary, path);
                        break;
                    }
                    catch (IOException)
                    {
                        // A read-only collector may briefly hold the old file
                        // without delete sharing. Never publish partial JSON.
                        if (attempt >= 9) throw;
                        Thread.Sleep(25);
                    }
                }
            }
            finally { if (File.Exists(temporary)) File.Delete(temporary); }
        }

        private static bool SelfTest()
        {
            if (!IsExactFixture(FixtureA, "A") || !IsExactFixture(FixtureB, "B") ||
                IsExactFixture(FixtureB, "A") || IsExactFixture(FixtureA, "B") ||
                IsExactFixture("RDP-SEAT-A", "A") || IsExactFixture(FixtureA + "\r\n", "A") ||
                IsExactFixture(FixtureA + "private", "A") || IsExactFixture(String.Empty, "B")) return false;
            Guid run = Guid.NewGuid();
            string valid = "{\"schemaVersion\":1,\"purpose\":\"" + Purpose + "\",\"runId\":\"" + run.ToString("D") + "\"}";
            using (MemoryStream data = new MemoryStream(Encoding.UTF8.GetBytes(valid)))
                if (!MarkerMatches(data, run)) return false;
            using (MemoryStream data = new MemoryStream(Encoding.UTF8.GetBytes(valid)))
                if (MarkerMatches(data, Guid.NewGuid())) return false;
            using (MemoryStream data = new MemoryStream(Encoding.UTF8.GetBytes(valid.Replace(Purpose, "other"))))
                if (MarkerMatches(data, run)) return false;
            Evidence fixture = new Evidence { RunId = run.ToString("D"), Seat = "A", TokenSid = "synthetic-token",
                NotepadTokenSid = "synthetic-token", SessionId = 7, NotepadSessionId = 7, OwnerValidated = true,
                WindowValidated = true, EditValidated = true, ExactFixtureNow = true, ExactFixtureSeen = true };
            string json = Encoding.UTF8.GetString(SerializeEvidence(fixture));
            if (json.Contains(FixtureA) || json.Contains(FixtureB) || json.Contains("private") || json.Contains("clipboard")) return false;
            using (MemoryStream memory = new MemoryStream(Encoding.UTF8.GetBytes(json)))
            {
                Evidence copy = (Evidence)new DataContractJsonSerializer(typeof(Evidence)).ReadObject(memory);
                return copy.OwnerValidated && copy.ExactFixtureNow && !copy.OtherFixtureSeen && copy.SessionId == copy.NotepadSessionId;
            }
        }
    }
}
