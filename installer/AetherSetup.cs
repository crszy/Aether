// Aether's installer (AetherSetup.exe) and uninstaller (built with /define:UNINSTALL as AetherUninstall.exe).
// C# 5 / .NET Framework 4.x, built with the csc.exe that ships with Windows (make-installer.ps1).
// Installs for the current user only (no administrator): %LOCALAPPDATA%\Programs\Aether by default.
// The program files ride inside the exe as the "payload.zip" resource.
using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

namespace AetherSetup
{
    static class Program
    {
        [STAThread]
        static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
#if UNINSTALL
            Application.Run(new UninstallForm());
#else
            // AetherSetup.exe --unpack-only <folder>: only unpacks (no shortcuts, registry or settings) - for testing
            if (args.Length == 2 && args[0] == "--unpack-only")
            {
                SetupForm.Unpack(args[1], null);
                return;
            }
            // the real setup: unpack to a temp folder behind a splash, then run setup-ui (QML) with that folder's
            // own Quickshell. "--classic" (or the QML setup failing to start) = the plain form below.
            if (args.Length == 1 && args[0] == "--classic")
            {
                Application.Run(new SetupForm());
                return;
            }
            SplashForm splash = new SplashForm();
            Application.Run(splash);
            if (splash.Fallback)
                Application.Run(new SetupForm());
#endif
        }
    }

