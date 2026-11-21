#!/usr/bin/env bash
# Event-driven streaming resource monitor daemon for zenith-shell

exec python3 -u -c '
import json, os, glob, time, socket, sys

def read_cpu():
    try:
        with open("/proc/stat", "r") as f:
            line = f.readline()
            parts = line.split()[1:]
            vals = [int(x) for x in parts]
            idle = vals[3] + (vals[4] if len(vals) > 4 else 0)
            return sum(vals), idle
    except Exception:
        return 0, 0

# Cache static system info
cpu_model = ""
curr_freq_mhz = 0
try:
    with open("/proc/cpuinfo", "r") as f:
        for line in f:
            if "model name" in line and not cpu_model:
                cpu_model = line.split(":", 1)[1].strip()
            elif "cpu MHz" in line and curr_freq_mhz == 0:
                try:
                    curr_freq_mhz = float(line.split(":", 1)[1].strip())
                except Exception:
                    pass
except Exception:
    pass

freq_str = f"{round(curr_freq_mhz/1000, 2)}GHz" if curr_freq_mhz else "N/A"

os_name = "NixOS"
if os.path.exists("/etc/os-release"):
    try:
        with open("/etc/os-release", "r") as f:
            for line in f:
                if line.startswith("PRETTY_NAME="):
                    os_name = line.split("=", 1)[1].strip().strip("\"")
    except Exception:
        pass
kernel = os.uname().release

ip_addr = ""
try:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("1.1.1.1", 80))
    ip_addr = s.getsockname()[0]
    s.close()
except Exception:
    pass

# Bytes moved on the interface that carries the default route, so the bar
# shows the link you are actually using rather than the first one in
# /proc/net/dev (which could be a bridge or a VM tap).
def default_iface():
    try:
        with open("/proc/net/route") as f:
            next(f)
            for line in f:
                cols = line.split()
                if len(cols) > 1 and cols[1] == "00000000":
                    return cols[0]
    except Exception:
        pass
    return ""

def read_net(iface):
    if not iface:
        return 0, 0
    try:
        with open("/proc/net/dev") as f:
            for line in f:
                if ":" not in line:
                    continue
                name, rest = line.split(":", 1)
                if name.strip() == iface:
                    cols = rest.split()
                    return int(cols[0]), int(cols[8])
    except Exception:
        pass
    return 0, 0

INTERVAL = 3.0
last_t, last_i = read_cpu()
iface = default_iface()
last_rx, last_tx = read_net(iface)
last_wall = time.monotonic()
_startup_ppid = os.getppid()

while True:
    try:
        # Orphaned by a shell that exited or reloaded: nothing is reading this
        # any more. Without the check these daemons accumulate one per restart
        # and poll forever -- four of them were found running against a shell
        # that had not existed for twelve hours.
        #
        # Compared against the parent we started with rather than against pid 1:
        # an orphan is not necessarily reparented to init, since systemd marks
        # the user session as a subreaper and adopts it instead.
        if os.getppid() != _startup_ppid:
            break
        time.sleep(INTERVAL)
        t, i = read_cpu()
        total_diff = t - last_t
        idle_diff = i - last_i
        last_t, last_i = t, i

        if total_diff > 0:
            cpu_overall = int(round(100.0 * (total_diff - idle_diff) / total_diff))
            cpu_overall = max(0, min(100, cpu_overall))
        else:
            cpu_overall = 0

        mem_total, mem_avail = 0, 0
        with open("/proc/meminfo", "r") as f:
            for line in f:
                if line.startswith("MemTotal:"):
                    mem_total = int(line.split()[1])
                elif line.startswith("MemAvailable:"):
                    mem_avail = int(line.split()[1])
        mem_perc = int(round(100.0 * (mem_total - mem_avail) / mem_total)) if mem_total > 0 else 0

        temps = []
        for p in glob.glob("/sys/class/hwmon/hwmon*/temp*_input") + glob.glob("/sys/class/thermal/thermal_zone*/temp"):
            try:
                with open(p, "r") as f:
                    v = int(f.read().strip())
                    if 10000 <= v <= 115000:
                        temps.append(v // 1000)
                    elif 10 <= v <= 115:
                        temps.append(v)
            except Exception:
                pass
        cpu_temp = max(temps) if temps else 0

        with open("/proc/loadavg", "r") as f:
            load = float(f.read().split()[0])
        load_perc = int(round(load * 10))

        fs_perc = 0
        try:
            st = os.statvfs("/")
            if st.f_blocks > 0:
                fs_perc = int(round(100.0 * (1.0 - (st.f_bavail / st.f_blocks))))
        except Exception:
            pass

        # Re-resolve the route occasionally: wifi <-> ethernet switches change it.
        iface = default_iface()
        rx, tx = read_net(iface)
        now = time.monotonic()
        dt = max(now - last_wall, 0.1)
        net_down = int(max(0, rx - last_rx) / 1024 / dt) if rx >= last_rx else 0
        net_up = int(max(0, tx - last_tx) / 1024 / dt) if tx >= last_tx else 0
        last_rx, last_tx, last_wall = rx, tx, now

        data = {
            "cpu": cpu_overall,
            "net_iface": iface,
            "net_down": net_down,
            "net_up": net_up,
            "mem": mem_perc,
            "temp": cpu_temp,
            "load": load,
            "load_perc": load_perc,
            "fs": fs_perc,
            "cpu_model": cpu_model,
            "freq": freq_str,
            "arch": os_name,
            "kernel": kernel,
            "ip": ip_addr
        }
        print(json.dumps(data), flush=True)
    except (BrokenPipeError, KeyboardInterrupt):
        break
    except Exception:
        time.sleep(2)
'
