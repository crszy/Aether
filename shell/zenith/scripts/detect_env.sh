#!/usr/bin/env bash
# Advanced Environment & System Metadata Detection Script for Quickshell / Zenith Shell

DISTRO_ID="linux"
DISTRO_NAME="Linux"
DISTRO_VERSION=""

if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO_ID="${ID:-linux}"
    DISTRO_NAME="${NAME:-Linux}"
    DISTRO_VERSION="${VERSION_ID:-}"
elif command -v lsb_release >/dev/null 2>&1; then
    DISTRO_ID=$(lsb_release -si | tr '[:upper:]' '[:lower:]')
    DISTRO_NAME=$(lsb_release -ds)
fi

USER_NAME="${USER:-$(whoami)}"
HOME_DIR="${HOME:-/home/$USER_NAME}"
HOSTNAME_STR="$(hostname 2>/dev/null || cat /etc/hostname 2>/dev/null || echo "localhost")"
KERNEL_VER="$(uname -r 2>/dev/null || echo "unknown")"

XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME_DIR/.local/share}"
XDG_DATA_DIRS="${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

# Timezone & Country detection
TZ_PATH="$(readlink -f /etc/localtime 2>/dev/null)"
TIMEZONE=""

if [ -n "$TZ_PATH" ]; then
    TIMEZONE="${TZ_PATH#*zoneinfo/}"
fi
if [ -z "$TIMEZONE" ] && command -v timedatectl >/dev/null 2>&1; then
    TIMEZONE="$(timedatectl 2>/dev/null | grep "Time zone:" | awk '{print $3}')"
fi

LANG_STR="${LANG:-en_US.UTF-8}"
LANG_CODE="${LANG_STR%%.*}"

# Extract Country Code & Name from Locale / Timezone / Fallback
COUNTRY_CODE=""
COUNTRY_NAME="United States"

# Timezone first, locale second.
#
# The locale says which language you read, not where you are: LANG=en_US.UTF-8
# is the default for a great many people who have never been to the United
# States. Checking it first meant a machine in Asia/Karachi reported US, and
# the calendar filled up with Presidents Day instead of Pakistan Day.
if [ -n "$TIMEZONE" ]; then
    case "$TIMEZONE" in
        *Karachi*|*Pakistan*) COUNTRY_CODE="PK" ;;
        *Riyadh*|*Saudi*) COUNTRY_CODE="SA" ;;
        *London*|*United_Kingdom*) COUNTRY_CODE="GB" ;;
        *Berlin*|*Germany*) COUNTRY_CODE="DE" ;;
        *Paris*|*France*) COUNTRY_CODE="FR" ;;
        *Tokyo*|*Japan*) COUNTRY_CODE="JP" ;;
        *Kolkata*|*India*) COUNTRY_CODE="IN" ;;
        *Jakarta*|*Indonesia*) COUNTRY_CODE="ID" ;;
        *Cairo*|*Egypt*) COUNTRY_CODE="EG" ;;
        *Toronto*|*Vancouver*|*Canada*) COUNTRY_CODE="CA" ;;
        *Sydney*|*Australia*) COUNTRY_CODE="AU" ;;
        *New_York*|*Chicago*|*Los_Angeles*|*America*) COUNTRY_CODE="US" ;;
    esac
fi

# Only when the timezone is unknown to the table above does the locale get a
# say -- it is better than nothing, but it is a guess.
if [ -z "$COUNTRY_CODE" ] && [[ "$LANG_STR" =~ _([A-Z]{2}) ]]; then
    COUNTRY_CODE="${BASH_REMATCH[1]}"
fi

COUNTRY_CODE="${COUNTRY_CODE:-US}"

case "$COUNTRY_CODE" in
    PK) COUNTRY_NAME="Pakistan" ;;
    US) COUNTRY_NAME="United States" ;;
    GB) COUNTRY_NAME="United Kingdom" ;;
    IN) COUNTRY_NAME="India" ;;
    DE) COUNTRY_NAME="Germany" ;;
    FR) COUNTRY_NAME="France" ;;
    SA) COUNTRY_NAME="Saudi Arabia" ;;
    ID) COUNTRY_NAME="Indonesia" ;;
    CA) COUNTRY_NAME="Canada" ;;
    AU) COUNTRY_NAME="Australia" ;;
    JP) COUNTRY_NAME="Japan" ;;
    TR) COUNTRY_NAME="Türkiye" ;;
    *) COUNTRY_NAME="$COUNTRY_CODE" ;;
esac

# Screen resolution detection via Hyprland, wlr-randr, or drm
SCREEN_WIDTH=1920
SCREEN_HEIGHT=1080
REFRESH_RATE=60
RES_STR="1920x1080"

if command -v hyprctl >/dev/null 2>&1; then
    MON_INFO="$(hyprctl monitors -j 2>/dev/null | jq -r '.[0] | "\(.width) \(.height) \(.refreshRate)"' 2>/dev/null)"
    if [ -n "$MON_INFO" ]; then
        read -r SCREEN_WIDTH SCREEN_HEIGHT REFRESH_RATE <<< "$MON_INFO"
        REFRESH_RATE=$(printf "%.0f" "$REFRESH_RATE" 2>/dev/null || echo 60)
        RES_STR="${SCREEN_WIDTH}x${SCREEN_HEIGHT}"
    fi
