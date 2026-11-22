#!/usr/bin/env bash
# Lists every roadmap slug from the developer-roadmap repository, as a JSON array.
#
# The repository moved: it is now nilbuild/developer-roadmap, and the roadmaps
# live at the top level rather than under src/data. The old
# kamranahmedse/.../src/data/roadmaps path answers 301 and then 404, so it is
# not a usable endpoint any more.
#
# The result is cached because the unauthenticated GitHub API allows only 60
# requests an hour, and this would otherwise spend one on every shell start.
set -uo pipefail

CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/zenith"
CACHE="$CACHE_DIR/roadmaps.json"
TTL=86400
API="https://api.github.com/repos/nilbuild/developer-roadmap/contents/roadmaps"

fresh() {
    [ -s "$CACHE" ] || return 1
    local age=$(( $(date +%s) - $(stat -c %Y "$CACHE" 2>/dev/null || echo 0) ))
    [ "$age" -lt "$TTL" ]
}

if fresh; then
    cat "$CACHE"
    exit 0
fi

mkdir -p "$CACHE_DIR"
out=$(curl -sSL -m 20 "$API" 2>/dev/null \
      | jq -c '[.[] | select(.type == "dir") | .name]' 2>/dev/null)

# Only replace the cache with something that actually parsed. A rate-limit
# reply is valid JSON but not an array, which is why the length check matters.
if [ -n "$out" ] && [ "$(printf '%s' "$out" | jq -r 'if type == "array" and length > 0 then "ok" else "no" end' 2>/dev/null)" = "ok" ]; then
    printf '%s' "$out" > "$CACHE"
    printf '%s' "$out"
elif [ -s "$CACHE" ]; then
    # Offline or rate limited: a stale list beats an empty menu.
    cat "$CACHE"
else
    printf '[]'
fi
