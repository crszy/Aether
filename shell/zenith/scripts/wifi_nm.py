#!/usr/bin/env python3
import json
import sys
import subprocess
import os

def split_terse(line):
    fields = []
    curr = []
    escaped = False
    for ch in line:
        if escaped:
            curr.append(ch)
            escaped = False
        elif ch == "\\":
            escaped = True
        elif ch == ":":
            fields.append("".join(curr))
            curr = []
        else:
            curr.append(ch)
    fields.append("".join(curr))
    return fields

def run_cmd(args):
    try:
        res = subprocess.run(args, capture_output=True, text=True, timeout=10)
        return res.stdout.strip()
    except Exception:
        return ""

def get_wifi_state(do_scan=True, list_cached=False):
    radio = run_cmd(["nmcli", "radio", "wifi"])
    is_airplane = (radio.lower() == "disabled")
    
    # Get known connection names
    known_lines = run_cmd(["nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"]).split("\n")
    known_networks = []
    known_dict = {}
    for line in known_lines:
        if not line:
            continue
        parts = split_terse(line)
        if len(parts) >= 2 and ("wireless" in parts[1] or "wifi" in parts[1]):
            name = parts[0].strip()
            if name:
                known_networks.append(name)
                known_dict[name] = True

    # Find wifi device and active connection state
    dev_lines = run_cmd(["nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "dev"]).split("\n")
    wifi_dev = ""
    current_state = "disconnected"
    current_ssid = ""
    for line in dev_lines:
        if not line:
            continue
        parts = split_terse(line)
        if len(parts) >= 2 and parts[1] == "wifi":
            wifi_dev = parts[0].strip()
            state_str = parts[2].lower() if len(parts) >= 3 else ""
            if "connected" in state_str and "disconnecting" not in state_str:
                current_state = "connected"
            elif "connecting" in state_str or "associating" in state_str:
                current_state = "connecting"
            else:
                current_state = "disconnected"
            if len(parts) >= 4:
                current_ssid = parts[3].strip()
            break

    # Get IP address of wifi device if connected
    ipv4_address = ""
    if wifi_dev and current_state == "connected":
        show_lines = run_cmd(["nmcli", "-t", "-f", "IP4.ADDRESS", "dev", "show", wifi_dev]).split("\n")
        for line in show_lines:
            if ":" in line:
                val = line.split(":", 1)[1].strip()
                if val:
                    ipv4_address = val
                    break

    networks = []
    if do_scan or list_cached:
        # --rescan yes drives the radio for about five seconds. --rescan no
        # returns NetworkManager's existing view in about ten milliseconds,
        # which is what the panel shows while the real scan is still running.
        rescan = "yes" if do_scan else "no"
        scan_lines = run_cmd(["nmcli", "-t", "-f", "SSID,SIGNAL,SECURITY,IN-USE", "dev", "wifi", "list", "--rescan", rescan]).split("\n")
        seen = {}
        for line in scan_lines:
            if not line:
                continue
            parts = split_terse(line)
            if len(parts) >= 4:
                ssid = parts[0].strip()
                if not ssid or ssid in seen:
                    continue
                seen[ssid] = True
                
                try:
                    signal_pct = int(parts[1])
                except ValueError:
                    signal_pct = 0
                    
                if signal_pct >= 75:
                    signal_level = 4
                elif signal_pct >= 50:
                    signal_level = 3
                elif signal_pct >= 25:
                    signal_level = 2
                elif signal_pct > 0:
                    signal_level = 1
                else:
                    signal_level = 0
                    
                security = parts[2].strip().lower()
                is_active = (parts[3].strip() == "*")
                is_known = bool(known_dict.get(ssid, False))
                
                if is_active:
                    current_ssid = ssid
                    current_state = "connected"

                networks.append({
                    "ssid": ssid,
                    "signalPct": signal_pct,
                    "signal": signal_level,
                    "security": security,
                    "connected": is_active,
                    "isKnown": is_known
                })

        # Sort networks: active first, then known, then signal strength, then name
        networks.sort(key=lambda x: (
            not x["connected"],
            not x["isKnown"],
            -x["signalPct"],
            x["ssid"].lower()
        ))

    return {
        "isAirplane": is_airplane,
        "currentState": current_state,
        "currentSsid": current_ssid,
        "ipv4Address": ipv4_address,
        "knownNetworks": known_networks,
        "knownDict": known_dict,
        "networks": networks
    }

def main():
    do_scan = True
    list_cached = False
    if len(sys.argv) > 1:
        cmd = sys.argv[1].lower()
        if cmd == "status" or cmd == "fast":
            do_scan = False
        elif cmd == "cached":
            do_scan = False
            list_cached = True
        elif cmd == "connect" and len(sys.argv) >= 3:
            ssid = sys.argv[2]
            password = sys.argv[3] if len(sys.argv) >= 4 else ""
            if password:
                res = run_cmd(["nmcli", "dev", "wifi", "connect", ssid, "password", password])
            else:
                res = run_cmd(["nmcli", "dev", "wifi", "connect", ssid])
            print(res)
            return
        elif cmd == "disconnect":
            dev = run_cmd(["sh", "-c", "nmcli -t -f DEVICE,TYPE dev | grep ':wifi' | cut -d: -f1 | head -n1"])
            if dev:
                res = run_cmd(["nmcli", "dev", "disconnect", dev])
                print(res)
            return
        elif cmd == "forget" and len(sys.argv) >= 3:
            ssid = sys.argv[2]
            res = run_cmd(["nmcli", "connection", "delete", ssid])
            print(res)
            return
        elif cmd == "airplane":
            enable = sys.argv[2].lower() if len(sys.argv) >= 3 else "off"
            state = "off" if enable in ["true", "on", "1", "yes"] else "on"
            res = run_cmd(["nmcli", "radio", "wifi", state])
            print(res)
            return

    data = get_wifi_state(do_scan=do_scan, list_cached=list_cached)
    print(json.dumps(data, indent=2))

if __name__ == "__main__":
    main()
