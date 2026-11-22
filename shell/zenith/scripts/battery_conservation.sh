#!/usr/bin/env bash
# Battery conservation / charge-limit control.
#
# Lenovo ideapad exposes conservation_mode; most other laptops expose
# charge_control_end_threshold instead. Both are looked for, so this works
# beyond the machine it was written on.
#
#   battery_conservation.sh status        {"supported":bool,"active":bool,"label":str}
#   battery_conservation.sh set 0|1       write the value
#   battery_conservation.sh toggle        flip it
#
# The sysfs node is root-owned. A direct write is tried first, because a udev
# rule may have granted access; pkexec is the fallback and will prompt. To make
# it passwordless, add to configuration.nix:
#
#   services.udev.extraRules = ''
#     KERNEL=="VPC2004:00", SUBSYSTEM=="platform", \
#       RUN+="${pkgs.coreutils}/bin/chgrp wheel /sys%p/conservation_mode", \
#       RUN+="${pkgs.coreutils}/bin/chmod g+w /sys%p/conservation_mode"
#   '';
set -uo pipefail

find_node() {
    local c
    for c in /sys/bus/platform/drivers/ideapad_acpi/*/conservation_mode \
             /sys/devices/platform/*/conservation_mode; do
        [ -f "$c" ] && { printf '%s\n' "$c"; return 0; }
    done
    for c in /sys/class/power_supply/*/charge_control_end_threshold; do
        [ -f "$c" ] && { printf '%s\n' "$c"; return 0; }
    done
    return 1
}

NODE="$(find_node || true)"

label_for() {
    case "$1" in
        *conservation_mode) echo "Conservation Mode" ;;
        *charge_control_end_threshold) echo "Charge Limit" ;;
        *) echo "Battery Care" ;;
    esac
}

# conservation_mode is 0/1; charge_control_end_threshold is a percentage, where
# anything below 100 counts as limited.
is_active() {
    local raw="$1"
    case "$NODE" in
        *charge_control_end_threshold) [ "${raw:-100}" -lt 100 ] ;;
        *) [ "${raw:-0}" = "1" ] ;;
    esac
}

value_for() {  # desired on/off -> value to write
    case "$NODE" in
        *charge_control_end_threshold) [ "$1" = "1" ] && echo 60 || echo 100 ;;
        *) echo "$1" ;;
    esac
}

emit_status() {
    if [ -z "$NODE" ]; then
        printf '{"supported":false,"active":false,"label":""}\n'
        return
    fi
    local raw active
    raw="$(cat "$NODE" 2>/dev/null || echo 0)"
    if is_active "$raw"; then active=true; else active=false; fi
    printf '{"supported":true,"active":%s,"label":"%s","value":"%s"}\n' \
        "$active" "$(label_for "$NODE")" "$raw"
}

write_value() {
    local v="$1"
    [ -n "$NODE" ] || { printf '{"ok":false,"error":"unsupported"}\n'; return; }
    if printf '%s' "$v" > "$NODE" 2>/dev/null; then
        printf '{"ok":true,"method":"direct"}\n'
    elif command -v pkexec >/dev/null 2>&1 \
         && pkexec /bin/sh -c "printf '%s' '$v' > '$NODE'" 2>/dev/null; then
        printf '{"ok":true,"method":"pkexec"}\n'
    else
        printf '{"ok":false,"error":"permission denied"}\n'
    fi
}

case "${1:-status}" in
    status) emit_status ;;
    set)    write_value "$(value_for "${2:-0}")" ;;
    toggle)
        raw="$(cat "$NODE" 2>/dev/null || echo 0)"
        if is_active "$raw"; then write_value "$(value_for 0)"; else write_value "$(value_for 1)"; fi
        ;;
    *) printf '{"ok":false,"error":"unknown command"}\n' ;;
esac
