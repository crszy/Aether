// AetherSensors — the temperature sidecar.
//
// Why this exists at all: reading an Intel/AMD CPU's temperature means reading MSRs, which needs
// ring 0. LibreHardwareMonitorLib carries the signed kernel driver that does it, and it is C#/.NET,
// so it cannot link into Aether's native shell. Hence a separate tiny process.
//
// It writes one small JSON file and nothing else — no socket, no server, no ports to configure.
// Aether reads that file and ignores it once it goes stale, so a dead sidecar degrades to "no
// reading" rather than a frozen number.
//
// It needs ADMIN to load the driver. Run unelevated and the CPU sensors simply come back empty;
// the JSON says so via "elevated" and "cpu": null, and Aether shows that rather than pretending.

using System;
using System.Globalization;
using System.IO;
using System.Security.Principal;
using System.Text;
using System.Threading;
using LibreHardwareMonitor.Hardware;

namespace AetherSensors
{
    // LHM only refreshes a subtree you visit, so every poll walks the tree through this.
    sealed class Visitor : IVisitor
    {
        public void VisitComputer(IComputer c) { c.Traverse(this); }
        public void VisitHardware(IHardware h) { h.Update(); foreach (var s in h.SubHardware) s.Accept(this); }
        public void VisitSensor(ISensor s) { }
        public void VisitParameter(IParameter p) { }
    }

    static class Program
    {
        static string OutPath()
        {
            string dir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Aether");
            Directory.CreateDirectory(dir);
            return Path.Combine(dir, "sensors.json");
        }

        static bool IsElevated()
        {
            try
            {
                using var id = WindowsIdentity.GetCurrent();
                return new WindowsPrincipal(id).IsInRole(WindowsBuiltInRole.Administrator);
            }
            catch { return false; }
        }

        // Prefer the package/die sensor over any single core, and never trust a wild value.
        static float? PickTemp(IHardware hw, params string[] preferred)
        {
            float? best = null;
            foreach (var s in hw.Sensors)
            {
                if (s.SensorType != SensorType.Temperature || !s.Value.HasValue) continue;
                float v = s.Value.Value;
                if (v <= 0 || v > 150) continue;
                foreach (var want in preferred)
                    if (s.Name.IndexOf(want, StringComparison.OrdinalIgnoreCase) >= 0)
                        return v;
                if (!best.HasValue || v > best.Value) best = v;   // fall back to the hottest reading
            }
            return best;
        }

        static string J(float? v) =>
            v.HasValue ? v.Value.ToString("0.0", CultureInfo.InvariantCulture) : "null";

        static int Main(string[] args)
        {
            int intervalMs = 2000;
            foreach (var a in args)
                if (a.StartsWith("--interval=", StringComparison.OrdinalIgnoreCase))
                    int.TryParse(a.Substring(11), out intervalMs);
            if (intervalMs < 500) intervalMs = 500;

            bool elevated = IsElevated();
            var computer = new Computer
            {
                IsCpuEnabled = true,
                IsGpuEnabled = true,
                IsMotherboardEnabled = true,
                IsStorageEnabled = false,
                IsMemoryEnabled = false,
                IsNetworkEnabled = false,
            };

            try { computer.Open(); }
            catch (Exception ex)
            {
                File.WriteAllText(OutPath(),
                    "{\"ok\":false,\"elevated\":" + (elevated ? "true" : "false") +
                    ",\"error\":\"" + ex.Message.Replace("\"", "'") + "\"}");
                return 1;
            }

            var visitor = new Visitor();
            // A single Ctrl+C / taskkill should leave a clean "stopped" marker rather than a stale
            // reading that Aether would keep showing until the staleness window expires.
            var stop = new ManualResetEventSlim(false);
            Console.CancelKeyPress += (s, e) => { e.Cancel = true; stop.Set(); };
            AppDomain.CurrentDomain.ProcessExit += (s, e) => stop.Set();

            try
            {
                while (!stop.IsSet)
                {
                    computer.Accept(visitor);

                    float? cpu = null, gpu = null;
                    string cpuName = "", gpuName = "";
                    foreach (var hw in computer.Hardware)
                    {
                        if (hw.HardwareType == HardwareType.Cpu)
                        {
                            var t = PickTemp(hw, "Package", "Tctl", "Tdie", "Core Average", "CPU Total");
                            if (t.HasValue && !cpu.HasValue) { cpu = t; cpuName = hw.Name; }
                        }
                        else if (hw.HardwareType == HardwareType.GpuNvidia ||
                                 hw.HardwareType == HardwareType.GpuAmd ||
                                 hw.HardwareType == HardwareType.GpuIntel)
                        {
                            var t = PickTemp(hw, "GPU Core", "GPU Temperature", "Hot Spot");
                            if (t.HasValue && !gpu.HasValue) { gpu = t; gpuName = hw.Name; }
                        }
                    }

                    var sb = new StringBuilder();
                    sb.Append("{\"ok\":true");
                    sb.Append(",\"elevated\":").Append(elevated ? "true" : "false");
                    sb.Append(",\"ts\":").Append(DateTimeOffset.UtcNow.ToUnixTimeSeconds());
                    sb.Append(",\"cpu\":").Append(J(cpu));
                    sb.Append(",\"gpu\":").Append(J(gpu));
                    sb.Append(",\"cpuName\":\"").Append(cpuName.Replace("\"", "'")).Append('"');
                    sb.Append(",\"gpuName\":\"").Append(gpuName.Replace("\"", "'")).Append('"');
                    sb.Append('}');

                    // write-then-move so Aether never reads a half-written file
                    string path = OutPath(), tmp = path + ".tmp";
                    File.WriteAllText(tmp, sb.ToString());
                    File.Move(tmp, path, true);

                    stop.Wait(intervalMs);
                }
            }
            finally
            {
                try { computer.Close(); } catch { }
                try { File.WriteAllText(OutPath(), "{\"ok\":false,\"stopped\":true}"); } catch { }
            }
            return 0;
        }
    }
}