#if !UNINSTALL
    // borderless splash: the wordmark and a thin progress line while the files unpack to %TEMP%
    partial class SplashForm : Form
    {
        public bool Fallback;
        string temp;
        int percent;
        readonly System.Windows.Forms.Timer anim = new System.Windows.Forms.Timer();
        float phase;

        public SplashForm()
        {
            FormBorderStyle = FormBorderStyle.None;
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(560, 300);
            BackColor = Color.FromArgb(7, 8, 12);
            DoubleBuffered = true;
            ShowInTaskbar = true;
            Text = "Aether Setup";
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch (Exception) { }
            anim.Interval = 16;
            anim.Tick += delegate { phase += 0.02f; Invalidate(); };
            Shown += delegate { anim.Start(); new Thread(Work) { IsBackground = true }.Start(); };
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
            // a slow glow behind the wordmark
            int glowX = (int)(ClientSize.Width / 2 + Math.Sin(phase) * 120) - 220;
            using (System.Drawing.Drawing2D.GraphicsPath p = new System.Drawing.Drawing2D.GraphicsPath())
            {
                p.AddEllipse(glowX, 20, 440, 260);
                using (System.Drawing.Drawing2D.PathGradientBrush b = new System.Drawing.Drawing2D.PathGradientBrush(p))
                {
                    b.CenterColor = Color.FromArgb(70, 158, 203, 255);
                    b.SurroundColors = new Color[] { Color.FromArgb(0, 7, 8, 12) };
                    g.FillPath(b, p);
                }
            }
            using (Font f = new Font("Segoe UI", 34f, FontStyle.Bold))
            {
                string word = "A E T H E R";
                SizeF s = g.MeasureString(word, f);
                g.DrawString(word, f, Brushes.White, (ClientSize.Width - s.Width) / 2, 92);
            }
            using (Font f = new Font("Segoe UI", 10.5f))
            using (Brush b = new SolidBrush(Color.FromArgb(150, 255, 255, 255)))
            {
                string sub = message;
                SizeF s = g.MeasureString(sub, f);
                g.DrawString(sub, f, b, (ClientSize.Width - s.Width) / 2, 160);
            }
            int w = 320, x = (ClientSize.Width - w) / 2, y = 214;
            using (Brush track = new SolidBrush(Color.FromArgb(40, 255, 255, 255)))
                g.FillRectangle(track, x, y, w, 4);
            using (Brush fill = new SolidBrush(Color.FromArgb(158, 203, 255)))
                g.FillRectangle(fill, x, y, w * percent / 100, 4);
        }

        void Work()
        {
            try
            {
                temp = Path.Combine(Path.GetTempPath(), "AetherSetup-" + DateTime.Now.Ticks);
                Directory.CreateDirectory(temp);
                using (Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip"))
                using (ZipArchive zip = new ZipArchive(s, ZipArchiveMode.Read))
                {
                    int n = zip.Entries.Count, i = 0;
                    string root = Path.GetFullPath(temp).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
                    foreach (ZipArchiveEntry e in zip.Entries)
                    {
                        i++;
                        string target = Path.GetFullPath(Path.Combine(temp, e.FullName));
                        if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                            continue;
                        if (e.FullName.EndsWith("/") || e.FullName.EndsWith("\\"))
                        {
                            Directory.CreateDirectory(target);
                            continue;
                        }
                        Directory.CreateDirectory(Path.GetDirectoryName(target));
                        e.ExtractToFile(target, true);
                        percent = i * 100 / n;
                    }
                }

                string qs = Path.Combine(temp, "qs", "qsw.exe");
                string ui = Path.Combine(temp, "config", "AetherSetup.qml"); // Caelestia's config folder, so it uses its parts
                if (!File.Exists(qs) || !File.Exists(ui))
                    throw new Exception("setup files missing");
                ProcessStartInfo psi = new ProcessStartInfo(qs, "-p \"" + ui + "\"");
                psi.UseShellExecute = false;
                psi.WorkingDirectory = temp;
                psi.EnvironmentVariables["PATH"] = Path.Combine(temp, "caelestia", "lib") + ";" + Path.Combine(temp, "qs") + ";" + Environment.GetEnvironmentVariable("PATH");
                string qsDir = Path.Combine(temp, "qs");
                psi.EnvironmentVariables["QML_IMPORT_PATH"] = Path.Combine(qsDir, "qml_modules") + ";" + Path.Combine(qsDir, "shims") + ";" + Path.Combine(temp, "caelestia", "qml") + ";" + Path.Combine(qsDir, "qml");
                psi.EnvironmentVariables["QT_PLUGIN_PATH"] = Path.Combine(qsDir, "plugins");
                psi.EnvironmentVariables["AETHER_SRC"] = temp;
                Process p = Process.Start(psi);
                DateTime started = DateTime.Now;
                Thread.Sleep(1500);
                BeginInvoke((MethodInvoker)delegate { Hide(); });
                p.WaitForExit();
                // it could not even start: fall back to the plain setup
                if (p.ExitCode != 0 && (DateTime.Now - started).TotalSeconds < 4)
                    Fallback = true;

                // "Install" in the setup window wrote its choices here; carry them out
                string plan = Path.Combine(temp, "install-plan.json");
                if (File.Exists(plan))
                    Install(plan);
            }
            catch (Exception)
            {
                Fallback = true;
            }
            finally
            {
                // the temp copy is ours alone; the installed copy lives in the chosen folder
                try { if (temp != null && Directory.Exists(temp)) Directory.Delete(temp, true); } catch (Exception) { }
                BeginInvoke((MethodInvoker)delegate { anim.Stop(); Close(); });
            }
        }
    }
#endif

#if !UNINSTALL
    partial class SplashForm
    {
        string message = "Getting everything ready…";

        void Say(string text)
        {
            message = text;
            BeginInvoke((MethodInvoker)delegate { Show(); Invalidate(); });
        }

        // moves the unpacked files into place (a rename on the same drive: no second copy for Defender to scan),
        // writes the settings, makes the shortcuts and starts Aether
        void Install(string planPath)
        {
            System.Web.Script.Serialization.JavaScriptSerializer js = new System.Web.Script.Serialization.JavaScriptSerializer();
            System.Collections.Generic.Dictionary<string, object> plan = js.Deserialize<System.Collections.Generic.Dictionary<string, object>>(File.ReadAllText(planPath));
            string target = Path.GetFullPath((string)plan["target"]);
            percent = 100;
            Say("Installing…");

            Paths.StopRunning(target);
            string old = null;
            if (Directory.Exists(target) && Directory.EnumerateFileSystemEntries(target).GetEnumerator().MoveNext())
            {
                // only ever replace an Aether Shell install - never a folder that belongs to something else
                if (!File.Exists(Path.Combine(target, "Caelestia.exe")) || !File.Exists(Path.Combine(target, "qs", "qs.exe")))
                    throw new Exception(target + " already holds another program - pick an empty folder.");
                old = target + ".old-" + DateTime.Now.Ticks;
                Directory.Move(target, old);
            }
            Directory.CreateDirectory(Path.GetDirectoryName(target));
            bool moved = false;
            for (int i = 0; i < 20 && !moved; i++)
            {
                try { Directory.Move(temp, target); moved = true; }
                catch (IOException) { Thread.Sleep(250); } // the setup window may still hold its files for a moment
            }
            if (!moved)
            {
                // another drive: copy once, then the temp copy is removed in finally
                Say("Copying to the other drive…");
                Process rc = Process.Start(new ProcessStartInfo("robocopy.exe", "\"" + temp + "\" \"" + target + "\" /E /MT:16 /NFL /NDL /NJH /NJS /NP") { CreateNoWindow = true, UseShellExecute = false });
                rc.WaitForExit();
                if (rc.ExitCode >= 8)
                    throw new Exception("copy failed");
            }
            else
            {
                temp = null; // it is the installed copy now
            }

            string settings = plan.ContainsKey("settingsJson") ? plan["settingsJson"] as string : null;
            if (!string.IsNullOrEmpty(settings))
            {
                string cfg = (string)plan["configDir"];
                Directory.CreateDirectory(cfg);
                File.WriteAllText(Path.Combine(cfg, "windows.json"), settings);
            }

            Say("Shortcuts…");
            string args = "-NoProfile -ExecutionPolicy Bypass -File \"" + Path.Combine(target, "setup-ui", "finish.ps1") + "\" -Dir \"" + target + "\"" +
                " -StartMenu " + ((bool)plan["startMenu"] ? "1" : "0") + " -Desktop " + ((bool)plan["desktop"] ? "1" : "0") + " -Autostart " + ((bool)plan["autostart"] ? "1" : "0");
            Process fin = Process.Start(new ProcessStartInfo("powershell.exe", args) { CreateNoWindow = true, UseShellExecute = false });
            fin.WaitForExit();

            if ((bool)plan["tiling"])
                Process.Start(new ProcessStartInfo("cmd.exe", "/c start \"AetherWM\" powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" +
                    Path.Combine(target, "extras", "aether-tiling", "build-aether-tiling.ps1") + "\" -Yes") { UseShellExecute = false });

            Say("Starting Aether…");
            Process.Start(new ProcessStartInfo(Path.Combine(target, "Caelestia.exe")) { WorkingDirectory = target, UseShellExecute = false });

            // the previous install (only if it really was one) goes once Aether is running from the new one
            if (old != null && File.Exists(Path.Combine(old, "Caelestia.exe")) && File.Exists(Path.Combine(old, "qs", "qs.exe")))
                try { Directory.Delete(old, true); } catch (Exception) { }
            Thread.Sleep(1500);
        }
    }
#endif

    static class Theme
    {
        public static readonly Color Back = Color.FromArgb(17, 19, 24);
        public static readonly Color Card = Color.FromArgb(29, 32, 40);
        public static readonly Color Text = Color.FromArgb(226, 226, 233);
        public static readonly Color Sub = Color.FromArgb(160, 163, 175);
        public static readonly Color Accent = Color.FromArgb(178, 197, 255);
        public static readonly Color OnAccent = Color.FromArgb(24, 46, 96);

        public static Button MakeButton(string text, bool primary)
        {
            Button b = new Button();
            b.Text = text;
            b.FlatStyle = FlatStyle.Flat;
            b.FlatAppearance.BorderSize = 0;
            b.BackColor = primary ? Accent : Card;
            b.ForeColor = primary ? OnAccent : Text;
            b.Font = new Font("Segoe UI Semibold", 10f);
            b.Height = 38;
            b.Width = 150;
            b.Cursor = Cursors.Hand;
            return b;
        }

        public static Label MakeLabel(string text, float size, Color color, bool bold)
        {
            Label l = new Label();
            l.Text = text;
            l.AutoSize = true;
            l.ForeColor = color;
            l.BackColor = Color.Transparent;
            l.Font = new Font(bold ? "Segoe UI Semibold" : "Segoe UI", size);
            return l;
        }

        public static CheckBox MakeCheck(string text, bool on)
        {
            CheckBox c = new CheckBox();
            c.Text = text;
            c.Checked = on;
            c.AutoSize = true;
            c.ForeColor = Text;
            c.Font = new Font("Segoe UI", 10f);
            c.FlatStyle = FlatStyle.Flat;
            return c;
        }
    }

    static class Paths
    {
        public const string AppName = "Aether Shell"; // not "Aether": another program uses that name
        public const string UninstallKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\AetherShell"; // not "Aether": another program uses it
        public const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";

        public static string DefaultDir()
        {
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", AppName);
        }
        public static string StartMenuDir()
        {
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), AppName);
        }
        public static string DesktopLink()
        {
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), AppName + ".lnk");
        }
        public static string ConfigDir()
        {
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".config", "caelestia");
        }

        public static void Shortcut(string link, string target, string args, string icon, string description)
        {
            Type t = Type.GetTypeFromProgID("WScript.Shell");
            object shell = Activator.CreateInstance(t);
            object sc = t.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { link });
            Type st = sc.GetType();
            st.InvokeMember("TargetPath", BindingFlags.SetProperty, null, sc, new object[] { target });
            st.InvokeMember("Arguments", BindingFlags.SetProperty, null, sc, new object[] { args ?? "" });
            st.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, sc, new object[] { Path.GetDirectoryName(target) });
            st.InvokeMember("IconLocation", BindingFlags.SetProperty, null, sc, new object[] { icon + ",0" });
            st.InvokeMember("Description", BindingFlags.SetProperty, null, sc, new object[] { description });
            st.InvokeMember("Save", BindingFlags.InvokeMethod, null, sc, null);
        }

        // asks a running Aether in that folder to quit (its files are in use while it runs)
        public static void StopRunning(string dir)
        {
            string launcher = Path.Combine(dir, "Caelestia.exe");
            if (!File.Exists(launcher))
                return;
            try
            {
                Process p = Process.Start(new ProcessStartInfo(launcher, "--stop") { UseShellExecute = false, CreateNoWindow = true });
                if (p != null)
                    p.WaitForExit(15000);
            }
            catch (Exception) { }
            Thread.Sleep(1500);
        }

        public static string Json(string s)
        {
            return "\"" + s.Replace("\\", "/").Replace("\"", "\\\"") + "\"";
        }
    }

    // a dark, borderless-looking window with a header
    class AetherForm : Form
    {
        protected Panel Body;

        public AetherForm(string title, string subtitle)
        {
            Text = title;
            BackColor = Theme.Back;
            ForeColor = Theme.Text;
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(640, 460);
            Font = new Font("Segoe UI", 10f);
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch (Exception) { }

            Label head = Theme.MakeLabel(title, 22f, Theme.Text, true);
            head.Location = new Point(32, 26);
            Controls.Add(head);
            Label sub = Theme.MakeLabel(subtitle, 10.5f, Theme.Sub, false);
            sub.Location = new Point(35, 72);
            sub.MaximumSize = new Size(570, 0);
            Controls.Add(sub);

            Body = new Panel();
            Body.Location = new Point(32, 116);
            Body.Size = new Size(576, 320);
            Body.BackColor = Theme.Back;
            Controls.Add(Body);
        }
    }

