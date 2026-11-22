# Builds the Hyprland + niri port checklist from their sources (ref/hyprland, ref/niri).
# Writes dotfiles/port-inventory.json: every Hyprland option (name, type, description, default), every dispatcher
# (Lua hl.dsp.* and classic names), and niri's actions and config sections.
import json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.join(HERE, '..', 'ref')
out = {}

# ---- Hyprland options: MS<Type>("name", "description", default, ...)
src = open(os.path.join(REF, 'hyprland/src/config/values/ConfigValues.cpp'), encoding='utf-8').read()
opts = []
for m in re.finditer(r'MS<(\w+)>\(\s*"([^"]+)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*([^\n]*)', src):
    typ, name, desc, rest = m.groups()
    default = rest.split(', {')[0].rstrip('),').strip()
    opts.append({'name': name, 'type': typ, 'desc': desc, 'default': default})
out['hyprland_options'] = opts

# ---- Hyprland dispatchers (Lua API) and classic names still mentioned in the source
dsp = set()
for root, _, files in os.walk(os.path.join(REF, 'hyprland/src')):
    for f in files:
        if f.endswith(('.cpp', '.hpp')):
            t = open(os.path.join(root, f), encoding='utf-8', errors='ignore').read()
            dsp.update(re.findall(r'"(hl\.dsp\.[a-z_.]+)"', t))
lua = open(os.path.join(REF, 'hyprland/src/config/lua/bindings/LuaBindingsDispatchers.cpp'), encoding='utf-8').read()
# setDispatcherFn(L, "name", ...) lines, grouped by the lua_setfield(L, -2, "namespace") that closes each table
reg, buf = [], []
for m in re.finditer(r'setDispatcherFn\(L,\s*"([a-z_]+)"|lua_setfield\(L,\s*-2,\s*"([a-z_]+)"\)', lua):
    if m.group(1): buf.append(m.group(1)); continue
    ns = m.group(2)
    reg += ['hl.dsp.' + n for n in buf] if ns == 'dsp' else ['hl.dsp.%s.%s' % (ns, n) for n in buf]
    buf = []
reg = sorted(set(reg))
out['hyprland_dispatchers_lua'] = sorted(dsp)
out['hyprland_dispatcher_names'] = reg

# ---- Hyprland Lua top-level API (hl.config, hl.bind, hl.window_rule ...)
api = set()
for root, _, files in os.walk(os.path.join(REF, 'hyprland/src/config/lua')):
    for f in files:
        if f.endswith('.cpp'):
            t = open(os.path.join(root, f), encoding='utf-8', errors='ignore').read()
            api.update(re.findall(r'"(hl\.[a-z_]+)"', t))
            api.update('hl.' + x for x in re.findall(r'lua_setfield\(\s*L\s*,\s*-2\s*,\s*"([a-z_]+)"', t))
out['hyprland_lua_api'] = sorted(api)

# ---- niri actions (niri-ipc Action enum) and config sections
ipc = os.path.join(REF, 'niri/niri-ipc/src/lib.rs')
actions = []
if os.path.exists(ipc):
    t = open(ipc, encoding='utf-8').read()
    body = t[t.find('pub enum Action'):]
    body = body[:body.find('\n}\n')]
    actions = re.findall(r'^\s{4}([A-Z][A-Za-z]+)\b', body, re.M)
out['niri_actions'] = actions
sections = []
lib = os.path.join(REF, 'niri/niri-config/src/lib.rs')
if os.path.exists(lib):
    t = open(lib, encoding='utf-8').read()
    body = t[t.find('pub struct Config {'):]
    body = body[:body.find('\n}\n')]
    sections = re.findall(r'^\s+pub ([a-z_]+):', body, re.M)
out['niri_config_children'] = sections

json.dump(out, open(os.path.join(HERE, 'port-inventory.json'), 'w', encoding='utf-8'), indent=1)
by = {}
for o in opts: by[o['name'].split(':')[0]] = by.get(o['name'].split(':')[0], 0) + 1
print('hyprland options', len(opts), by)
print('lua dispatchers', len(out['hyprland_dispatchers_lua']), 'names', len(reg), 'lua api', len(api))
print('niri actions', len(actions), 'niri config children', len(sections))
