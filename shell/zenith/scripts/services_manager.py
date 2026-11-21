#!/usr/bin/env python3
"""
services_manager.py - Zenith System & User Services Controller

Hierarchical app-level services manager: manages primary daemons and automatically
cascades start/stop/restart/enable/disable across all their sub-services.
"""

import sys
import json
import subprocess
import re
from typing import List, Dict, Any

MAIN_APPS_DEF: List[Dict[str, Any]] = [
    {
        "id": "docker.service",
        "name": "Docker Engine",
        "scope": "system",
        "category": "Containers",
        "icon": "󰡨",
        "sub_units": [
            {"id": "docker.socket", "scope": "system"},
            {"id": "containerd.service", "scope": "system"}
        ]
    },
    {
        "id": "hermes-agent.service",
        "name": "Hermes AI Agent",
        "scope": "system",
        "category": "AI & Dev",
        "icon": "󰚩",
        "sub_units": [
            {"id": "hermes-gateway.service", "scope": "user"}
        ]
    },
    {
        "id": "samba-smbd.service",
        "name": "Samba Sharing",
        "scope": "system",
        "category": "Sharing & Network",
        "icon": "󰒍",
        "sub_units": [
            {"id": "samba-nmbd.service", "scope": "system"},
            {"id": "samba-wsdd.service", "scope": "system"},
            {"id": "samba-winbindd.service", "scope": "system"}
        ]
    },
    {
        "id": "figma-agent.service",
        "name": "Figma Agent",
        "scope": "user",
        "category": "AI & Dev",
        "icon": "󰞋",
        "sub_units": []
    },
    {
        "id": "libvirtd.service",
        "name": "Libvirt Hypervisor",
        "scope": "system",
        "category": "Virtualization",
        "icon": "󰢹",
        "sub_units": [
            {"id": "libvirt-guests.service", "scope": "system"},
            {"id": "virtnetworkd.service", "scope": "system"}
        ]
    },
    {
        "id": "bluetooth.service",
        "name": "Bluetooth Stack",
        "scope": "system",
        "category": "Hardware",
        "icon": "󰂯",
        "sub_units": [
            {"id": "mpris-proxy.service", "scope": "user"}
        ]
    },
    {
        "id": "pipewire.service",
        "name": "PipeWire Audio",
        "scope": "user",
        "category": "Audio",
        "icon": "󰓃",
        "sub_units": [
            {"id": "pipewire-pulse.service", "scope": "user"},
            {"id": "wireplumber.service", "scope": "user"}
        ]
    },
    {
        "id": "NetworkManager.service",
        "name": "Network Manager",
        "scope": "system",
        "category": "Sharing & Network",
        "icon": "󰖩",
        "sub_units": [
            {"id": "wpa_supplicant.service", "scope": "system"},
            {"id": "ModemManager.service", "scope": "system"}
        ]
    },
    {
        "id": "gammastep.service",
        "name": "Gammastep Night Light",
        "scope": "user",
        "category": "Display",
        "icon": "󱩌",
        "sub_units": []
    },
    {
        "id": "xdg-desktop-portal.service",
        "name": "Desktop Portals",
        "scope": "user",
        "category": "System",
        "icon": "󰖯",
        "sub_units": [
            {"id": "xdg-desktop-portal-hyprland.service", "scope": "user"},
            {"id": "xdg-desktop-portal-gtk.service", "scope": "user"}
        ]
    },
    {
        "id": "hyprpolkitagent.service",
        "name": "Hyprland Polkit Agent",
        "scope": "user",
        "category": "Security",
        "icon": "󰌾",
        "sub_units": []
    }
]

def format_bytes(b: int) -> str:
    if b <= 0:
        return "0 B"
    for unit in ["B", "KB", "MB", "GB"]:
        if b < 1024.0:
            return f"{b:.1f} {unit}" if unit in ["MB", "GB"] else f"{int(b)} {unit}"
        b /= 1024.0
    return f"{b:.1f} TB"

def fetch_properties(units: List[str], scope: str) -> Dict[str, Dict[str, str]]:
    if not units:
        return {}
    cmd = ["systemctl"]
    if scope == "user":
        cmd.append("--user")
    cmd.extend(["show", *units, "--property=Id,Description,ActiveState,SubState,UnitFileState,MainPID,MemoryCurrent,ActiveEnterTimestamp"])
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=5)
        raw = res.stdout
    except Exception:
        return {}

    results = {}
    current: Dict[str, str] = {}
    current_id = None

    for line in raw.splitlines():
        line = line.strip()
        if not line:
            if current_id and current:
                results[current_id] = current
                current = {}
                current_id = None
            continue
        if "=" in line:
            k, v = line.split("=", 1)
            current[k] = v
            if k == "Id":
                current_id = v

    if current_id and current:
        results[current_id] = current

    return results

