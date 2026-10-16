using System;
using System.Collections.Generic;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace MultiseatPrototype {
    internal enum DeviceKind { Monitor, Keyboard, Mouse, Audio }
    internal sealed class DeviceChoice {
        internal DeviceKind Kind;
        internal string Id;
        internal string Label;
        internal string Details; // Local UI only; never part of the saved plan or diagnostics.
        internal IntPtr InputHandle;
        internal Rectangle Bounds;
        public override string ToString() { return Label; }
    }
    internal sealed class HardwareCatalog {
        internal readonly List<DeviceChoice> Devices = new List<DeviceChoice>();
        internal readonly List<string> Warnings = new List<string>();
        internal static string KindLabel(DeviceKind kind) {
            switch (kind) {
            case DeviceKind.Monitor: return "Monitor";
            case DeviceKind.Keyboard: return "Teclado";
            case DeviceKind.Mouse: return "Mouse";
            default: return "Áudio";
            }
        }
        internal static string Id(DeviceKind kind, string privateIdentifier) {
            using (SHA256 sha = SHA256.Create())
                return kind.ToString().ToLowerInvariant() + ":" + BitConverter.ToString(
                    sha.ComputeHash(Encoding.UTF8.GetBytes(privateIdentifier))).Replace("-", "").ToLowerInvariant();
        }
        internal DeviceChoice Find(DeviceKind kind, string id) {
            return Devices.Find(delegate(DeviceChoice d) { return d.Kind == kind && d.Id == id; });
        }
        internal DeviceChoice FindInput(IntPtr handle, DeviceKind kind) {
            return Devices.Find(delegate(DeviceChoice d) { return d.Kind == kind && d.InputHandle == handle; });
        }
        internal List<DeviceChoice> OfKind(DeviceKind kind) {
            return Devices.FindAll(delegate(DeviceChoice d) { return d.Kind == kind; });
        }
        internal static HardwareCatalog Preview() {
            HardwareCatalog catalog = new HardwareCatalog();
            foreach (DeviceKind kind in Enum.GetValues(typeof(DeviceKind)))
                for (int i = 0; i < 2; ++i)
                    catalog.Devices.Add(new DeviceChoice { Kind = kind, Id = Id(kind, "preview fixture " + i),
                        Label = KindLabel(kind) + " " + (i + 1) + (kind == DeviceKind.Monitor ? " · 1920 × 1080" : "") });
            return catalog;
        }
        internal static HardwareCatalog Scan() {
            HardwareCatalog catalog = new HardwareCatalog();
            Native.MonitorCallback callback = delegate(IntPtr monitor, IntPtr dc, IntPtr rect, IntPtr data) {
                Native.MonitorInfo info = new Native.MonitorInfo(); info.Size = Marshal.SizeOf(typeof(Native.MonitorInfo));
                if (!Native.GetMonitorInfo(monitor, ref info)) return false;
                Rectangle bounds = Rectangle.FromLTRB(info.Monitor.Left, info.Monitor.Top, info.Monitor.Right, info.Monitor.Bottom);
                Native.DisplayDevice physical = new Native.DisplayDevice(); physical.Size = Marshal.SizeOf(typeof(Native.DisplayDevice));
                string identity = Native.EnumDisplayDevices(info.Device, 0, ref physical, 1) && !String.IsNullOrEmpty(physical.DeviceId)
                    ? physical.DeviceId : info.Device;
                // Revalidate after topology/resolution changes rather than silently
                // treating DISPLAYn as a permanent physical-monitor identity.
                identity += "|" + bounds.Left + "," + bounds.Top + "," + bounds.Width + "," + bounds.Height;
                catalog.Devices.Add(new DeviceChoice { Kind = DeviceKind.Monitor,
                    Id = Id(DeviceKind.Monitor, identity), Bounds = bounds,
                    Label = "Monitor " + (catalog.OfKind(DeviceKind.Monitor).Count + 1) + " · " + bounds.Width + " × " + bounds.Height +
                        ((info.Flags & 1) != 0 ? " · principal" : "") });
                return catalog.Devices.Count <= 64;
            };
            if (!Native.EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, callback, IntPtr.Zero))
                catalog.Warnings.Add("Não foi possível listar todos os monitores.");
            try { catalog.ScanInputs(); }
            catch (ExternalException) {
                catalog.Warnings.Add("A lista de teclados e mouses está indisponível. Atualize os dispositivos.");
            } catch (InvalidOperationException) {
                catalog.Warnings.Add("A lista de teclados e mouses está indisponível. Atualize os dispositivos.");
            }
            try { catalog.ScanAudio(); }
            catch (ExternalException) {
                catalog.Warnings.Add("As saídas de áudio não estão disponíveis nesta sessão.");
            } catch (InvalidOperationException) {
                catalog.Warnings.Add("As saídas de áudio não estão disponíveis nesta sessão.");
            }
            return catalog;
        }
        private void ScanInputs() {
            uint itemSize = (uint)Marshal.SizeOf(typeof(Native.RawDevice));
            for (int attempt = 0; attempt < 3; ++attempt) {
                uint count = 0;
                if (Native.GetRawInputDeviceList(IntPtr.Zero, ref count, itemSize) == UInt32.MaxValue || count > 1024)
                    throw new InvalidOperationException();
                if (count == 0) return;
                IntPtr buffer = Marshal.AllocHGlobal(checked((int)(count * itemSize)));
                try {
                    uint read = Native.GetRawInputDeviceList(buffer, ref count, itemSize);
                    if (read == UInt32.MaxValue) continue; // hotplug raced with enumeration
                    if (read > count) throw new InvalidOperationException();
                    List<DeviceChoice> pending = new List<DeviceChoice>();
                    for (uint i = 0; i < read; ++i) {
                        Native.RawDevice device = (Native.RawDevice)Marshal.PtrToStructure(IntPtr.Add(buffer, checked((int)(i * itemSize))), typeof(Native.RawDevice));
                        if (device.Type != 0 && device.Type != 1) continue;
                        uint length = 0;
                        Native.GetRawInputDeviceInfo(device.Handle, 0x20000007, null, ref length);
                        if (length == 0 || length > 4096) continue;
                        StringBuilder name = new StringBuilder((int)length);
                        if (Native.GetRawInputDeviceInfo(device.Handle, 0x20000007, name, ref length) == UInt32.MaxValue || name.Length == 0) continue;
                        DeviceKind kind = device.Type == 1 ? DeviceKind.Keyboard : DeviceKind.Mouse;
                        string id = Id(kind, name.ToString());
                        if (pending.Exists(delegate(DeviceChoice d) { return d.Id == id; })) continue;
                        pending.Add(new DeviceChoice { Kind = kind, Id = id, InputHandle = device.Handle });
                    }
                    pending.Sort(delegate(DeviceChoice a, DeviceChoice b) { return String.CompareOrdinal(a.Id, b.Id); });
                    foreach (DeviceChoice device in pending) {
                        device.Label = KindLabel(device.Kind) + " " + (OfKind(device.Kind).Count + 1);
                        Devices.Add(device);
                    }
                    return;
                } finally { Marshal.FreeHGlobal(buffer); }
            }
            throw new InvalidOperationException();
        }
        private void ScanAudio() {
            IAudioEnumerator enumerator = null; IAudioCollection collection = null; IAudioDevice defaultDevice = null;
            try {
                enumerator = (IAudioEnumerator)new AudioEnumerator();
                string defaultId = null;
                if (enumerator.GetDefaultAudioEndpoint(0, 0, out defaultDevice) >= 0) defaultDevice.GetId(out defaultId);
                Marshal.ThrowExceptionForHR(enumerator.EnumAudioEndpoints(0, 1, out collection));
                uint count; Marshal.ThrowExceptionForHR(collection.GetCount(out count));
                if (count > 256) throw new InvalidOperationException();
                for (uint i = 0; i < count; ++i) {
                    IAudioDevice device = null;
                    try {
                        Marshal.ThrowExceptionForHR(collection.Item(i, out device));
                        string privateId; Marshal.ThrowExceptionForHR(device.GetId(out privateId));
                        Devices.Add(new DeviceChoice { Kind = DeviceKind.Audio, Id = Id(DeviceKind.Audio, privateId),
                            Label = "Áudio " + (i + 1) + (privateId == defaultId ? " · saída padrão" : ""), Details = AudioName(device) });
                    } finally { Release(device); }
                }
            } finally { Release(defaultDevice); Release(collection); Release(enumerator); }
        }
        private static string AudioName(IAudioDevice device) {
            IAudioProperties properties = null; AudioVariant value = new AudioVariant();
            try {
                if (device.OpenPropertyStore(0, out properties) < 0) return "Nome local indisponível";
                AudioPropertyKey key = new AudioPropertyKey { Format = new Guid("a45c254e-df1c-4efd-8020-67d146a850e0"), Number = 14 };
                if (properties.GetValue(ref key, out value) < 0 || value.Type != 31 || value.Pointer == IntPtr.Zero)
                    return "Nome local indisponível";
                string name = Marshal.PtrToStringUni(value.Pointer);
                return String.IsNullOrEmpty(name) ? "Nome local indisponível" : name.Substring(0, Math.Min(name.Length, 160));
            } finally { Native.PropVariantClear(ref value); Release(properties); }
        }
        private static void Release(object value) { if (value != null && Marshal.IsComObject(value)) Marshal.ReleaseComObject(value); }
    }

    internal static class AssociationInput {
        internal static bool IsKeyboardPress(ushort key, ushort flags) { return key == 0x75 && (flags & 1) == 0; }
        internal static bool IsMousePress(ushort flags) { return (flags & 0x0004) != 0; } // RI_MOUSE_RIGHT_BUTTON_DOWN
    }

    internal static class Native {
        internal delegate bool MonitorCallback(IntPtr monitor, IntPtr dc, IntPtr rect, IntPtr data);
        [StructLayout(LayoutKind.Sequential)] internal struct Rect { internal int Left, Top, Right, Bottom; }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] internal struct MonitorInfo {
            internal int Size; internal Rect Monitor, Work; internal uint Flags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] internal string Device;
        }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] internal struct DisplayDevice {
            internal int Size;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] internal string Name;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] internal string Description;
            internal uint State;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] internal string DeviceId;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] internal string Key;
        }
        [StructLayout(LayoutKind.Sequential)] internal struct RawDevice { internal IntPtr Handle; internal uint Type; }
        [StructLayout(LayoutKind.Sequential)] internal struct RawRegistration { internal ushort Page, Usage; internal uint Flags; internal IntPtr Target; }
        [StructLayout(LayoutKind.Sequential)] internal struct RawHeader { internal uint Type, Size; internal IntPtr Device, Parameter; }
        [StructLayout(LayoutKind.Sequential)] internal struct RawKeyboard { internal ushort Scan, Flags, Reserved, Key; internal uint Message, Extra; }
        [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool EnumDisplayMonitors(IntPtr dc, IntPtr clip, MonitorCallback callback, IntPtr data);
        [DllImport("user32.dll", EntryPoint = "GetMonitorInfoW", CharSet = CharSet.Unicode)] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);
        [DllImport("user32.dll", EntryPoint = "EnumDisplayDevicesW", CharSet = CharSet.Unicode)] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool EnumDisplayDevices(string device, uint index, ref DisplayDevice display, uint flags);
        [DllImport("user32.dll", SetLastError = true)] internal static extern uint GetRawInputDeviceList(IntPtr data, ref uint count, uint size);
        [DllImport("user32.dll", EntryPoint = "GetRawInputDeviceInfoW", CharSet = CharSet.Unicode, SetLastError = true)] internal static extern uint GetRawInputDeviceInfo(IntPtr device, uint command, StringBuilder data, ref uint size);
        [DllImport("user32.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool RegisterRawInputDevices(RawRegistration[] devices, uint count, uint size);
        [DllImport("user32.dll", SetLastError = true)] internal static extern uint GetRawInputData(IntPtr input, uint command, IntPtr data, ref uint bytes, uint headerSize);
        [DllImport("ole32.dll")] internal static extern int PropVariantClear(ref AudioVariant value);
    }
    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] internal class AudioEnumerator { }
    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IAudioEnumerator {
        [PreserveSig] int EnumAudioEndpoints(int flow, uint state, out IAudioCollection devices);
        [PreserveSig] int GetDefaultAudioEndpoint(int flow, int role, out IAudioDevice device);
    }
    [ComImport, Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IAudioCollection {
        [PreserveSig] int GetCount(out uint count);
        [PreserveSig] int Item(uint index, out IAudioDevice device);
    }
    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IAudioDevice {
        [PreserveSig] int Activate(ref Guid id, uint context, IntPtr parameters, [MarshalAs(UnmanagedType.IUnknown)] out object result);
        [PreserveSig] int OpenPropertyStore(uint access, out IAudioProperties store);
        [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
        [PreserveSig] int GetState(out uint state);
    }
    [StructLayout(LayoutKind.Sequential)] internal struct AudioPropertyKey { internal Guid Format; internal uint Number; }
    [StructLayout(LayoutKind.Explicit, Size = 24)] internal struct AudioVariant {
        [FieldOffset(0)] internal ushort Type;
        [FieldOffset(8)] internal IntPtr Pointer;
    }
    [ComImport, Guid("886d8eeb-8cf2-4446-8d02-cdba1dbdcf99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IAudioProperties {
        [PreserveSig] int GetCount(out uint count);
        [PreserveSig] int GetAt(uint index, out AudioPropertyKey key);
        [PreserveSig] int GetValue(ref AudioPropertyKey key, out AudioVariant value);
    }
}