#if !UNINSTALL
    class SetupForm : AetherForm
    {
        TextBox dirBox;
        CheckBox startMenu, desktop, autostart;
        ProgressBar bar;
        Label status;
        Button install, browse, open;
        string installedDir;

        public SetupForm() : base("Aether", "Linux desktop shells on Windows - Caelestia, end-4, DankMaterial, Noctalia and Zenith.\nAfter installing you pick your look, your bars and your title bars.")
        {
            Label where = Theme.MakeLabel("Install to", 10f, Theme.Sub, false);
            where.Location = new Point(0, 0);
            Body.Controls.Add(where);

            dirBox = new TextBox();
            dirBox.Text = Paths.DefaultDir();
            dirBox.Location = new Point(0, 24);
            dirBox.Width = 440;
            dirBox.BackColor = Theme.Card;
            dirBox.ForeColor = Theme.Text;
            dirBox.BorderStyle = BorderStyle.FixedSingle;
            dirBox.Font = new Font("Segoe UI", 10.5f);
            Body.Controls.Add(dirBox);

            browse = Theme.MakeButton("Browse…", false);
            browse.Location = new Point(452, 21);
            browse.Width = 124;
            browse.Height = 32;
            browse.Click += delegate
            {
                using (FolderBrowserDialog d = new FolderBrowserDialog())
                {
                    d.SelectedPath = dirBox.Text;
                    if (d.ShowDialog(this) == DialogResult.OK)
                        dirBox.Text = Path.Combine(d.SelectedPath, Paths.AppName);
                }
            };
            Body.Controls.Add(browse);

            startMenu = Theme.MakeCheck("Start menu shortcut", true);
            startMenu.Location = new Point(0, 74);
            Body.Controls.Add(startMenu);
            desktop = Theme.MakeCheck("Desktop shortcut", false);
            desktop.Location = new Point(0, 104);
            Body.Controls.Add(desktop);
            autostart = Theme.MakeCheck("Start Aether when I sign in", false);
            autostart.Location = new Point(0, 134);
            Body.Controls.Add(autostart);

            Label note = Theme.MakeLabel("For this Windows user only - no administrator needed. Your settings stay in %USERPROFILE%\\.config\\caelestia.", 9f, Theme.Sub, false);
            note.Location = new Point(0, 176);
            note.MaximumSize = new Size(570, 0);
            Body.Controls.Add(note);

            bar = new ProgressBar();
            bar.Location = new Point(0, 224);
            bar.Size = new Size(576, 8);
            bar.Style = ProgressBarStyle.Continuous;
            bar.Visible = false;
            Body.Controls.Add(bar);

            status = Theme.MakeLabel("", 9.5f, Theme.Sub, false);
            status.Location = new Point(0, 240);
            Body.Controls.Add(status);

            install = Theme.MakeButton("Install", true);
            install.Location = new Point(426, 276);
            install.Click += delegate { Start(); };
            Body.Controls.Add(install);

            open = Theme.MakeButton("Open Aether", true);
            open.Location = new Point(426, 276);
            open.Visible = false;
            open.Click += delegate
            {
                try { Process.Start(new ProcessStartInfo(Path.Combine(installedDir, "Caelestia.exe")) { WorkingDirectory = installedDir }); } catch (Exception) { }
                Close();
            };
            Body.Controls.Add(open);

            AcceptButton = install;
        }

        void Start()
        {
            string dir = dirBox.Text.Trim();
            if (dir.Length == 0)
                return;
            install.Enabled = browse.Enabled = dirBox.Enabled = startMenu.Enabled = desktop.Enabled = autostart.Enabled = false;
            bar.Visible = true;
            bool sm = startMenu.Checked, dt = desktop.Checked, au = autostart.Checked;
            Thread t = new Thread(delegate () { Work(dir, sm, dt, au); });
            t.IsBackground = true;
            t.Start();
        }

        // unpacks the payload into dir (never outside it); form = progress, or null
        public static void Unpack(string dir, SetupForm form)
        {
            Directory.CreateDirectory(dir);
            using (Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip"))
            using (ZipArchive zip = new ZipArchive(s, ZipArchiveMode.Read))
            {
                int n = zip.Entries.Count, i = 0;
                string root = Path.GetFullPath(dir).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
                foreach (ZipArchiveEntry e in zip.Entries)
                {
                    i++;
                    string target = Path.GetFullPath(Path.Combine(dir, e.FullName));
                    if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                        continue;
                    if (e.FullName.EndsWith("/") || e.FullName.EndsWith("\\"))
                    {
                        Directory.CreateDirectory(target);
                        continue;
                    }
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    e.ExtractToFile(target, true);
                    if (form != null && i % 40 == 0)
                        form.Say("Unpacking… " + e.Name, 2 + i * 88 / n);
                }
            }
        }

        void Say(string text, int percent)
        {
            BeginInvoke((MethodInvoker)delegate
            {
                status.Text = text;
                if (percent >= 0)
                    bar.Value = Math.Max(0, Math.Min(100, percent));
            });
        }

        void Work(string dir, bool sm, bool dt, bool au)
        {
            try
            {
                Say("Closing Aether if it is running…", 0);
                Paths.StopRunning(dir);

                Say("Unpacking…", 1);
                Unpack(dir, this);

                Say("Shortcuts…", 92);
                string exe = Path.Combine(dir, "Caelestia.exe");
                if (sm)
                {
                    Directory.CreateDirectory(Paths.StartMenuDir());
                    Paths.Shortcut(Path.Combine(Paths.StartMenuDir(), "Aether Shell.lnk"), exe, "", exe, "Aether - Linux shells on Windows");
                    Paths.Shortcut(Path.Combine(Paths.StartMenuDir(), "Aether Shell settings.lnk"), exe, "--settings", exe, "Aether settings");
                    string un = Path.Combine(dir, "AetherUninstall.exe");
                    if (File.Exists(un))
                        Paths.Shortcut(Path.Combine(Paths.StartMenuDir(), "Uninstall Aether Shell.lnk"), un, "", un, "Remove Aether");
                }
                if (dt)
                    Paths.Shortcut(Paths.DesktopLink(), exe, "", exe, "Aether - Linux shells on Windows");
                using (RegistryKey run = Registry.CurrentUser.CreateSubKey(Paths.RunKey))
                {
                    if (au)
                        run.SetValue("Caelestia", "\"" + exe + "\"");
                }

                // Settings > Apps entry
                using (RegistryKey k = Registry.CurrentUser.CreateSubKey(Paths.UninstallKey))
                {
                    k.SetValue("DisplayName", "Aether Shell");
                    k.SetValue("DisplayIcon", exe);
                    k.SetValue("DisplayVersion", "1.0");
                    k.SetValue("Publisher", "Aether Shell");
                    k.SetValue("InstallLocation", dir);
                    k.SetValue("UninstallString", "\"" + Path.Combine(dir, "AetherUninstall.exe") + "\"");
                    k.SetValue("NoModify", 1, RegistryValueKind.DWord);
                    k.SetValue("NoRepair", 1, RegistryValueKind.DWord);
                }

                // first start: the "Make it yours" setup, with the video behind the settings. An existing
                // windows.json (someone who already uses Caelestia/Aether) is left exactly as it is.
                Directory.CreateDirectory(Paths.ConfigDir());
                string wj = Path.Combine(Paths.ConfigDir(), "windows.json");
                if (!File.Exists(wj))
                {
                    string video = Path.Combine(dir, "media", "aether-background.mp4");
                    File.WriteAllText(wj, "{\n    \"setupPending\": true,\n    \"settingsStyle\": \"rice\",\n    \"riceBackground\": " +
                        (File.Exists(video) ? Paths.Json(video) : "\"\"") + ",\n    \"riceDim\": 0.35\n}\n");
                }

                installedDir = dir;
                BeginInvoke((MethodInvoker)delegate
                {
                    bar.Value = 100;
                    status.Text = "Installed. Open Aether to pick your look.";
                    install.Visible = false;
                    open.Visible = true;
                    AcceptButton = open;
                });
            }
            catch (Exception ex)
            {
                BeginInvoke((MethodInvoker)delegate
                {
                    status.Text = "Install failed: " + ex.Message;
                    status.ForeColor = Color.FromArgb(255, 180, 171);
                    install.Enabled = browse.Enabled = dirBox.Enabled = true;
                });
            }
        }
    }
#else
    class UninstallForm : AetherForm
    {
        Label status;
        Button remove;

        public UninstallForm() : base("Remove Aether", "Removes the program, its shortcuts and its Settings > Apps entry.\nYour settings in %USERPROFILE%\\.config\\caelestia are kept.")
        {
            status = Theme.MakeLabel("", 10f, Theme.Sub, false);
            status.Location = new Point(0, 10);
            status.MaximumSize = new Size(570, 0);
            Body.Controls.Add(status);

            remove = Theme.MakeButton("Remove", true);
            remove.Location = new Point(426, 276);
            remove.Click += delegate { Remove(); };
            Body.Controls.Add(remove);

            Button cancel = Theme.MakeButton("Cancel", false);
            cancel.Location = new Point(266, 276);
            cancel.Click += delegate { Close(); };
            Body.Controls.Add(cancel);
        }

        void Remove()
        {
            remove.Enabled = false;
            string dir = Path.GetDirectoryName(Application.ExecutablePath);
            status.Text = "Closing Aether…";
            Refresh();
            Paths.StopRunning(dir);
            try { if (Directory.Exists(Paths.StartMenuDir())) Directory.Delete(Paths.StartMenuDir(), true); } catch (Exception) { }
            try { if (File.Exists(Paths.DesktopLink())) File.Delete(Paths.DesktopLink()); } catch (Exception) { }
            try
            {
                using (RegistryKey run = Registry.CurrentUser.OpenSubKey(Paths.RunKey, true))
                {
                    if (run != null && run.GetValue("Caelestia") != null)
                        run.DeleteValue("Caelestia");
                }
                Registry.CurrentUser.DeleteSubKeyTree(Paths.UninstallKey, false);
            }
            catch (Exception) { }

            // the install folder goes once this window has closed (this exe lives in it); only a folder that
            // really is an Aether install (Caelestia.exe + qs\qs.exe inside) is removed
            if (File.Exists(Path.Combine(dir, "Caelestia.exe")) && File.Exists(Path.Combine(dir, "qs", "qs.exe")))
            {
                ProcessStartInfo psi = new ProcessStartInfo("cmd.exe", "/c ping -n 3 127.0.0.1 >nul & rmdir /s /q \"" + dir + "\"");
                psi.CreateNoWindow = true;
                psi.UseShellExecute = false;
                psi.WorkingDirectory = Path.GetTempPath();
                Process.Start(psi);
            }
            status.Text = "Aether is removed.";
            remove.Visible = false;
        }
    }
#endif
}
