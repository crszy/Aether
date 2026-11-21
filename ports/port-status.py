# Writes dotfiles/PORT-STATUS.md: every Hyprland option / dispatcher and niri action / config section with its port status.
#   done     wired into Caelestia for Windows (HyprConf.qml -> the port's engine)
#   partial  read or approximated (Windows can only do part of it)
#   todo     not yet
#   n/a      cannot exist on Windows (Linux graphics stack, VTs, XWayland ...)
# Edit STATUS below as items get done, then run: py -I port-status.py
import json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
inv = json.load(open(os.path.join(HERE, 'port-inventory.json'), encoding='utf-8'))

STATUS = {
    # ---- Hyprland options
    'general:layout': 'done', 'general:gaps_in': 'done', 'general:gaps_out': 'done', 'general:border_size': 'partial',
    'general:col.active_border': 'partial', 'general:col.inactive_border': 'done', 'decoration:rounding': 'partial',
    'dwindle:preserve_split': 'done', 'dwindle:force_split': 'done', 'dwindle:smart_split': 'done',
    'dwindle:default_split_ratio': 'done', 'master:mfact': 'done', 'master:new_status': 'done', 'master:new_on_top': 'done',
    'master:orientation': 'done', 'scrolling:column_width': 'done', 'scrolling:explicit_column_widths': 'done',
    'scrolling:focus_fit_method': 'partial', 'binds:workspace_back_and_forth': 'done',
    'cursor:warp_on_change_workspace': 'done', 'animations:enabled': 'done', 'input:follow_mouse': 'done',
    # ---- Hyprland dispatchers the port's dispatcher already runs
    'hl.dsp.focus': 'done', 'hl.dsp.window.move': 'done', 'hl.dsp.window.close': 'done', 'hl.dsp.window.kill': 'done',
    'hl.dsp.window.pin': 'done', 'hl.dsp.window.fullscreen': 'done', 'hl.dsp.global': 'done', 'hl.dsp.exec_cmd': 'done',
    'hl.dsp.window.float': 'done', 'hl.dsp.window.swap': 'done', 'hl.dsp.window.resize': 'done', 'hl.dsp.window.center': 'done',
    'hl.dsp.layout': 'done', 'hl.dsp.workspace.toggle_special': 'done', 'hl.dsp.window.drag': 'partial',
    'hl.dsp.window.pseudo': 'partial', 'hl.dsp.window.cycle_next': 'done', 'hl.dsp.submap': 'done',
    'hl.dsp.group.toggle': 'partial', 'hl.dsp.reload_config': 'done', 'hl.dsp.no_op': 'done',
    'hl.dsp.workspace.move': 'n/a', 'hl.dsp.workspace.swap_monitors': 'n/a', 'hl.dsp.workspace.show': 'done',
    'hl.dsp.window.fullscreen_state': 'done', 'hl.dsp.exec_raw': 'done', 'hl.dsp.cursor.move': 'done',
    'hl.dsp.cursor.move_to_corner': 'done', 'hl.dsp.window.bring_to_top': 'done', 'hl.dsp.window.alter_zorder': 'done',
    'hl.dsp.send_shortcut': 'done', 'hl.dsp.window.signal': 'n/a',
    'hl.dsp.exit': 'n/a', 'hl.dsp.dpms': 'todo', 'hl.dsp.force_renderer_reload': 'n/a', 'hl.dsp.release_input_capture': 'n/a',
    # ---- niri
    'Quit': 'n/a', 'ChangeVt': 'n/a', 'ToggleDebugTint': 'n/a', 'DebugToggleOpaqueRegions': 'n/a', 'DebugToggleDamage': 'n/a',
    'SetDynamicCastWindow': 'n/a', 'SetDynamicCastMonitor': 'n/a', 'ClearDynamicCastTarget': 'n/a', 'StopCast': 'n/a',
    'Spawn': 'done', 'SpawnSh': 'done', 'CloseWindow': 'done', 'FullscreenWindow': 'done', 'FocusColumnLeft': 'done',
    'FocusColumnRight': 'done', 'FocusWindowUp': 'done', 'FocusWindowDown': 'done', 'ToggleWindowFloating': 'done',
    'FocusWorkspace': 'done', 'MoveWindowToWorkspace': 'done', 'SwitchPresetColumnWidth': 'partial', 'CenterColumn': 'partial',
    'ToggleOverview': 'partial', 'OpenOverview': 'partial', 'CloseOverview': 'partial',
}
NA_PREFIX = ['xwayland:', 'opengl:', 'render:', 'debug:', 'ecosystem:', 'experimental:', 'quirks:', 'input-capture:',
             'input:tablet', 'input:tablettool', 'input:touchdevice', 'input:virtualkeyboard', 'misc:vrr', 'misc:vfr',
             'cursor:no_hardware_cursors', 'cursor:use_cpu_buffer', 'cursor:sync_gsettings_theme', 'cursor:enable_hyprcursor']
