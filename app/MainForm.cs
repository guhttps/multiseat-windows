using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace MultiseatPrototype {
    internal sealed class MainForm : Form {
        private HardwareCatalog catalog;
        private SeatPlan plan;
        private readonly ConfigurationStore store;
        private readonly ComboBox[,] selections = new ComboBox[2, 4];
        private readonly Label status = new Label();
        private readonly Label inventory = new Label();
        private readonly TextBox results = new TextBox();
        private Button save;
        private readonly ToolTip help = new ToolTip();
        private readonly Timer devicesChanged = new Timer();
        private readonly Color accent = Color.FromArgb(35, 93, 198);
        private int associationSeat = -1;
        private bool awaitingMouse;
        private bool updating;
        private bool registered;
        private bool dirty;
        private bool closing;
        private readonly bool passive;
        private readonly bool previewOnly;

        internal MainForm(HardwareCatalog devices, bool smoke, bool preview = false) {
            catalog = devices; store = ConfigurationStore.Local(); plan = SeatPlan.Empty(); passive = smoke;
            previewOnly = preview;
            Text = "Multiseat Windows · Painel experimental";
            Font = new Font("Segoe UI", 10F);
            BackColor = Color.FromArgb(243, 246, 251);
            ForeColor = Color.FromArgb(30, 42, 61);
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(1120, 800); MinimumSize = new Size(920, 700);
            AutoScaleMode = AutoScaleMode.Dpi;
            Icon = SystemIcons.Application;
            BuildInterface();
            string initialMessage = "Selecione os dispositivos de cada estação. Salve um rascunho local quando desejar.";
            if (!smoke) {
                try { plan = store.Load(); }
                catch (Exception error) {
                    if (!(error is IOException || error is UnauthorizedAccessException || error is System.Runtime.Serialization.SerializationException)) throw;
                    initialMessage = "Não foi possível carregar a configuração local. O arquivo foi preservado; revise as seleções antes de salvar.";
                }
            }
            PopulateSelections(); SetStatus(initialMessage, false);
            if (catalog.Warnings.Count != 0) AppendResult(String.Join(Environment.NewLine, catalog.Warnings.ToArray()));
            devicesChanged.Interval = 750;
            devicesChanged.Tick += delegate { devicesChanged.Stop(); RefreshDevices(); };
            FormClosing += delegate(object sender, FormClosingEventArgs args) {
                if (!smoke && dirty && MessageBox.Show(this, "Existem alterações não salvas. Fechar o painel e descartar essas alterações?",
                    "Configuração local", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) args.Cancel = true;
                if (!args.Cancel) { closing = true; devicesChanged.Stop(); }
            };
        }

        private Label TextLabel(string text, float size, bool bold) {
            return new Label { Text = text, AutoSize = false, Dock = DockStyle.Fill,
                Font = new Font("Segoe UI", size, bold ? FontStyle.Bold : FontStyle.Regular),
                TextAlign = ContentAlignment.MiddleLeft, Margin = Padding.Empty };
        }
        private Button Action(string text, EventHandler click, bool primary) {
            Button button = new Button { Text = text, AutoSize = true, Height = 36, MinimumSize = new Size(120, 36),
                FlatStyle = FlatStyle.Flat, BackColor = primary ? accent : Color.White,
                ForeColor = primary ? Color.White : ForeColor, Margin = new Padding(0, 0, 10, 0), Padding = new Padding(10, 2, 10, 2) };
            button.FlatAppearance.BorderColor = primary ? accent : Color.FromArgb(205, 214, 228);
            button.Click += click; return button;
        }
        private void BuildInterface() {
            Panel scroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true, Padding = new Padding(22) };
            Controls.Add(scroll);
            TableLayoutPanel layout = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 1 };
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); scroll.Controls.Add(layout);
            AddRow(layout, TextLabel("MULTISEAT WINDOWS", 22, true), 48);
            AddRow(layout, TextLabel("Seu computador, organizado em duas estações.", 11, false), 30);
            Label experimental = TextLabel("EXPERIMENTAL · Configuração e testes locais\r\nO motor de postos independentes ainda não está disponível. Esta versão não separa aplicativos nem contas Windows.", 10, false);
            experimental.BackColor = Color.FromArgb(255, 244, 214); experimental.Padding = new Padding(14, 5, 14, 5);
            AddRow(layout, experimental, 72);
            inventory.Dock = DockStyle.Fill; inventory.TextAlign = ContentAlignment.MiddleLeft;
            AddRow(layout, inventory, 44);
            TableLayoutPanel seats = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1 };
            seats.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50)); seats.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            seats.Controls.Add(BuildSeat(0), 0, 0); seats.Controls.Add(BuildSeat(1), 1, 0);
            AddRow(layout, seats, 322);
            FlowLayoutPanel actions = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = true, Padding = new Padding(0, 12, 0, 4) };
            save = Action("Salvar configuração", delegate { SavePlan(); }, true);
            actions.Controls.Add(save);
            actions.Controls.Add(Action("Atualizar dispositivos", delegate { RefreshDevices(); }, false));
            actions.Controls.Add(Action("Identificar monitores", delegate { IdentifyMonitors(); }, false));
            Button start = Action("Iniciar postos", delegate { }, false); start.Enabled = false;
            actions.Controls.Add(start);
            help.SetToolTip(start, "O motor independente ainda não foi implementado.");
            AddRow(layout, actions, 62);
            status.Dock = DockStyle.Fill; status.TextAlign = ContentAlignment.MiddleLeft;
            AddRow(layout, status, 46);
            AddRow(layout, TextLabel("Validação do protótipo", 12, true), 34);
            FlowLayoutPanel tests = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = true };
            tests.Controls.Add(Action("Testar dois pares", delegate { LaunchInteractive("ms-seat-demo.exe"); }, false));
            tests.Controls.Add(Action("Identificar entradas", delegate { LaunchInteractive("ms-keyboard-probe.exe"); }, false));
            tests.Controls.Add(Action("Diagnóstico de ambiente", delegate { RunDiagnostic("ms-diagnostic.exe"); }, false));
            tests.Controls.Add(Action("Sessões e monitores", delegate { RunDiagnostic("ms-session-probe.exe"); }, false));
            AddRow(layout, tests, 48);
            results.Multiline = true; results.ReadOnly = true; results.ScrollBars = ScrollBars.Vertical;
            results.Dock = DockStyle.Fill; results.BackColor = Color.White; results.BorderStyle = BorderStyle.FixedSingle;
            results.Font = new Font("Consolas", 9F);
            AddRow(layout, results, 125);
            AddRow(layout, TextLabel("O teste de dois pares usa painéis próprios; as seleções salvas ainda não controlam esses testes.\r\nRevise o plano após reconectar monitores ou mudar a resolução. A configuração fica somente no seu perfil.", 9, false), 52);
        }
        private static void AddRow(TableLayoutPanel layout, Control control, int height) {
            int row = layout.RowCount++; layout.RowStyles.Add(new RowStyle(SizeType.Absolute, height)); layout.Controls.Add(control, 0, row);
        }
        private Control BuildSeat(int index) {
            TableLayoutPanel card = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 7,
                BackColor = Color.White, Padding = new Padding(16), Margin = new Padding(index == 0 ? 0 : 8, 0, index == 0 ? 8 : 0, 0) };
            card.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 88)); card.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            card.RowStyles.Add(new RowStyle(SizeType.Absolute, 36)); card.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
            for (int i = 0; i < 4; ++i) card.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
            card.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
            Label title = TextLabel("Estação " + (index + 1), 17, true); title.ForeColor = accent;
            card.Controls.Add(title, 0, 0); card.SetColumnSpan(title, 2);
            Label state = TextLabel("PLANO LOCAL · não ativado", 9, true); state.ForeColor = Color.FromArgb(109, 119, 137);
            card.Controls.Add(state, 0, 1); card.SetColumnSpan(state, 2);
            foreach (DeviceKind kind in Enum.GetValues(typeof(DeviceKind))) {
                int row = (int)kind + 2;
                card.Controls.Add(TextLabel(HardwareCatalog.KindLabel(kind), 10, false), 0, row);
                ComboBox combo = new ComboBox { Dock = DockStyle.Fill, DropDownStyle = ComboBoxStyle.DropDownList, Margin = new Padding(0, 6, 0, 5),
                    AccessibleName = HardwareCatalog.KindLabel(kind) + " da estação " + (index + 1), DropDownWidth = 380 };
                int seat = index; DeviceKind deviceKind = kind;
                combo.SelectedIndexChanged += delegate {
                    if (updating) return;
                    DeviceChoice choice = combo.SelectedItem as DeviceChoice;
                    plan.Seats[seat].Set(deviceKind, choice == null ? null : choice.Id);
                    dirty = true;
                    List<string> errors = SeatPlan.Validate(plan, catalog);
                    SetStatus(errors.Count == 0 ? "Alterações locais ainda não salvas." : errors[0], errors.Count != 0);
                };
                selections[index, (int)kind] = combo;
                if (kind == DeviceKind.Audio) {
                    Panel audio = new Panel { Dock = DockStyle.Fill, Margin = Padding.Empty };
                    Button details = new Button { Text = "…", Dock = DockStyle.Right, Width = 30, FlatStyle = FlatStyle.Flat,
                        AccessibleName = "Ver nomes locais das saídas de áudio" };
                    details.Click += delegate { ShowAudioDetails(); }; help.SetToolTip(details, "Ver nomes locais das saídas de áudio");
                    combo.Dock = DockStyle.Fill; audio.Controls.Add(combo); audio.Controls.Add(details); card.Controls.Add(audio, 1, row);
                } else card.Controls.Add(combo, 1, row);
            }
            Button associate = Action("Associar teclado + mouse", delegate { BeginAssociation(index); }, false);
            card.Controls.Add(associate, 0, 6); card.SetColumnSpan(associate, 2);
            return card;
        }
        private void PopulateSelections() {
            updating = true;
            try {
                foreach (DeviceKind kind in Enum.GetValues(typeof(DeviceKind))) {
                    for (int seat = 0; seat < 2; ++seat) {
                        ComboBox combo = selections[seat, (int)kind]; combo.Items.Clear();
                        combo.Items.Add("Não associado");
                        foreach (DeviceChoice device in catalog.OfKind(kind)) combo.Items.Add(device);
                        string selectedId = plan.Seats[seat].Get(kind); combo.SelectedIndex = 0;
                        for (int i = 1; i < combo.Items.Count; ++i)
                            if (((DeviceChoice)combo.Items[i]).Id == selectedId) combo.SelectedIndex = i;
                        if (!String.IsNullOrEmpty(selectedId) && combo.SelectedIndex == 0) {
                            combo.Items.Add("Salvo, mas desconectado — selecione novamente"); combo.SelectedIndex = combo.Items.Count - 1;
                        }
                    }
                }
            } finally { updating = false; }
            inventory.Text = catalog.OfKind(DeviceKind.Monitor).Count + " monitores   ·   " + catalog.OfKind(DeviceKind.Keyboard).Count +
                " teclados   ·   " + catalog.OfKind(DeviceKind.Mouse).Count + " mouses   ·   " + catalog.OfKind(DeviceKind.Audio).Count + " saídas de áudio";
        }
        private void SetStatus(string message, bool error) { status.Text = message; status.ForeColor = error ? Color.FromArgb(173, 48, 42) : ForeColor; }
        private void AppendResult(string message) {
            if (results.TextLength > 32000) results.Clear();
            results.AppendText(message + Environment.NewLine);
        }
        private void SavePlan() {
            List<string> errors = SeatPlan.Validate(plan, catalog);
            if (errors.Count != 0) { SetStatus(errors[0], true); return; }
            try { store.Save(plan); dirty = false; SetStatus("Configuração salva apenas neste perfil. Os postos ainda não estão ativos.", false); }
            catch (Exception error) {
                if (!(error is IOException || error is UnauthorizedAccessException || error is System.Runtime.Serialization.SerializationException)) throw;
                SetStatus("Não foi possível salvar. A configuração anterior foi preservada.", true);
            }
        }
        private void ShowAudioDetails() {
            using (Form dialog = new Form { Text = "Saídas de áudio · detalhes locais", Width = 610, Height = 330,
                StartPosition = FormStartPosition.CenterParent, MinimizeBox = false, MaximizeBox = false, ShowInTaskbar = false }) {
                TextBox detail = new TextBox { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical,
                    BackColor = Color.White, Font = Font, BorderStyle = BorderStyle.None };
                detail.Text = "Use estes nomes para escolher a saída no plano. O painel ainda não redireciona o áudio.\r\n" +
                    "Os nomes são exibidos só nesta janela; não entram na configuração salva.\r\n\r\n";
                foreach (DeviceChoice device in catalog.OfKind(DeviceKind.Audio))
                    detail.AppendText(device.Label + ": " + (device.Details ?? "Dispositivo fictício da prévia") + "\r\n\r\n");
                dialog.Padding = new Padding(18); dialog.Controls.Add(detail); dialog.ShowDialog(this);
            }
        }
        private void RefreshDevices() {
            if (closing) return;
            associationSeat = -1; catalog = HardwareCatalog.Scan(); PopulateSelections();
            List<string> errors = SeatPlan.Validate(plan, catalog);
            SetStatus(errors.Count == 0 ? "Dispositivos atualizados. As seleções existentes foram preservadas." : errors[0], errors.Count != 0);
        }
        private void BeginAssociation(int seat) {
            if (!registered) { SetStatus("A leitura de entrada está indisponível. Use as listas de dispositivos.", true); return; }
            associationSeat = seat; awaitingMouse = false;
            SetStatus("Estação " + (seat + 1) + ": mantenha este painel em foco e pressione F6 no teclado desejado. Esc cancela.", false);
        }
        private void Associate(IntPtr handle, DeviceKind kind) {
            if (associationSeat < 0 || (awaitingMouse ? kind != DeviceKind.Mouse : kind != DeviceKind.Keyboard)) return;
            DeviceChoice device = catalog.FindInput(handle, kind);
            if (device == null) { SetStatus("Entrada sem origem disponível. Atualize os dispositivos e tente novamente.", true); return; }
            if (plan.Seats[1 - associationSeat].Get(kind) == device.Id) {
                SetStatus("Este dispositivo já está no plano da outra estação. Remova a seleção anterior primeiro.", true); return;
            }
            ComboBox combo = selections[associationSeat, (int)kind]; combo.SelectedItem = device;
            if (kind == DeviceKind.Keyboard) {
                awaitingMouse = true;
                SetStatus("Teclado escolhido. Agora clique com o botão direito do mouse desejado dentro deste painel.", false);
            } else {
                associationSeat = -1;
                SetStatus("Par associado ao plano local. Salve para guardar a seleção; isto ainda não isola aplicativos.", false);
            }
        }
        protected override bool ProcessCmdKey(ref Message message, Keys keys) {
            if (keys == Keys.Escape && associationSeat >= 0) { associationSeat = -1; SetStatus("Associação cancelada. As seleções já feitas foram mantidas.", false); return true; }
            return base.ProcessCmdKey(ref message, keys);
        }
        protected override void OnHandleCreated(EventArgs e) {
            base.OnHandleCreated(e);
            if (previewOnly) return;
            Native.RawRegistration[] registrations = {
                new Native.RawRegistration { Page = 1, Usage = 6, Flags = 0x2000, Target = Handle },
                new Native.RawRegistration { Page = 1, Usage = 2, Flags = 0x2000, Target = Handle } };
            registered = Native.RegisterRawInputDevices(registrations, 2, (uint)Marshal.SizeOf(typeof(Native.RawRegistration)));
        }
        protected override void OnHandleDestroyed(EventArgs e) {
            if (registered) {
                Native.RawRegistration[] registrations = {
                    new Native.RawRegistration { Page = 1, Usage = 6, Flags = 1 },
                    new Native.RawRegistration { Page = 1, Usage = 2, Flags = 1 } };
                Native.RegisterRawInputDevices(registrations, 2, (uint)Marshal.SizeOf(typeof(Native.RawRegistration)));
                registered = false;
            }
            base.OnHandleDestroyed(e);
        }
        protected override void WndProc(ref Message message) {
            if (message.Msg == 0x00ff && associationSeat >= 0) {
                int headerSize = Marshal.SizeOf(typeof(Native.RawHeader)); uint size = 48;
                IntPtr data = Marshal.AllocHGlobal((int)size);
                try {
                    uint read = Native.GetRawInputData(message.LParam, 0x10000003, data, ref size, (uint)headerSize);
                    if (read >= headerSize && read <= 48) {
                        Native.RawHeader header = (Native.RawHeader)Marshal.PtrToStructure(data, typeof(Native.RawHeader));
                        if (header.Size == read && header.Device != IntPtr.Zero) {
                            if (header.Type == 1 && read >= headerSize + 16) {
                                Native.RawKeyboard key = (Native.RawKeyboard)Marshal.PtrToStructure(IntPtr.Add(data, headerSize), typeof(Native.RawKeyboard));
                                if (AssociationInput.IsKeyboardPress(key.Key, key.Flags)) Associate(header.Device, DeviceKind.Keyboard);
                            } else if (header.Type == 0 && read >= headerSize + 24 &&
                                AssociationInput.IsMousePress(unchecked((ushort)Marshal.ReadInt16(data, headerSize + 4)))) Associate(header.Device, DeviceKind.Mouse);
                        }
                    }
                } finally { Marshal.FreeHGlobal(data); }
            }
            if (message.Msg == 0x00fe && !closing && !previewOnly) { devicesChanged.Stop(); devicesChanged.Start(); }
            base.WndProc(ref message); // Foreground Raw Input must receive default cleanup.
        }
        private void IdentifyMonitors() {
            foreach (DeviceChoice monitor in catalog.OfKind(DeviceKind.Monitor)) {
                Form marker = new Form { FormBorderStyle = FormBorderStyle.None, StartPosition = FormStartPosition.Manual,
                    Bounds = new Rectangle(monitor.Bounds.Left + 32, monitor.Bounds.Top + 32, 250, 100),
                    BackColor = accent, ShowInTaskbar = false, TopMost = true };
                Label number = TextLabel(monitor.Label.Split('·')[0].Trim(), 26, true); number.ForeColor = Color.White; marker.Controls.Add(number);
                Timer timer = new Timer { Interval = 2000 };
                timer.Tick += delegate { timer.Stop(); marker.Close(); timer.Dispose(); };
                marker.Show(this); timer.Start();
            }
        }
        private static string Companion(string name) {
            string path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, name);
            if (!File.Exists(path) || (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Companion indisponível.");
            return path;
        }
        private void LaunchInteractive(string name) {
            try {
                Process process = Process.Start(new ProcessStartInfo(Companion(name)) { WorkingDirectory = AppDomain.CurrentDomain.BaseDirectory, UseShellExecute = false });
                if (process != null) process.Dispose();
                SetStatus("Teste aberto. Associe os dispositivos novamente na janela do teste; fechar seus painéis encerra o teste.", false);
            } catch (Exception error) {
                if (!(error is IOException || error is System.ComponentModel.Win32Exception || error is InvalidOperationException)) throw;
                SetStatus("Teste indisponível. Use a instalação completa com os aplicativos de diagnóstico.", true);
            }
        }
        private async void RunDiagnostic(string name) {
            try {
                using (Process process = new Process()) {
                    process.StartInfo = new ProcessStartInfo(Companion(name)) { UseShellExecute = false, CreateNoWindow = true,
                        RedirectStandardOutput = true, RedirectStandardError = true, WorkingDirectory = AppDomain.CurrentDomain.BaseDirectory };
                    if (!process.Start()) throw new InvalidOperationException();
                    System.Threading.Tasks.Task<string> output = process.StandardOutput.ReadToEndAsync();
                    System.Threading.Tasks.Task<string> errors = process.StandardError.ReadToEndAsync();
                    bool ended = await System.Threading.Tasks.Task.Run(delegate { return process.WaitForExit(10000); });
                    if (!ended) { process.Kill(); process.WaitForExit(2000); }
                    string text = await output; string failure = await errors;
                    if (closing || IsDisposed) return;
                    AppendResult(name + " · " + (ended ? "código " + process.ExitCode : "tempo de observação excedido") + Environment.NewLine + text + failure);
                }
            } catch (Exception error) {
                if (closing || IsDisposed) return;
                if (!(error is IOException || error is System.ComponentModel.Win32Exception || error is InvalidOperationException)) throw;
                SetStatus("Diagnóstico indisponível. Verifique se os aplicativos de diagnóstico acompanham a instalação.", true);
            }
        }
        protected override bool ShowWithoutActivation { get { return passive; } }
        protected override CreateParams CreateParams {
            get {
                CreateParams parameters = base.CreateParams;
                if (passive) parameters.ExStyle |= 0x08000000;
                return parameters;
            }
        }
        private void PreparePassiveWindow() {
            Opacity = 0; ShowInTaskbar = false; Show(); PerformLayout(); Application.DoEvents();
        }
        internal bool SmokeCheck() {
            PreparePassiveWindow();
            if (!registered || !IsHandleCreated) return false;
            foreach (ComboBox selection in selections)
                if (!selection.IsHandleCreated || selection.Width < 100 || selection.Height < 10) return false;
            return true;
        }
        internal void RenderPreview(string path) {
            // Draw only our own controls into an off-screen bitmap, never the desktop.
            foreach (DeviceKind kind in Enum.GetValues(typeof(DeviceKind))) {
                List<DeviceChoice> choices = catalog.OfKind(kind);
                for (int i = 0; i < 2 && i < choices.Count; ++i) plan.Seats[i].Set(kind, choices[i].Id);
            }
            PopulateSelections(); SetStatus("Prévia com dispositivos fictícios. Nenhuma configuração real foi carregada ou salva.", false);
            ClientSize = new Size(1120, 960); PreparePassiveWindow();
            using (Bitmap bitmap = new Bitmap(Width, Height)) {
                DrawToBitmap(bitmap, new Rectangle(0, 0, Width, Height));
                using (FileStream file = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                    bitmap.Save(file, System.Drawing.Imaging.ImageFormat.Png);
            }
        }
        protected override void Dispose(bool disposing) {
            if (disposing) { devicesChanged.Dispose(); help.Dispose(); }
            base.Dispose(disposing);
        }
    }
}