def get_services_list() -> Dict[str, Any]:
    # Collect all needed system and user units
    all_system = set()
    all_user = set()

    for app in MAIN_APPS_DEF:
        if app["scope"] == "system":
            all_system.add(app["id"])
        else:
            all_user.add(app["id"])

        for sub in app.get("sub_units", []):
            if sub["scope"] == "system":
                all_system.add(sub["id"])
            else:
                all_user.add(sub["id"])

    system_props = fetch_properties(list(all_system), "system")
    user_props = fetch_properties(list(all_user), "user")

    services = []
    total_running = 0
    total_stopped = 0
    total_failed = 0
    total_mem_bytes = 0

    for app in MAIN_APPS_DEF:
        main_id = app["id"]
        main_scope = app["scope"]
        main_p = system_props.get(main_id) if main_scope == "system" else user_props.get(main_id)
        if not main_p:
            main_p = {}

        # Aggregate across main + sub-units
        units_group = [{"id": main_id, "scope": main_scope, "props": main_p}]
        for sub in app.get("sub_units", []):
            sp = system_props.get(sub["id"]) if sub["scope"] == "system" else user_props.get(sub["id"])
            units_group.append({
                "id": sub["id"],
                "scope": sub["scope"],
                "props": sp or {}
            })

        group_running = False
        group_failed = False
        group_mem_bytes = 0
        active_sub_names = []

        for u in units_group:
            p = u["props"]
            act = p.get("ActiveState", "inactive")
            if act == "active":
                group_running = True
                active_sub_names.append(u["id"])
            elif act == "failed":
                group_failed = True

            mem_val = p.get("MemoryCurrent", "0")
            try:
                if mem_val not in ["[not set]", "", None]:
                    group_mem_bytes += int(mem_val)
            except ValueError:
                pass

        # Determine overall state
        if group_running:
            state = "active"
            sub_state = "running"
            total_running += 1
            total_mem_bytes += group_mem_bytes
        elif group_failed:
            state = "failed"
            sub_state = "failed"
            total_failed += 1
        else:
            state = "inactive"
            sub_state = "dead"
            total_stopped += 1

        unit_file_state = main_p.get("UnitFileState", "disabled")
        is_enabled = (unit_file_state in ["enabled", "linked", "linked-runtime", "static"])
        
        main_pid_str = main_p.get("MainPID", "0")
        try:
            main_pid = int(main_pid_str)
        except ValueError:
            main_pid = 0

        sub_list_short = [s["id"].replace(".service", "") for s in app.get("sub_units", [])]
        sub_desc = ", ".join(sub_list_short)

        services.append({
            "id": main_id,
            "name": app["name"],
            "scope": main_scope,
            "category": app["category"],
            "icon": app["icon"],
            "unitCount": len(units_group),
            "subUnits": [s["id"] for s in app.get("sub_units", [])],
            "subUnitsSummary": sub_desc,
            "activeState": state,
            "subState": sub_state,
            "isRunning": group_running,
            "unitFileState": unit_file_state,
            "isEnabled": is_enabled,
            "pid": main_pid,
            "memoryBytes": group_mem_bytes,
            "memoryFormatted": format_bytes(group_mem_bytes) if group_mem_bytes > 0 else "0 B",
            "activeEnterTimestamp": main_p.get("ActiveEnterTimestamp", "")
        })

    return {
        "services": services,
        "stats": {
            "total": len(services),
            "running": total_running,
            "stopped": total_stopped,
            "failed": total_failed,
            "totalMemoryBytes": total_mem_bytes,
            "totalMemoryFormatted": format_bytes(total_mem_bytes)
        }
    }

def find_app_def(unit_id: str) -> Dict[str, Any]:
    for app in MAIN_APPS_DEF:
        if app["id"] == unit_id:
            return app
        for sub in app.get("sub_units", []):
            if sub["id"] == unit_id:
                return app
    return {"id": unit_id, "scope": "system", "sub_units": []}

