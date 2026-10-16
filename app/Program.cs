using System;
using System.IO;
using System.Reflection;
using System.Windows.Forms;

[assembly: AssemblyTitle("Multiseat Windows")]
[assembly: AssemblyDescription("Painel experimental para configurar duas estações e executar diagnósticos locais.")]
[assembly: AssemblyProduct("Multiseat Windows")]
[assembly: AssemblyVersion("0.1.0.0")]
[assembly: AssemblyFileVersion("0.1.0.0")]

namespace MultiseatPrototype {
    internal static class Program {
        private static bool SelfTest() {
            if (!AssociationInput.IsKeyboardPress(0x75, 0) || AssociationInput.IsKeyboardPress(0x75, 1) ||
                AssociationInput.IsKeyboardPress(0x41, 0) || !AssociationInput.IsMousePress(4) ||
                AssociationInput.IsMousePress(8) || AssociationInput.IsMousePress(1)) return false;
            HardwareCatalog catalog = new HardwareCatalog();
            SeatPlan plan = SeatPlan.Empty();
            if (SeatPlan.Validate(plan, catalog).Count != 0) return false;
            foreach (DeviceKind kind in Enum.GetValues(typeof(DeviceKind))) {
                for (int i = 0; i < 2; ++i) {
                    string id = HardwareCatalog.Id(kind, "synthetic fixture " + kind + " " + i);
                    catalog.Devices.Add(new DeviceChoice { Kind = kind, Id = id, Label = "Synthetic fixture" });
                    plan.Seats[i].Set(kind, id);
                }
            }
            if (SeatPlan.Validate(plan, catalog).Count != 0) return false;
            string keyboard = plan.Seats[1].Keyboard;
            plan.Seats[1].Keyboard = plan.Seats[0].Keyboard;
            if (SeatPlan.Validate(plan, catalog).Count == 0) return false;
            plan.Seats[1].Keyboard = keyboard;
            if (SeatPlan.Validate(plan, new HardwareCatalog()).Count == 0) return false;
            string temporaryRoot = Path.Combine(Path.GetTempPath(), "multiseat-panel-test-" + Guid.NewGuid().ToString("N"));
            ConfigurationStore store = new ConfigurationStore(temporaryRoot);
            try {
                store.Save(plan);
                SeatPlan loaded = store.Load();
                if (SeatPlan.Validate(loaded, catalog).Count != 0 || loaded.Seats[1].Keyboard != keyboard) return false;
                loaded.Seats[0].Audio = null; store.Save(loaded);
                if (store.Load().Seats[0].Audio != null) return false;
                byte[] previous = File.ReadAllBytes(store.FilePath);
                loaded.Seats[1].Keyboard = "invalid";
                bool refused = false;
                try { store.Save(loaded); } catch (InvalidDataException) { refused = true; }
                if (!refused || Convert.ToBase64String(previous) != Convert.ToBase64String(File.ReadAllBytes(store.FilePath))) return false;
                File.WriteAllText(store.FilePath, "{\"version\":999,\"seats\":[]}");
                try { store.Load(); return false; } catch (InvalidDataException) { }
                return true;
            } finally {
                // Delete only the fixture file we created; never recurse through a user directory.
                if (File.Exists(store.FilePath)) File.Delete(store.FilePath);
                if (Directory.Exists(temporaryRoot)) Directory.Delete(temporaryRoot, false);
            }
        }
        [STAThread]
        private static int Main(string[] args) {
            bool smoke = args.Length == 1 && args[0] == "--smoke";
            bool selfTest = args.Length == 1 && args[0] == "--self-test";
            bool screenshot = args.Length == 2 && args[0] == "--screenshot";
            if (args.Length != 0 && !smoke && !selfTest && !screenshot) return 2;
            try {
                if (selfTest) {
                    bool passed = SelfTest();
                    Console.WriteLine((passed ? "PASS" : "FAIL") + ": local plan validation, duplicate bindings, disconnected devices and atomic JSON storage; synthetic devices only.");
                    return passed ? 0 : 1;
                }
                Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
                if (!smoke && !screenshot) {
                    Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
                    Application.ThreadException += delegate {
                        MessageBox.Show("Não foi possível concluir esta ação. Feche o painel e tente novamente. Nenhum posto independente foi iniciado.",
                            "Multiseat Windows", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    };
                }
                HardwareCatalog catalog = screenshot ? HardwareCatalog.Preview() : HardwareCatalog.Scan();
                using (MainForm window = new MainForm(catalog, smoke || screenshot, screenshot)) {
                    if (screenshot) { window.RenderPreview(Path.GetFullPath(args[1])); return 0; }
                    if (smoke) {
                        bool passed = window.SmokeCheck();
                        Console.WriteLine((passed ? "PASS" : "FAIL") + ": hidden control panel, read-only hardware catalog and foreground input registration; no settings saved, companions started or sessions changed.");
                        return passed ? 0 : 1;
                    }
                    Application.Run(window);
                }
                return 0;
            } catch (Exception error) {
                if (!smoke && !selfTest && !screenshot) MessageBox.Show("O painel não pôde ser aberto. Use a instalação completa para Windows 10/11 x64 e tente novamente.",
                    "Multiseat Windows", MessageBoxButtons.OK, MessageBoxIcon.Error);
                else Console.Error.WriteLine("FAIL: control-panel operation could not complete (" + error.GetType().Name + ", HRESULT=0x" +
                    error.HResult.ToString("X8") + "); no session engine was started.");
                return 1;
            }
        }
    }
}
