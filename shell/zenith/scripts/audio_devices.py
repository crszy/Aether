#!/usr/bin/env python3
"""Audio sinks and sources with their default flags, as JSON, for VolumeService.

Parses `wpctl status`. A Bluetooth headset microphone is listed by PipeWire
under Filters rather than Sources, so it is picked up from the [Audio/Source]
annotation when no Bluetooth source was found the normal way.
"""
import subprocess, re, json

def get_desc(dev_id):
    try:
        out = subprocess.check_output(['wpctl', 'inspect', str(dev_id)], text=True, timeout=1)
        for line in out.splitlines():
            if 'node.description' in line:
                m = re.search(r'node\.description\s*=\s*"(.*)"', line)
                if m: return m.group(1)
    except: pass
    return ''

def get_devices():
    try: out = subprocess.check_output(['wpctl', 'status'], text=True, timeout=2)
    except Exception: out = ''

    sinks, sources = [], []
    active_sink, active_source = -1, -1

    in_sinks = False
    in_sources = False

    for line in out.splitlines():
        if 'Sinks:' in line:
            in_sinks = True
            in_sources = False
            continue
        elif 'Sources:' in line:
            in_sources = True
            in_sinks = False
            continue
        elif any(k in line for k in ['Filters:', 'Streams:', 'Settings', 'Video']):
            in_sinks = False
            in_sources = False

        m = re.search(r'(\*?\s*)(\d+)\.\s+(.*?)\s*(\[|$)', line)
        if m and (in_sinks or in_sources):
            is_def = '*' in m.group(1)
            dev_id = int(m.group(2))
            dev_name = m.group(3).strip()

            if not dev_name or dev_name == '(null)' or 'camera' in dev_name.lower():
                continue

            if dev_name.startswith('Built-in Audio'):
                dev_name = 'Built-in Microphone' if in_sources else 'Built-in Speaker'
            elif dev_name.startswith('bluez_'):
                real_desc = get_desc(dev_id)
                if real_desc: dev_name = real_desc

            item = {'id': dev_id, 'name': dev_name, 'isDefault': is_def}
            if in_sinks:
                sinks.append(item)
                if is_def: active_sink = dev_id
            elif in_sources:
                sources.append(item)
                if is_def: active_source = dev_id

    if not any('bluez' in s['name'].lower() or 'bluetooth' in s['name'].lower() or 'yopod' in s['name'].lower() for s in sources):
        for line in out.splitlines():
            if '[Audio/Source]' in line and ('bluez' in line or 'bluetooth' in line):
                m = re.search(r'(\*?\s*)(\d+)\.\s+(.*?)\s*\[Audio/Source\]', line)
                if m:
                    is_def = '*' in m.group(1)
                    dev_id = int(m.group(2))
                    real_desc = get_desc(dev_id)
                    dev_name = real_desc if real_desc else 'Bluetooth Microphone'
                    item = {'id': dev_id, 'name': dev_name, 'isDefault': is_def}
                    sources.append(item)
                    if is_def: active_source = dev_id

    return {'sinks': sinks, 'sources': sources, 'activeSinkId': active_sink, 'activeSourceId': active_source}

print(json.dumps(get_devices()))
