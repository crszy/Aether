#!/usr/bin/env python3
"""Per-application output streams (name, volume, mute), as JSON, for the
volume panel's app mixer. Read from pw-dump, so it needs no pulse layer."""
import json
import subprocess
import sys

try:
    data = json.loads(subprocess.check_output(["pw-dump"], stderr=subprocess.DEVNULL))
except Exception:
    print("[]")
    sys.exit(0)

result = []
for obj in data:
    if obj.get("type") != "PipeWire:Interface:Node":
        continue
    info = obj.get("info", {})
    props = info.get("props", {})
    if props.get("media.class") != "Stream/Output/Audio":
        continue
    vol_pct, muted = 100, False
    for p in info.get("params", {}).get("Props", []):
        vols = p.get("channelVolumes")
        if vols:
            # PipeWire volumes are cubic; wpctl shows the cube root.
            vol_pct = int(round(max(vols) ** (1 / 3) * 100))
        if "mute" in p:
            muted = bool(p["mute"])
    result.append({
        "index": obj["id"],
        "properties": {"application.name": props.get("application.name") or props.get("media.name") or "App"},
        "volume": {"front-left": {"value_percent": "%d%%" % vol_pct}},
        "mute": muted,
    })

print(json.dumps(result))
