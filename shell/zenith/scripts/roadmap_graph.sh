#!/usr/bin/env bash
# Emits one roadmap as a drawable graph:
#
#   {"w":..,"h":..,"nodes":[{id,type,x,y,w,h,label,fs}],
#                  "edges":[{sx,sy,tx,ty,sd,td,dashed}]}
#
# Source is https://roadmap.sh/<slug>.json -- the same document roadmap.sh
# renders, so the layout here is the real one rather than a tree invented from
# the labels. Node positions are absolute canvas coordinates that do not start
# at zero, so everything is translated by the minimum corner here; QML then
# only has to place rectangles and draw lines.
#
# Edge endpoints are resolved to pixels here too. The handle letters encode a
# side, which was worked out from the geometry rather than documentation:
#   w = top    x = bottom    y = left    z = right
# (suffix 1 is an input, 2 an output).
set -uo pipefail

slug="${1:-}"
[ -n "$slug" ] || { printf '{"w":0,"h":0,"nodes":[],"edges":[]}'; exit 0; }

CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/zenith/roadmaps"
CACHE="$CACHE_DIR/$slug.graph.json"
TTL=604800

build() {
jq -c '
  # Everything that carries meaning. legend/vertical are page furniture.
  # paragraph nodes are the small group headings inside sections ("Key-Value",
  # "Document DBs"), so they stay -- dropping them left those groups unlabelled.
  #
  # The roadmap.sh promo block is dropped: a "roadmap.sh" button, its
  # "Click to visit the roadmap" hint and the "Find the detailed version..."
  # blurb are advertising for the website, not part of the roadmap.
  [ .nodes[]
    | select(.type == "topic" or .type == "subtopic"
             or .type == "button" or .type == "label" or .type == "section"
             or .type == "paragraph")
    | select(((.data.label // "")
              | test("^roadmap\\.sh$|^Click to visit the roadmap$|^Find the detailed version")) | not) ]
  as $candidates
  # Sponsor cards. roadmap.sh lays these out as a paragraph with a call-to-
  # action button positioned on top of it, which is fine on their canvas and
  # renders here as text with a button sitting through the middle of it.
  # Matched by the wording rather than a sponsor name, and anything drawn
  # inside such a card goes with it.
  | ([$candidates[]
      | select(.type == "paragraph")
      | select((.data.label // "") | test("^Special thanks|sponsor(ed)? by"; "i"))
      | {x: .position.x, y: .position.y,
         w: (.width // .measured.width // 100), h: (.height // .measured.height // 40)}]) as $ads
  | [$candidates[]
     | select(. as $n
              | ($ads | map(
                    ($n.position.x >= .x) and ($n.position.y >= .y)
                    and (($n.position.x + ($n.width // $n.measured.width // 100)) <= (.x + .w + 2))
                    and (($n.position.y + ($n.height // $n.measured.height // 40)) <= (.y + .h + 2))
                  ) | any) | not)]
  as $keep
  | ([$keep[].position.x] | min) as $minx
  | ([$keep[].position.y] | min) as $miny
  | ( reduce .nodes[] as $n ({};
        .[$n.id] = { x: ($n.position.x), y: ($n.position.y),
                     w: ($n.width // $n.measured.width // 100),
                     h: ($n.height // $n.measured.height // 40) }) ) as $N
  # anchor: node + handle letter -> absolute point
  | def anchor($n; $letter):
      if   $letter == "w" then { x: ($n.x + $n.w / 2), y: $n.y }
      elif $letter == "x" then { x: ($n.x + $n.w / 2), y: ($n.y + $n.h) }
      elif $letter == "y" then { x: $n.x,              y: ($n.y + $n.h / 2) }
      else                     { x: ($n.x + $n.w),     y: ($n.y + $n.h / 2) }
      end;
    {
      w: ((([$keep[] | .position.x + (.width // .measured.width // 100)] | max) - $minx) | ceil),
      h: ((([$keep[] | .position.y + (.height // .measured.height // 40)] | max) - $miny) | ceil),
      nodes: [ $keep[] | {
                 id: .id,
                 type: .type,
                 x: ((.position.x - $minx) | floor),
                 y: ((.position.y - $miny) | floor),
                 w: ((.width // .measured.width // .style.width // 100) | floor),
                 h: ((.height // .measured.height // .style.height // 40) | floor),
                 label: (.data.label // ""),
                 fs: (.data.style.fontSize // 0),
                 # roadmap.sh marks optional work through data.legend: purple is
                 # a personal recommendation, green an alternative to pick
                 # instead, grey something whose order does not matter. A node
                 # with no legend is core material, which is the distinction
                 # between "must learn" and "can skip".
                 lg: (.data.legend.label // ""),
                 lc: (.data.legend.color // "")
               } ],
      edges: [ .edges[]
               | select($N[.source] != null and $N[.target] != null)
               | (anchor($N[.source]; (.sourceHandle // "z")[0:1])) as $s
               | (anchor($N[.target]; (.targetHandle // "y")[0:1])) as $t
               | {
                   sx: (($s.x - $minx) | floor), sy: (($s.y - $miny) | floor),
                   tx: (($t.x - $minx) | floor), ty: (($t.y - $miny) | floor),
                   sd: ((.sourceHandle // "z")[0:1]),
                   td: ((.targetHandle // "y")[0:1]),
                   dashed: ((.data.edgeStyle // "solid") == "dashed")
                 } ]
    }' 2>/dev/null
}

if [ -s "$CACHE" ] && [ $(( $(date +%s) - $(stat -c %Y "$CACHE" 2>/dev/null || echo 0) )) -lt "$TTL" ]; then
    cat "$CACHE"; exit 0
fi

mkdir -p "$CACHE_DIR"
out=$(curl -sSL -m 25 "https://roadmap.sh/${slug}.json" 2>/dev/null | build)

if [ -n "$out" ] && [ "$(printf '%s' "$out" | jq -r '.nodes | length' 2>/dev/null)" -gt 0 ] 2>/dev/null; then
    printf '%s' "$out" > "$CACHE"; printf '%s' "$out"
elif [ -s "$CACHE" ]; then
    cat "$CACHE"
else
    printf '{"w":0,"h":0,"nodes":[],"edges":[]}'
fi
