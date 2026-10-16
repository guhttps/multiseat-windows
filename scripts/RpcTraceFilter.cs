using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;

// Restricts RPC in this project's existing ETW session; does not modify RPC security.
public static class RpcTraceFilter
{
    [StructLayout(LayoutKind.Sequential)]
    private struct Descriptor { public ulong Pointer; public uint Size; public uint Type; }
    [StructLayout(LayoutKind.Sequential)]
    private struct Parameters
    {
        public uint Version, EnableProperty, ControlFlags;
        public Guid SourceId;
        public IntPtr Filters;
        public uint FilterCount;
    }
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode)]
    private static extern uint ControlTraceW(ulong traceId, string name, IntPtr properties, uint control);
    [DllImport("advapi32.dll")]
    private static extern uint EnableTraceEx2(ulong traceId, ref Guid provider, uint control,
        byte level, ulong anyKeyword, ulong allKeyword, uint timeout, ref Parameters parameters);

    public static void Apply(string sessionName, uint[] processIds, bool includeStacks)
    {
        if (IntPtr.Size != 8) throw new InvalidOperationException("x64 process required.");
        if (!Regex.IsMatch(sessionName, @"\AMultiseatRdp_[0-9]{8}-[0-9]{6}-[a-f0-9]{8}\z"))
            throw new ArgumentException("Only this project's trace sessions are supported.");
        if (processIds == null || processIds.Length != 2 || processIds[0] == 0 || processIds[1] == 0)
            throw new ArgumentException("Two active service process IDs required.");

        IntPtr properties = IntPtr.Zero, ids = IntPtr.Zero, pids = IntPtr.Zero, filters = IntPtr.Zero;
        try
        {
            // EVENT_TRACE_PROPERTIES x64: 120-byte structure plus output strings.
            properties = Marshal.AllocHGlobal(8192);
            Marshal.Copy(new byte[8192], 0, properties, 8192);
            Marshal.WriteInt32(properties, 0, 8192); // Wnode.BufferSize
            Marshal.WriteInt32(properties, 44, 0x20000); // WNODE_FLAG_TRACED_GUID
            Marshal.WriteInt32(properties, 112, 4096); // LogFileNameOffset
            Marshal.WriteInt32(properties, 116, 120); // LoggerNameOffset
            uint status = ControlTraceW(0, sessionName, properties, 0); // QUERY
            if (status != 0) throw new Win32Exception((int)status, "Query owned ETW session failed.");
            ulong handle = unchecked((ulong)Marshal.ReadInt64(properties, 8)); // HistoricalContext

            ushort[] eventIds = { 1, 5, 6, 7, 8 };
            ids = Marshal.AllocHGlobal(4 + eventIds.Length * 2);
            Marshal.WriteByte(ids, 0, 1); // FilterIn
            Marshal.WriteByte(ids, 1, 0);
            Marshal.WriteInt16(ids, 2, (short)eventIds.Length);
            for (int index = 0; index < eventIds.Length; index++)
                Marshal.WriteInt16(ids, 4 + index * 2, (short)eventIds[index]);
            pids = Marshal.AllocHGlobal(processIds.Length * 4);
            for (int index = 0; index < processIds.Length; index++)
                Marshal.WriteInt32(pids, index * 4, unchecked((int)processIds[index]));

            int size = Marshal.SizeOf(typeof(Descriptor));
            filters = Marshal.AllocHGlobal(size * 2);
            Marshal.StructureToPtr(new Descriptor {
                Pointer = unchecked((ulong)ids.ToInt64()), Size = (uint)(4 + eventIds.Length * 2), Type = 0x80000200
            }, filters, false);
            Marshal.StructureToPtr(new Descriptor {
                Pointer = unchecked((ulong)pids.ToInt64()), Size = (uint)(processIds.Length * 4), Type = 0x80000004
            }, IntPtr.Add(filters, size), false);
            var parameters = new Parameters {
                Version = 2, Filters = filters, FilterCount = 2,
                // User SID of the event writer and EventWrite call stack; no security changes.
                EnableProperty = includeStacks ? 0x5U : 0U
            };
            Guid provider = new Guid("6ad52b32-d609-4be9-ae07-ce8dae937e39");
            status = EnableTraceEx2(handle, ref provider, 1, 4, ulong.MaxValue, 0, 0, ref parameters);
            if (status != 0) throw new Win32Exception((int)status, "RPC event/process filters failed.");
        }
        finally
        {
            if (filters != IntPtr.Zero) Marshal.FreeHGlobal(filters);
            if (pids != IntPtr.Zero) Marshal.FreeHGlobal(pids);
            if (ids != IntPtr.Zero) Marshal.FreeHGlobal(ids);
            if (properties != IntPtr.Zero) Marshal.FreeHGlobal(properties);
        }
    }
}
