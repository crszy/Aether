#!/usr/bin/env bash
# The detail behind one roadmap node, as JSON:
#
#   {"title":..., "description":..., "resources":[{"kind","label","url"}]}
#
# roadmap.sh keys its written content by node id: each roadmap has a content/
# directory of markdown files named "<topic-slug>@<nodeId>.md". So a node in the
# graph maps straight onto the same text the website shows when you click it.
#
# The file listing costs one GitHub API call (60/hour unauthenticated) and is
# cached per roadmap; the markdown itself comes from raw.githubusercontent,
# which is not rate limited.
set -uo pipefail

slug="${1:-}"
node="${2:-}"
if [ -z "$slug" ] || [ -z "$node" ]; then
    printf '{"title":"","description":"","resources":[]}'
    exit 0
fi

CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/zenith/roadmaps"
INDEX="$CACHE_DIR/$slug.index.json"
BODY="$CACHE_DIR/$slug.$node.json"
TTL=604800
mkdir -p "$CACHE_DIR"

fresh() {
    [ -s "$1" ] && [ $(( $(date +%s) - $(stat -c %Y "$1" 2>/dev/null || echo 0) )) -lt "$TTL" ]
}

if fresh "$BODY"; then cat "$BODY"; exit 0; fi

if ! fresh "$INDEX"; then
    curl -sSL -m 25 \
        "https://api.github.com/repos/nilbuild/developer-roadmap/contents/roadmaps/$slug/content" \
        2>/dev/null | jq -c '[.[]? | select(.type == "file") | .name]' > "$INDEX.tmp" 2>/dev/null
    if [ -s "$INDEX.tmp" ] && [ "$(jq -r 'type' "$INDEX.tmp" 2>/dev/null)" = "array" ]; then
        mv "$INDEX.tmp" "$INDEX"
    else
        rm -f "$INDEX.tmp"
    fi
fi

name="$(jq -r --arg n "@$node.md" '[.[] | select(endswith($n))][0] // empty' "$INDEX" 2>/dev/null)"
if [ -z "$name" ]; then
    printf '{"title":"","description":"","resources":[],"missing":true}'
    exit 0
fi

md="$(curl -sSL -m 25 \
    "https://raw.githubusercontent.com/nilbuild/developer-roadmap/master/roadmaps/$slug/content/$name" \
    2>/dev/null)"
[ -n "$md" ] || { printf '{"title":"","description":"","resources":[],"missing":true}'; exit 0; }

printf '%s' "$md" | python3 -c '
import json, re, sys

md = sys.stdin.read()
title, description, resources = "", [], []

for line in md.splitlines():
    stripped = line.strip()
    if not title and stripped.startswith("# "):
        title = stripped[2:].strip()
        continue
    # Resource lines look like: - [@article@Some Title](https://...)
    m = re.match(r"-\s*\[@(\w+)@([^\]]+)\]\((\S+)\)", stripped)
    if m:
        resources.append({"kind": m.group(1), "label": m.group(2).strip(), "url": m.group(3)})
        continue
    # A plain link with no type tag still counts.
    m = re.match(r"-\s*\[([^\]]+)\]\((\S+)\)", stripped)
    if m:
        resources.append({"kind": "link", "label": m.group(1).strip(), "url": m.group(2)})
        continue
    if stripped.lower().startswith("visit the following resources"):
        continue
    if stripped and not stripped.startswith("#"):
        description.append(stripped)

print(json.dumps({
    "title": title,
    "description": " ".join(description).strip(),
    "resources": resources,
}))
' > "$BODY.tmp" 2>/dev/null

if [ -s "$BODY.tmp" ]; then
    mv "$BODY.tmp" "$BODY"
    cat "$BODY"
else
    rm -f "$BODY.tmp"
    printf '{"title":"","description":"","resources":[]}'
fi
