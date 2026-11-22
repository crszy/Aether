#!/usr/bin/env bash
# What this machine physically has, as JSON.
#
# The shell should not offer controls for hardware that is not there: a
# Bluetooth panel on a desktop with no adapter, or a battery readout on a VM,
# is a dead button that makes the whole bar feel broken.
#
# Everything is read from sysfs rather than by asking a daemon, so this works
# before NetworkManager or bluetoothd are up, and costs nothing.
set -uo pipefail

has() { [ -e "$1" ] && echo true || echo false; }
first() { for c in "$@"; do [ -e "$c" ] && { echo "$c"; return; }; done; }

battery=false
for b in /sys/class/power_supply/BAT* /sys/class/power_supply/CMB*; do
    [ -e "$b/capacity" ] && { battery=true; break; }
done

bluetooth=false
for b in /sys/class/bluetooth/hci*; do
    [ -e "$b" ] && { bluetooth=true; break; }
done

wifi=false
for w in /sys/class/net/*/wireless /sys/class/net/*/phy80211; do
    [ -e "$w" ] && { wifi=true; break; }
done

ethernet=false
for e in /sys/class/net/*; do
    name="$(basename "$e")"
    case "$name" in
        lo|virbr*|docker*|veth*|br-*|tun*|tap*|wl*) continue ;;
    esac
    # A wired interface has a device symlink and no wireless directory.
    [ -e "$e/device" ] && [ ! -e "$e/wireless" ] && { ethernet=true; break; }
done

backlight=false
for l in /sys/class/backlight/*; do
    [ -e "$l/brightness" ] && { backlight=true; break; }
done

# systemd-detect-virt prints "none" and exits 1 on bare metal, so `|| echo none`
# appends a second "none" and every comparison against it fails -- which made a
# physical laptop report itself as a VM.
virt="$(systemd-detect-virt 2>/dev/null | head -1)"
[ -n "$virt" ] || virt="none"
if [ "$virt" = "none" ]; then is_vm=false; else is_vm=true; fi

# Chassis type: 8-11 and 14 are portables, 30-32 tablets. Used only as a
# fallback hint -- the presence of a battery is the real signal.
chassis="$(cat /sys/class/dmi/id/chassis_type 2>/dev/null || echo 0)"
case "$chassis" in
    8|9|10|11|14|30|31|32) portable=true ;;
    *) portable=false ;;
esac

printf '{"battery":%s,"bluetooth":%s,"wifi":%s,"ethernet":%s,"backlight":%s,"vm":%s,"virt":"%s","portable":%s}\n' \
    "$battery" "$bluetooth" "$wifi" "$ethernet" "$backlight" "$is_vm" "$virt" "$portable"