NIRI_NA = ['xwayland_satellite', 'debug', 'switch_events', 'environment']

# every niri action the config.kdl reader translates (caelestia-win/services/niri.js) runs on the port's engine
NIRI_JS = open(os.path.join(HERE, 'caelestia-win', 'services', 'niri.js'), encoding='utf-8').read()
for kebab, body in re.findall(r'"([a-z-]+)": \(\) => (.{0,40})', NIRI_JS):
    camel = ''.join(p.capitalize() for p in kebab.split('-'))
    STATUS.setdefault(camel, 'n/a' if 'problem' in body else 'done')
# config.kdl sections it reads
STATUS.update({'niri:binds': 'done', 'niri:spawn_at_startup': 'done', 'niri:spawn_sh_at_startup': 'done',
               'niri:layout': 'partial', 'niri:window_rules': 'partial', 'niri:animations': 'partial',
               'niri:input': 'n/a', 'niri:outputs': 'n/a', 'niri:cursor': 'n/a', 'niri:prefer_no_csd': 'n/a'})

def st(name):
    if name in STATUS: return STATUS[name]
    if any(name.startswith(p) for p in NA_PREFIX): return 'n/a'
    return 'todo'

rows, count = [], {}
def add(group, name, desc=''):
    s = st(name)
    count[s] = count.get(s, 0) + 1
    rows.append((group, name, s, desc))

for o in inv['hyprland_options']:
    add('Hyprland option: ' + o['name'].split(':')[0], o['name'], o['desc'])
for d in inv['hyprland_dispatcher_names']:
    add('Hyprland dispatcher', d)
for a in inv['niri_actions']:
    add('niri action', a)
LUA_API = [('hl.config', 'done'), ('hl.get_config', 'done'), ('hl.bind', 'done'), ('hl.unbind', 'done'),
           ('hl.define_submap', 'done'), ('hl.exec_cmd', 'done'), ('hl.on("hyprland.start")', 'done'),
           ('hl.on(other events)', 'todo'), ('hl.window_rule', 'done'), ('hl.layer_rule', 'todo'),
           ('hl.workspace_rule', 'todo'), ('hl.curve', 'partial'), ('hl.animation', 'partial'), ('hl.env', 'n/a'),
           ('hl.monitor', 'n/a'), ('hl.device', 'n/a'), ('hl.gesture', 'todo'), ('hl.permission', 'n/a'),
           ('hl.timer', 'todo'), ('hl.dispatch', 'todo'), ('hl.plugin.load', 'n/a'), ('hl.notification', 'todo'),
           ('hl.get_* queries (windows, workspaces, monitors...)', 'todo'), ('require()', 'done'),
           ('binds that run a Lua function', 'todo')]
for n, s in LUA_API:
    count[s] = count.get(s, 0) + 1
    rows.append(('Hyprland Lua API (hyprland.lua)', n, s, ''))
for c in inv['niri_config_children']:
    s = 'n/a' if c in NIRI_NA else STATUS.get('niri:' + c, 'todo')
    count[s] = count.get(s, 0) + 1
    rows.append(('niri config', c, s, ''))

mark = {'done': '[x]', 'partial': '[~]', 'todo': '[ ]', 'n/a': '[-]'}
lines = ['# Hyprland + niri port status', '',
         'Generated by `port-status.py` from the Hyprland and niri sources in `ref/` (`port-inventory.py`).',
         '[x] done  [~] partial  [ ] todo  [-] not possible on Windows', '',
         'Totals: ' + ', '.join('%s %d' % (k, v) for k, v in sorted(count.items())), '']
group = None
for g, n, s, d in rows:
    if g != group:
        lines += ['', '## ' + g, '']
        group = g
    lines.append('- %s `%s`%s' % (mark[s], n, (' - ' + d.replace('\\"', '"')) if d else ''))
open(os.path.join(HERE, 'PORT-STATUS.md'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
print(count, len(rows))