def control_service(action: str, scope: str, unit: str) -> Dict[str, Any]:
    app = find_app_def(unit)
    
    # Collect system and user targets
    system_targets = []
    user_targets = []

    if app["scope"] == "system":
        system_targets.append(app["id"])
    else:
        user_targets.append(app["id"])

    for sub in app.get("sub_units", []):
        if sub["scope"] == "system":
            system_targets.append(sub["id"])
        else:
            user_targets.append(sub["id"])

    messages = []
    overall_success = True

    if system_targets:
        cmd = ["systemctl", action, *system_targets]
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=20)
            if res.returncode != 0:
                err = res.stderr.strip() or res.stdout.strip()
                if "Read-only file system" in err:
                    err = "configured in NixOS configuration"
                messages.append(f"System ({len(system_targets)} units): {err}")
                overall_success = False
        except Exception as e:
            messages.append(f"System: {str(e)}")
            overall_success = False

    if user_targets:
        cmd = ["systemctl", "--user", action, *user_targets]
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=15)
            if res.returncode != 0:
                err = res.stderr.strip() or res.stdout.strip()
                messages.append(f"User ({len(user_targets)} units): {err}")
                overall_success = False
        except Exception as e:
            messages.append(f"User: {str(e)}")
            overall_success = False

    total_units = len(system_targets) + len(user_targets)
    msg = f"Successfully {action}ed {app.get('name', unit)} ({total_units} units)" if overall_success else "; ".join(messages)

    return {
        "success": overall_success,
        "action": action,
        "unit": unit,
        "scope": scope,
        "message": msg
    }

def get_service_logs(scope: str, unit: str, lines: int = 40) -> Dict[str, Any]:
    app = find_app_def(unit)
    
    # We can fetch journal logs for the main unit and any active sub-units
    cmd = ["journalctl"]
    if app["scope"] == "user":
        cmd.append("--user")
    cmd.extend(["-u", app["id"]])
    for sub in app.get("sub_units", []):
        if sub["scope"] == app["scope"]:
            cmd.extend(["-u", sub["id"]])
    cmd.extend(["-n", str(lines), "--no-pager"])

    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=6)
        raw_output = res.stdout.strip()
        raw_lines = raw_output.splitlines() if raw_output else []
        
        parsed_logs = []
        for line in raw_lines:
            line_str = line.strip()
            if not line_str:
                continue
            
            lower = line_str.lower()
            level = "info"
            if any(w in lower for w in ["error", "err", "failed", "failure", "fatal", "panic"]):
                level = "error"
            elif any(w in lower for w in ["warn", "warning"]):
                level = "warning"
            elif any(w in lower for w in ["started", "starting", "deactivated", "stopped", "stopping"]):
                level = "system"
            elif any(w in lower for w in ["debug"]):
                level = "debug"

            time_part = ""
            msg_part = line_str
            m = re.match(r"^([A-Z][a-z]{2}\s+\d+\s+\d{2}:\d{2}:\d{2})\s+.*:\s*(.*)$", line_str)
            if m:
                time_part = m.group(1)
                msg_part = m.group(2)

            parsed_logs.append({
                "raw": line_str,
                "timestamp": time_part,
                "message": msg_part,
                "level": level
            })

        return {
            "unit": unit,
            "appName": app.get("name", unit),
            "scope": app["scope"],
            "linesCount": len(parsed_logs),
            "logs": parsed_logs,
            "rawText": raw_output
        }
    except Exception as e:
        return {
            "unit": unit,
            "appName": app.get("name", unit),
            "scope": app["scope"],
            "linesCount": 0,
            "logs": [],
            "rawText": f"Error fetching logs: {str(e)}"
        }

def main():
    if len(sys.argv) < 2 or sys.argv[1] == "list":
        print(json.dumps(get_services_list()))
        return

    command = sys.argv[1]

    if command in ["start", "stop", "restart", "enable", "disable"]:
        if len(sys.argv) < 4:
            print(json.dumps({"success": False, "message": f"Usage: {command} <scope> <unit>"}))
            return
        scope = sys.argv[2]
        unit = sys.argv[3]
        print(json.dumps(control_service(command, scope, unit)))
        return

    if command == "logs":
        if len(sys.argv) < 4:
            print(json.dumps({"error": "Usage: logs <scope> <unit> [lines]"}))
            return
        scope = sys.argv[2]
        unit = sys.argv[3]
        lines = int(sys.argv[4]) if len(sys.argv) >= 5 else 40
        print(json.dumps(get_service_logs(scope, unit, lines)))
        return

    print(json.dumps({"error": f"Unknown command: {command}"}))

if __name__ == "__main__":
    main()
