using System;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Windows.Forms;
using System.Collections.Generic;
using System.Runtime.InteropServices.ComTypes;
using System.Diagnostics;
using System.Threading;

internal static class Native {
    [DllImport("oleaut32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    internal static extern void LoadTypeLibEx(string path, int registration, out ITypeLib library);
    [DllImport("wtsapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool WTSIsChildSessionsEnabled(out bool enabled);
    [DllImport("wtsapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool WTSEnableChildSessions([MarshalAs(UnmanagedType.Bool)] bool enabled);
    [DllImport("wtsapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool WTSGetChildSessionId(out uint id);
    [DllImport("wtsapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool WTSLogoffSession(IntPtr server, uint id,
        [MarshalAs(UnmanagedType.Bool)] bool wait);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool ProcessIdToSessionId(uint process, out uint session);
}

internal sealed class RdpEventLog : IDisposable {
    private readonly object control;
    private readonly Guid eventId = new Guid("336d5562-efa8-482e-8cb3-c5c0fc7a7db6");
    private readonly List<KeyValuePair<int, Delegate>> subscriptions = new List<KeyValuePair<int, Delegate>>();
    internal bool Ended;
    internal bool LoginComplete;
    internal int? DisconnectReason;
    internal RdpEventLog(object controlObject) {
        control = controlObject;
        ITypeLib library;
        Native.LoadTypeLibEx(System.IO.Path.Combine(Environment.SystemDirectory, "mstscax.dll"), 2, out library);
        ITypeInfo info = null;
        try {
            Guid id = eventId;
            library.GetTypeInfoOfGuid(ref id, out info);
            Bind(info, "OnConnected", new Action(delegate { Console.WriteLine("RDP event: OnConnected"); }));
            Bind(info, "OnLoginComplete", new Action(delegate {
                LoginComplete = true; Console.WriteLine("RDP event: OnLoginComplete");
            }));
            Bind(info, "OnDisconnected", new Action<int>(delegate(int reason) {
                DisconnectReason = reason; Ended = true;
                Console.WriteLine("RDP event: OnDisconnected reason=" + reason);
            }));
            Bind(info, "OnFatalError", new Action<int>(delegate(int code) {
                Ended = true; Console.WriteLine("RDP event: OnFatalError code=" + code);
            }));
            Bind(info, "OnLogonError", new Action<int>(delegate(int code) {
                Console.WriteLine("RDP event: OnLogonError code=" + code);
            }));
            Console.WriteLine("RDP event handlers registered: " + subscriptions.Count);
        } catch { Dispose(); throw; }
        finally {
            if (info != null) Marshal.ReleaseComObject(info);
            Marshal.ReleaseComObject(library);
        }
    }
    private void Bind(ITypeInfo info, string name, Delegate callback) {
        int[] ids = new int[1];
        info.GetIDsOfNames(new string[] { name }, 1, ids);
        ComEventsHelper.Combine(control, eventId, ids[0], callback);
        subscriptions.Add(new KeyValuePair<int, Delegate>(ids[0], callback));
    }
    public void Dispose() {
        Exception failure = null;
        foreach (KeyValuePair<int, Delegate> subscription in subscriptions) {
            try { ComEventsHelper.Remove(control, eventId, subscription.Key, subscription.Value); }
            catch (Exception error) { if (failure == null) failure = error; }
        }
        subscriptions.Clear();
        if (failure != null) throw new InvalidOperationException("RDP event handler cleanup failed.", failure);
    }
}

// A session ID alone can appear during a connection that subsequently fails.
// Require the same live child, a completed login and a connected control for
// three continuous seconds before reporting this limited experiment as passed.
internal sealed class ChildObservation {
    private readonly uint parent;
    private long? stableSince;
    internal uint ChildId = UInt32.MaxValue;
    internal ChildObservation(uint parentId) { parent = parentId; }
    internal bool Observe(bool found, uint child, short connection, bool loggedIn, bool ended, long elapsed) {
        bool distinct = found && child != 0 && child != UInt32.MaxValue && child != parent;
        if (distinct) {
            if (ChildId == UInt32.MaxValue) ChildId = child;
            else if (ChildId != child) throw new InvalidOperationException("Child identity changed; aborting.");
        }
        if (!distinct || connection != 1 || !loggedIn || ended) {
            stableSince = null;
            return false;
        }
        if (!stableSince.HasValue) stableSince = elapsed;
        return elapsed - stableSince.Value >= 3000;
    }
}

internal enum ChildCleanupAction { Refuse, AlreadyAbsent, LogoffOwned }

internal static class ChildCleanupPolicy {
    // ERROR_NOT_FOUND is the documented/observed absence baseline used by this
    // experiment. Access denied and all other query failures are not absence.
    internal static ChildCleanupAction Decide(uint parent, uint owned, bool found, uint latest, int queryError) {
        if (owned == 0 || owned == UInt32.MaxValue || owned == parent) return ChildCleanupAction.Refuse;
        if ((found && latest == UInt32.MaxValue) || (!found && queryError == 1168))
            return ChildCleanupAction.AlreadyAbsent;
        if (found && latest == owned) return ChildCleanupAction.LogoffOwned;
        return ChildCleanupAction.Refuse;
    }
}

[ComImport, Guid("302D8188-0052-4807-806A-362B628F9AC5"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IExtendedSettings {
    [PreserveSig] int Put([MarshalAs(UnmanagedType.BStr)] string name,
        [In, MarshalAs(UnmanagedType.Struct)] ref object value);
    [PreserveSig] int Get([MarshalAs(UnmanagedType.BStr)] string name,
        [Out, MarshalAs(UnmanagedType.Struct)] out object value);
}

internal sealed class RdpHost : AxHost {
    internal RdpHost() : base("A0C63C30-F08D-4AB4-907C-34905D770C7D") {}
    internal object ControlObject { get { return GetOcx(); } }
}

internal sealed class TestWindow : Form {
    private readonly bool smoke;
    internal TestWindow(bool smokeMode) {
        smoke = smokeMode;
        if (smoke) { Opacity = 0; ShowInTaskbar = false; }
    }
    protected override bool ShowWithoutActivation { get { return smoke; } }
    protected override CreateParams CreateParams {
        get {
            CreateParams parameters = base.CreateParams;
            if (smoke) parameters.ExStyle |= 0x08000000; // WS_EX_NOACTIVATE
            return parameters;
        }
    }
}

internal static class Program {
    private static bool SelfTest() {
        ChildObservation test = new ChildObservation(1);
        if (test.Observe(true, 2, 2, false, false, 0) ||
            test.Observe(true, 2, 1, false, false, 3000) ||
            test.Observe(true, 2, 1, true, false, 4000) ||
            test.Observe(true, 2, 1, true, false, 6999) ||
            !test.Observe(true, 2, 1, true, false, 7000)) return false;
        if (test.Observe(false, UInt32.MaxValue, 1, true, false, 8000) ||
            test.Observe(true, 2, 1, true, false, 9000) ||
            test.Observe(true, 2, 1, true, false, 11999) ||
            !test.Observe(true, 2, 1, true, false, 12000) ||
            test.Observe(true, 2, 1, true, true, 13000)) return false;
        ChildObservation invalid = new ChildObservation(1);
        if (invalid.Observe(true, 1, 1, true, false, 0) ||
            invalid.Observe(true, 0, 1, true, false, 3000) ||
            invalid.Observe(true, UInt32.MaxValue, 1, true, false, 4000)) return false;
        ChildObservation interrupted = new ChildObservation(1);
        if (interrupted.Observe(true, 2, 1, true, false, 0) ||
            interrupted.Observe(true, 2, 2, true, false, 2999) ||
            interrupted.Observe(true, 2, 1, true, false, 3000) ||
            interrupted.Observe(true, 2, 1, true, false, 5999) ||
            !interrupted.Observe(true, 2, 1, true, false, 6000)) return false;
        if (ChildCleanupPolicy.Decide(1, 2, true, 2, 0) != ChildCleanupAction.LogoffOwned ||
            ChildCleanupPolicy.Decide(1, 2, true, UInt32.MaxValue, 0) != ChildCleanupAction.AlreadyAbsent ||
            ChildCleanupPolicy.Decide(1, 2, false, UInt32.MaxValue, 1168) != ChildCleanupAction.AlreadyAbsent ||
            ChildCleanupPolicy.Decide(1, 2, false, 2, 5) != ChildCleanupAction.Refuse ||
            ChildCleanupPolicy.Decide(1, 2, false, 2, 0) != ChildCleanupAction.Refuse ||
            ChildCleanupPolicy.Decide(1, 2, true, 3, 0) != ChildCleanupAction.Refuse ||
            ChildCleanupPolicy.Decide(1, 2, true, 1, 0) != ChildCleanupAction.Refuse ||
            ChildCleanupPolicy.Decide(1, 2, true, 0, 0) != ChildCleanupAction.Refuse ||
            ChildCleanupPolicy.Decide(1, 1, true, 1, 0) != ChildCleanupAction.Refuse ||
            ChildCleanupPolicy.Decide(1, 0, true, 0, 0) != ChildCleanupAction.Refuse ||
            ChildCleanupPolicy.Decide(1, UInt32.MaxValue, true, UInt32.MaxValue, 0) != ChildCleanupAction.Refuse)
            return false;
        try {
            test.Observe(true, 3, 1, true, false, 14000);
            return false;
        } catch (InvalidOperationException) { return true; }
    }
    private static void Set(object control, string name, object value) {
        control.GetType().InvokeMember(name, BindingFlags.SetProperty, null, control,
            new object[] { value });
    }
    private static void Call(object control, string name) {
        control.GetType().InvokeMember(name, BindingFlags.InvokeMethod, null, control, null);
    }
    private static object Get(object control, string name) {
        return control.GetType().InvokeMember(name, BindingFlags.GetProperty, null, control, null);
    }
    private static void Ensure(bool result, string operation) {
        if (!result) throw new InvalidOperationException(operation + " failed: " + Marshal.GetLastWin32Error());
    }
    private static bool DisposeSafely(IDisposable resource) {
        if (resource == null) return true;
        try { resource.Dispose(); return true; }
        catch (Exception error) {
            Console.Error.WriteLine("Resource cleanup failed HRESULT=0x" + error.GetBaseException().HResult.ToString("X8"));
            return false;
        }
    }

    [STAThread]
    private static int Main(string[] args) {
        if (args.Length == 1 && args[0] == "--self-test") {
            bool passed = SelfTest();
            Console.WriteLine((passed ? "PASS" : "FAIL") + ": child observation and cleanup policy; synthetic states only, no Windows session changes.");
            return passed ? 0 : 1;
        }
        if (args.Length != 1 || (args[0] != "--smoke" && args[0] != "--test")) {
            Console.Error.WriteLine("Usage: ms-child-session-test.exe --self-test|--smoke|--test");
            Console.Error.WriteLine("--test temporarily enables child sessions, connects for up to 20 seconds,");
            Console.Error.WriteLine("then logs off only the newly observed child and restores the initial flag.");
            return 1;
        }
        bool smoke = args[0] == "--smoke";
        Console.WriteLine("Child-session test: diagnostics v5");
        using (WindowsIdentity identity = WindowsIdentity.GetCurrent()) {
            bool admin = new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator);
            Console.WriteLine("Administrator role active: " + admin);
            if (!smoke && !admin) {
                Console.Error.WriteLine("FAIL: open PowerShell as administrator before --test. No enable or connection attempted.");
                return 2;
            }
        }
        bool initial = false, changed = false, connectionAttempted = false;
        bool success = false, cleanupOk = true;
        bool childIdentitySafe = true;
        uint parent = 0, createdChild = UInt32.MaxValue;
        Form form = null;
        RdpHost host = null;
        object control = null;
        RdpEventLog events = null;
        Mutex testLease = null;
        bool leaseHeld = false;
        string operation = "Read initial session state";
        try {
            Ensure(Native.ProcessIdToSessionId((uint)System.Diagnostics.Process.GetCurrentProcess().Id, out parent), "ProcessIdToSessionId");
            if (!smoke) {
                operation = "Acquire exclusive test lease";
                // The enable flag and active-child limit are system-wide.
                testLease = new Mutex(false, "Global\\MultiseatWindowsChildSessionTest");
                try { leaseHeld = testLease.WaitOne(0); }
                catch (AbandonedMutexException) { leaseHeld = true; }
                if (!leaseHeld) throw new InvalidOperationException("Another child-session test is active on this computer; refused.");
            }
            Ensure(Native.WTSIsChildSessionsEnabled(out initial), "WTSIsChildSessionsEnabled");
            uint existing;
            bool found = Native.WTSGetChildSessionId(out existing);
            int queryError = found ? 0 : Marshal.GetLastWin32Error();
            if (!smoke && ((!found && queryError != 1168) || (found && existing != UInt32.MaxValue)))
                throw new InvalidOperationException("Existing child or unknown child state; test refused.");
            // A disabled feature + ERROR_NOT_FOUND is the observed pre-test baseline.
            if (!smoke && !found && initial) throw new InvalidOperationException("Enabled feature with unknown child state; test refused.");
            operation = "Host RDP control";
            Application.EnableVisualStyles();
            form = new TestWindow(smoke) { Text = "Multiseat - teste de sessao filha (observacao de 20 segundos)", Width = 1000, Height = 720 };
            host = new RdpHost { Dock = DockStyle.Fill };
            form.Controls.Add(host);
            form.Show();
            Application.DoEvents();
            control = host.ControlObject;
            IExtendedSettings settings = (IExtendedSettings)control;
            operation = "Configure ConnectToChildSession";
            object childMode = true;
            Marshal.ThrowExceptionForHR(settings.Put("ConnectToChildSession", ref childMode));
            object verified;
            Marshal.ThrowExceptionForHR(settings.Get("ConnectToChildSession", out verified));
            if (!(verified is bool) || !(bool)verified) throw new InvalidOperationException("Child mode not verified.");
            // Explicitly local; no credentials supplied, saved or logged.
            operation = "Configure local RDP server/display";
            Set(control, "Server", "localhost");
            Set(control, "DesktopWidth", 960);
            Set(control, "DesktopHeight", 640);
            operation = "Configure client CredSSP support";
            object advanced = Get(control, "AdvancedSettings7");
            Console.WriteLine("Client CredSSP before configuration: " +
                Convert.ToBoolean(Get(advanced, "EnableCredSspSupport")));
            Set(advanced, "EnableCredSspSupport", true);
            bool credSsp = Convert.ToBoolean(Get(advanced, "EnableCredSspSupport"));
            Console.WriteLine("Client CredSSP after configuration: " + credSsp);
            if (!credSsp) throw new InvalidOperationException("Client CredSSP configuration not retained.");
            Marshal.ThrowExceptionForHR(settings.Get("ConnectToChildSession", out verified));
            if (!(verified is bool) || !(bool)verified) throw new InvalidOperationException("Child mode was lost during configuration.");
            operation = "Read RDP Connected property";
            short connectionState = Convert.ToInt16(Get(control, "Connected"));
            Console.WriteLine("RDP connection state before Connect: " + connectionState);
            if (connectionState != 0) throw new InvalidOperationException("Control is already connecting or connected.");
            operation = "Subscribe RDP events";
            events = new RdpEventLog(control);
            if (smoke) {
                Console.WriteLine("PASS: hosted RDP control and child property; no enable or connection.");
                success = true;
            } else {
                Console.WriteLine("Parent session: " + parent + "; initial child flag: " + initial);
                if (!initial) {
                    operation = "WTSEnableChildSessions";
                    Ensure(Native.WTSEnableChildSessions(true), "WTSEnableChildSessions");
                    changed = true;
                    Console.WriteLine("Child sessions enabled for test: True");
                }
                operation = "IMsTscAx.Connect";
                // Connect can throw after the native control has started work.
                connectionAttempted = true;
                Call(control, "Connect");
                Console.WriteLine("Connect returned; waiting for a distinct child session ID.");
                operation = "Wait for child session ID";
                Stopwatch elapsed = Stopwatch.StartNew();
                ChildObservation observation = new ChildObservation(parent);
                while (!form.IsDisposed && !events.Ended && elapsed.ElapsedMilliseconds < 20000) {
                    Application.DoEvents();
                    if (form.IsDisposed || events.Ended) break;
                    uint child;
                    bool currentChild = Native.WTSGetChildSessionId(out child);
                    bool stable = observation.Observe(currentChild, child,
                        Convert.ToInt16(Get(control, "Connected")), events.LoginComplete, events.Ended,
                        elapsed.ElapsedMilliseconds);
                    if (createdChild == UInt32.MaxValue && observation.ChildId != UInt32.MaxValue) {
                        createdChild = observation.ChildId;
                        Console.WriteLine("New child session observed: " + createdChild);
                    }
                    if (stable) { success = true; break; }
                    Thread.Sleep(25);
                }
                success = success && !form.IsDisposed && !events.Ended &&
                    Convert.ToInt16(Get(control, "Connected")) == 1;
                Console.WriteLine("RDP connection state after wait: " + Convert.ToInt16(Get(control, "Connected")));
                Console.WriteLine("RDP extended disconnect reason: " + Get(control, "ExtendedDisconnectReason"));
                if (events.DisconnectReason.HasValue) Console.WriteLine("RDP disconnect reason: " + events.DisconnectReason.Value);
                Console.WriteLine(success ? "PASS: distinct child ID, completed login and stable connection; input/display independence not tested."
                    : "FAIL: no usable distinct child session demonstrated; see RDP events above.");
            }
        } catch (Exception error) {
            Exception cause = error.GetBaseException();
            Console.Error.WriteLine("FAIL at: " + operation);
            Console.Error.WriteLine("Cause: " + cause.GetType().Name + " HRESULT=0x" +
                cause.HResult.ToString("X8") + ": " + cause.Message);
        } finally {
            // Re-query even if Connect threw after beginning session creation.
            if (changed || connectionAttempted) {
                uint latest;
                bool found = Native.WTSGetChildSessionId(out latest);
                int queryError = found ? 0 : Marshal.GetLastWin32Error();
                if (!found && queryError != 1168) {
                    childIdentitySafe = false;
                    Console.Error.WriteLine("Cleanup child identity query failed: " + queryError);
                } else if (found && latest != UInt32.MaxValue && latest != parent) {
                    if (createdChild == UInt32.MaxValue) createdChild = latest;
                    else if (createdChild != latest) childIdentitySafe = false;
                }
            }
            if (control != null && connectionAttempted) {
                try {
                    if (Convert.ToInt16(Get(control, "Connected")) != 0) Call(control, "Disconnect");
                    else Console.WriteLine("RDP control already disconnected; Disconnect not needed.");
                } catch (Exception error) {
                    Console.Error.WriteLine("Disconnect error HRESULT=0x" + error.GetBaseException().HResult.ToString("X8"));
                }
            }
            if (createdChild != UInt32.MaxValue && childIdentitySafe) {
                // Disconnect may already have ended the child. Revalidate its
                // identity immediately before issuing a destructive logoff.
                uint latest;
                bool found = Native.WTSGetChildSessionId(out latest);
                int queryError = found ? 0 : Marshal.GetLastWin32Error();
                ChildCleanupAction action = ChildCleanupPolicy.Decide(parent, createdChild, found, latest, queryError);
                if (action == ChildCleanupAction.AlreadyAbsent) {
                    Console.WriteLine("Owned child already ended; logoff not needed.");
                } else if (action == ChildCleanupAction.LogoffOwned) {
                    cleanupOk = Native.WTSLogoffSession(IntPtr.Zero, createdChild, true);
                    int logoffError = cleanupOk ? 0 : Marshal.GetLastWin32Error();
                    Console.WriteLine("Owned child logoff: " + cleanupOk);
                    if (!cleanupOk) Console.Error.WriteLine("Logoff error: " + logoffError);
                } else {
                    childIdentitySafe = false;
                    Console.Error.WriteLine("Child ownership changed or became unknown; logoff refused.");
                }
            }
            cleanupOk = childIdentitySafe && cleanupOk;
            cleanupOk = DisposeSafely(events) && cleanupOk;
            cleanupOk = DisposeSafely(host) && cleanupOk;
            cleanupOk = DisposeSafely(form) && cleanupOk;
            if (changed && childIdentitySafe) {
                bool restored = Native.WTSEnableChildSessions(initial);
                bool after;
                restored = restored && Native.WTSIsChildSessionsEnabled(out after) && after == initial;
                Console.WriteLine("Initial child flag restored: " + restored);
                cleanupOk = cleanupOk && restored;
            } else if (changed) {
                Console.Error.WriteLine("Initial child flag NOT restored: ambiguous child identity requires manual review; no unknown child was logged off.");
            }
            if (leaseHeld) {
                try { testLease.ReleaseMutex(); }
                catch (ApplicationException) { cleanupOk = false; Console.Error.WriteLine("Test lease release failed."); }
            }
            cleanupOk = DisposeSafely(testLease) && cleanupOk;
        }
        return success && cleanupOk ? 0 : 1;
    }
}
