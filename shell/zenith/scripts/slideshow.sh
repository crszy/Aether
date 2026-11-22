#!/usr/bin/env bash
# Cycle the wallpapers listed in the slideshow list, one every INTERVAL
# seconds, re-theming the shell on each change. Run as the zenith-slideshow
# user service by the Wallpaper tab.
set -uo pipefail

SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIST="${XDG_CACHE_HOME:-$HOME/.cache}/zenith/slideshow_list"
INTERVAL="${ZENITH_SLIDESHOW_INTERVAL:-600}"

[ -s "$LIST" ] || { echo "no slideshow list at $LIST" >&2; exit 1; }
awww query >/dev/null 2>&1 || { awww-daemon & sleep 0.5; }

while :; do
    while IFS= read -r wall; do
        [ -f "$wall" ] || continue
        awww img --resize=crop "$wall" --transition-type fade --transition-duration 1 >/dev/null 2>&1
        printf '%s\n' "$wall" > "$HOME/.config/current_wallpaper.txt"
        "$SHELL_DIR/scripts/zenith-theme.sh" --autoselect "$wall" >/dev/null 2>&1
        sleep "$INTERVAL"
    done < "$LIST"
done
