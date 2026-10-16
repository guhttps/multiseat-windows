using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

// Offline symbols only: no process attachment, memory reads or DLL execution.
public sealed class RpcStackSymbols : IDisposable
{
    private readonly IntPtr key = new IntPtr(0x4d535250);
    private bool initialized;
    [DllImport("dbghelp.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool SymInitializeW(IntPtr process, string path, bool invade);
    [DllImport("dbghelp.dll", SetLastError = true)]
    private static extern bool SymCleanup(IntPtr process);
    [DllImport("dbghelp.dll")]
    private static extern uint SymSetOptions(uint options);
    [DllImport("dbghelp.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern ulong SymLoadModuleExW(IntPtr process, IntPtr file, string image,
        string module, ulong address, uint size, IntPtr data, uint flags);
    [DllImport("dbghelp.dll", SetLastError = true)]
    private static extern bool SymFromAddr(IntPtr process, ulong address, out ulong displacement, IntPtr symbol);

    public RpcStackSymbols(string path)
    {
        // UNDNAME, EXACT_SYMBOLS, FAIL_CRITICAL_ERRORS, NO_PROMPTS, IGNORE_NT_SYMPATH.
        SymSetOptions(0x2 | 0x400 | 0x200 | 0x80000 | 0x1000);
        if (!SymInitializeW(key, path, false)) throw new Win32Exception(Marshal.GetLastWin32Error());
        initialized = true;
    }
    public void Load(string path, string name, ulong address, uint size)
    {
        if (SymLoadModuleExW(key, IntPtr.Zero, path, name, address, size, IntPtr.Zero, 0) == 0)
        {
            int error = Marshal.GetLastWin32Error();
            if (error != 0) throw new Win32Exception(error, "Symbol module load failed: " + name);
        }
    }
    public string Resolve(ulong address)
    {
        // SYMBOL_INFO x64 size 88; variable ANSI Name starts at offset 84.
        IntPtr buffer = Marshal.AllocHGlobal(88 + 1024);
        try
        {
            Marshal.Copy(new byte[88 + 1024], 0, buffer, 88 + 1024);
            Marshal.WriteInt32(buffer, 0, 88);
            Marshal.WriteInt32(buffer, 80, 1024);
            ulong displacement;
            if (!SymFromAddr(key, address, out displacement, buffer)) return null;
            int length = Marshal.ReadInt32(buffer, 76);
            if (length < 0 || length >= 1024) throw new InvalidDataException("Invalid symbol name length.");
            return Marshal.PtrToStringAnsi(IntPtr.Add(buffer, 84), length) + "+0x" + displacement.ToString("X");
        }
        finally { Marshal.FreeHGlobal(buffer); }
    }
    public void Dispose()
    {
        if (initialized) { SymCleanup(key); initialized = false; }
    }

    public static string[] PdbIdentity(string path)
    {
        byte[] image = File.ReadAllBytes(path);
        if (image.Length < 64 || image[0] != 'M' || image[1] != 'Z') throw new InvalidDataException("Not a PE file.");
        int pe = BitConverter.ToInt32(image, 60);
        if (BitConverter.ToUInt32(image, pe) != 0x4550) throw new InvalidDataException("Invalid PE signature.");
        int count = BitConverter.ToUInt16(image, pe + 6);
        int optional = pe + 24;
        ushort magic = BitConverter.ToUInt16(image, optional);
        if (magic != 0x20b && magic != 0x10b) throw new InvalidDataException("Invalid optional header.");
        int directories = optional + (magic == 0x20b ? 112 : 96);
        uint debugRva = BitConverter.ToUInt32(image, directories + 6 * 8);
        uint debugSize = BitConverter.ToUInt32(image, directories + 6 * 8 + 4);
        int sections = optional + BitConverter.ToUInt16(image, pe + 20);
        int debugOffset = -1;
        for (int i = 0; i < count; i++)
        {
            int section = sections + i * 40;
            uint rva = BitConverter.ToUInt32(image, section + 12);
            uint size = BitConverter.ToUInt32(image, section + 16);
            if (debugRva >= rva && (ulong)debugRva < (ulong)rva + size)
                debugOffset = checked((int)(debugRva - rva + BitConverter.ToUInt32(image, section + 20)));
        }
        if (debugOffset < 0) throw new InvalidDataException("Debug directory missing.");
        for (uint i = 0; i + 28 <= debugSize; i += 28)
        {
            int entry = checked(debugOffset + (int)i);
            if (BitConverter.ToUInt32(image, entry + 12) != 2) continue; // CODEVIEW
            int data = checked((int)BitConverter.ToUInt32(image, entry + 24));
            int size = checked((int)BitConverter.ToUInt32(image, entry + 16));
            if (size < 25 || data < 0 || (long)data + size > image.Length || BitConverter.ToUInt32(image, data) != 0x53445352) continue;
            byte[] guid = new byte[16];
            Array.Copy(image, data + 4, guid, 0, 16);
            uint age = BitConverter.ToUInt32(image, data + 20);
            string file = Encoding.UTF8.GetString(image, data + 24, size - 24).TrimEnd('\0');
            string name = Path.GetFileName(file);
            if (string.IsNullOrEmpty(name) || !name.EndsWith(".pdb", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Invalid PDB name.");
            return new string[] { name, new Guid(guid).ToString("N").ToUpperInvariant() + age.ToString("X") };
        }
        throw new InvalidDataException("RSDS identity missing.");
    }
}