elif command -v wlr-randr >/dev/null 2>&1; then
    WLR_RES="$(wlr-randr 2>/dev/null | grep -m1 "px" | awk '{print $1}')"
    if [ -n "$WLR_RES" ]; then
        RES_STR="$WLR_RES"
        SCREEN_WIDTH="${WLR_RES%%x*}"
        SCREEN_HEIGHT="${WLR_RES##*x}"
    fi
fi

ICON_BASES=()

# Distro-specific icon base directory discovery (prioritizing NixOS profile & system paths)
if [ "$DISTRO_ID" = "nixos" ]; then
    [ -d "/etc/profiles/per-user/$USER_NAME/share/icons" ] && ICON_BASES+=("/etc/profiles/per-user/$USER_NAME/share/icons")
    [ -d "/run/current-system/sw/share/icons" ] && ICON_BASES+=("/run/current-system/sw/share/icons")
    [ -d "$XDG_DATA_HOME/icons" ] && ICON_BASES+=("$XDG_DATA_HOME/icons")
    [ -d "$HOME_DIR/.local/share/icons" ] && ICON_BASES+=("$HOME_DIR/.local/share/icons")
    [ -d "$HOME_DIR/.icons" ] && ICON_BASES+=("$HOME_DIR/.icons")
    [ -d "$HOME_DIR/.nix-profile/share/icons" ] && ICON_BASES+=("$HOME_DIR/.nix-profile/share/icons")
else
    [ -d "$XDG_DATA_HOME/icons" ] && ICON_BASES+=("$XDG_DATA_HOME/icons")
    [ -d "$HOME_DIR/.local/share/icons" ] && ICON_BASES+=("$HOME_DIR/.local/share/icons")
    [ -d "$HOME_DIR/.icons" ] && ICON_BASES+=("$HOME_DIR/.icons")
    [ -d "/usr/share/icons" ] && ICON_BASES+=("/usr/share/icons")
    [ -d "/usr/local/share/icons" ] && ICON_BASES+=("/usr/local/share/icons")
    [ -d "/var/lib/flatpak/exports/share/icons" ] && ICON_BASES+=("/var/lib/flatpak/exports/share/icons")
fi

JSON_ICON_BASES=$(printf '%s\n' "${ICON_BASES[@]}" | jq -R . | jq -s .)

# Theme directories that exist on this machine. The shell's icon fallback
# probes <base><sub><name>.svg/.png with an Image for every candidate; without
# this list it tried every theme it knew of on every base, ~1300 failed loads
# (each one a logged warning) for a single notification with an unknown icon.
THEME_SUBPATHS=(
    /Reversal/status@2x/32/ /Reversal/status@2x/22/ /Reversal/status/32/ /Reversal/status/22/
    /Reversal/status/scalable/ /Reversal/apps/scalable/ /Reversal/apps/48/
    /Reversal-dark/status@2x/32/ /Reversal-dark/status@2x/22/ /Reversal-dark/status/scalable/ /Reversal-dark/apps/scalable/
    /breeze/status/22@2x/ /breeze/status/22/ /breeze/status/scalable/ /breeze/apps/scalable/ /breeze/apps/48/
    /breeze-dark/status/22/ /breeze-dark/status/scalable/
    /hicolor/scalable/status/ /hicolor/scalable/apps/ /hicolor/48x48/status/ /hicolor/48x48/apps/
    /Papirus/48x48/apps/ /Papirus/scalable/apps/ /Adwaita/scalable/apps/
)
ICON_DIRS=()
for base in "${ICON_BASES[@]}"; do
    for sub in "${THEME_SUBPATHS[@]}"; do
        [ -d "$base$sub" ] && ICON_DIRS+=("$base$sub")
    done
done
JSON_ICON_DIRS=$(printf '%s\n' "${ICON_DIRS[@]}" | jq -R . | jq -s 'map(select(length > 0))')

cat <<EOF
{
  "distroId": "$DISTRO_ID",
  "distroName": "$DISTRO_NAME",
  "distroVersion": "$DISTRO_VERSION",
  "kernelVersion": "$KERNEL_VER",
  "hostname": "$HOSTNAME_STR",
  "user": "$USER_NAME",
  "homeDir": "$HOME_DIR",
  "isNixOS": $([ "$DISTRO_ID" = "nixos" ] && echo true || echo false),
  "isArch": $([ "$DISTRO_ID" = "arch" ] && echo true || echo false),
  "timezone": "${TIMEZONE:-UTC}",
  "language": "$LANG_CODE",
  "countryCode": "$COUNTRY_CODE",
  "countryName": "$COUNTRY_NAME",
  "screenWidth": ${SCREEN_WIDTH:-1920},
  "screenHeight": ${SCREEN_HEIGHT:-1080},
  "refreshRate": ${REFRESH_RATE:-60},
  "resolution": "$RES_STR",
  "iconBases": $JSON_ICON_BASES,
  "iconDirs": $JSON_ICON_DIRS
}
EOF
