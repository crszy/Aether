#!/usr/bin/env bash
# Output/input levels and mute state as one JSON line, for VolumeService.
#
# wpctl + pw-cli only: the previous python version spawned an interpreter and
# a full `pw-dump` of the graph on every PipeWire event.
set -uo pipefail

level() {  # "Volume: 0.78 [MUTED]" -> "78 true"
    local out vol muted=false
    out="$(wpctl get-volume "$1" 2>/dev/null || true)"
    [[ "$out" == *MUTED* ]] && muted=true
    vol="$(awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+(\.[0-9]+)?$/) { printf "%d", $i * 100 + 0.5; exit } }' <<<"$out")"
    printf '%s %s' "${vol:-0}" "$muted"
}

read -r out_vol out_muted < <(level @DEFAULT_AUDIO_SINK@)
read -r mic_vol mic_muted < <(level @DEFAULT_AUDIO_SOURCE@)

bt=false
wpctl inspect @DEFAULT_AUDIO_SINK@ 2>/dev/null | grep -qi bluez && bt=true

# An application is capturing when a plain input stream exists. The
# "/Internal" variants are PipeWire's own loopbacks (a Bluetooth headset mic,
# for one) and exist whether or not anything is listening.
mic_active=false
pw-cli ls Node 2>/dev/null | grep -q 'media.class = "Stream/Input/Audio"$' && mic_active=true

printf '{"outputVolume":%s,"muted":%s,"micVolume":%s,"micMuted":%s,"micActive":%s,"btActive":%s}\n' \
    "$out_vol" "$out_muted" "$mic_vol" "$mic_muted" "$mic_active" "$bt"
