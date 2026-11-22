-- hyprlua prelude: a stand-in for Hyprland's `hl` table. Running hyprland.lua against it records what the config
-- asks for (options, binds, rules, curves, animations, autostart) and __hl_dump() prints that as JSON for
-- Caelestia for Windows (services/HyprConf.qml). Names and argument shapes follow Hyprland's Lua API
-- (src/config/lua/bindings in hyprwm/Hyprland).

local R = { options = {}, binds = {}, execOnce = {}, rules = {}, workspaceRules = {}, layerRules = {}, curves = {},
            animations = {}, env = {}, monitors = {}, devices = {}, gestures = {}, submaps = {}, notes = {}, unbinds = {} }
local inStart = false

local function note(text, why) R.notes[#R.notes + 1] = { text = text, why = why } end

-- ---------------------------------------------------------------- dispatcher descriptors
-- hl.dsp.window.close() returns a value describing the call; it becomes the Lua call text the port's
-- Hyprland dispatcher understands ("hl.dsp.window.close()", "hl.dsp.focus({ workspace = 1 })").
local function lit(v)
  local t = type(v)
  if t == "string" then return string.format("%q", v) end
  if t == "number" or t == "boolean" then return tostring(v) end
  if t == "table" then
    local parts, n = {}, #v
    for i = 1, n do parts[#parts + 1] = lit(v[i]) end
    local keys = {}
    for k in pairs(v) do if not (type(k) == "number" and k >= 1 and k <= n and k % 1 == 0) then keys[#keys + 1] = k end end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    for _, k in ipairs(keys) do parts[#parts + 1] = tostring(k) .. " = " .. lit(v[k]) end
    return "{ " .. table.concat(parts, ", ") .. " }"
  end
  return "nil"
end

local DSP = {}
local function descriptor(name, args)
  local parts = {}
  for i = 1, args.n do parts[#parts + 1] = lit(args[i]) end
  return setmetatable({ __dsp = name, call = name .. "(" .. table.concat(parts, ", ") .. ")", args = args }, DSP)
end

local function namespace(prefix)
  return setmetatable({}, { __index = function(t, k)
    local name = prefix .. "." .. k
    local f = setmetatable({}, {
      __call = function(_, ...) return descriptor(name, table.pack(...)) end,
      __index = function(_, k2) return namespace(name)[k2] end })
    rawset(t, k, f)
    return f
  end })
end

-- ---------------------------------------------------------------- handles (rule:set_enabled(false) etc.)
local function handle(list, item)
  list[#list + 1] = item
  return setmetatable({}, { __index = function(_, k)
    if k == "set_enabled" then return function(_, on) item.enabled = on ~= false end end
    if k == "is_enabled" then return function() return item.enabled ~= false end end
    return function() return nil end
  end })
end

-- ---------------------------------------------------------------- options: hl.config({ general = { gaps_in = 5 } })
local function flatten(prefix, t)
  for k, v in pairs(t) do
    local key = prefix == "" and tostring(k) or (prefix .. ":" .. tostring(k))
    -- gradients and colour tables are values, not sections
    if type(v) == "table" and not (v.colors or v[1] ~= nil) then
      flatten(key, v)
    else
      if type(v) == "table" then
        local cols = v.colors or v
        local s = {}
        for i = 1, #cols do s[#s + 1] = tostring(cols[i]) end
        if v.angle then s[#s + 1] = tostring(v.angle) .. "deg" end
        v = table.concat(s, " ")
      elseif type(v) == "boolean" then v = v and "true" or "false"
      elseif type(v) == "number" then
        v = (math.type and math.type(v) == "integer" and v > 0xffffff) and string.format("0x%08x", v) or tostring(v)
      else v = tostring(v) end
      key = key:gsub(":col:", ":col."):gsub("^col:", "col.")
      R.options[key] = v
    end
  end
end

hl = {}
hl.dsp = namespace("hl.dsp")

function hl.config(t) if type(t) == "table" then flatten("", t) end end
function hl.get_config(key) return R.options[key] end
local currentSubmap = ""
local function keyId(k)
  local parts = {}
  for p in tostring(k):upper():gmatch("[^+%s]+") do parts[#parts + 1] = p end
  table.sort(parts)
  return table.concat(parts, "+")
end
function hl.bind(keys, action, opts)
  local b = { keys = tostring(keys), opts = opts or {}, submap = currentSubmap }
  if getmetatable(action) == DSP then
    b.call = action.call; b.dsp = action.__dsp; b.argv = {}
    for i = 1, action.args.n do b.argv[i] = action.args[i] end
  elseif type(action) == "function" then b.luaFunction = true
  else b.call = tostring(action) end
  return handle(R.binds, b)
end
-- like Hyprland: drops the binds on those keys made so far (in the submap being defined)
function hl.unbind(keys)
  local id = keyId(keys)
  for i = #R.binds, 1, -1 do
    if keyId(R.binds[i].keys) == id and (R.binds[i].submap or "") == currentSubmap then table.remove(R.binds, i) end
  end
end
function hl.define_submap(name, a, b)
  local fn = type(a) == "function" and a or b
  R.submaps[#R.submaps + 1] = name
  -- binds made inside fn belong to this submap
  local before = currentSubmap
  currentSubmap = tostring(name)
  if fn then local ok, e = pcall(fn); if not ok then note("submap " .. name, tostring(e)) end end
  currentSubmap = before
end
function hl.exec_cmd(cmd)
  if inStart then R.execOnce[#R.execOnce + 1] = tostring(cmd)
  else R.execOnce[#R.execOnce + 1] = tostring(cmd) end
end
function hl.on(event, fn)
  if event == "hyprland.start" and type(fn) == "function" then
    inStart = true
    local ok, e = pcall(fn)
    inStart = false
    if not ok then note("hl.on(hyprland.start)", tostring(e)) end
  else
    note("hl.on(" .. tostring(event) .. ")", "event handlers other than hyprland.start are not run yet")
  end
  return handle({}, {})
end
function hl.window_rule(t) return handle(R.rules, t or {}) end
function hl.layer_rule(t) return handle(R.layerRules, t or {}) end
function hl.workspace_rule(t) return handle(R.workspaceRules, t or {}) end
function hl.curve(name, t) R.curves[#R.curves + 1] = { name = name, def = t } end
function hl.animation(t) R.animations[#R.animations + 1] = t end
function hl.env(k, v) R.env[#R.env + 1] = { k = k, v = v } end
function hl.monitor(t) R.monitors[#R.monitors + 1] = t end
function hl.device(t) return handle(R.devices, t or {}) end
function hl.gesture(t) R.gestures[#R.gestures + 1] = t end
function hl.permission(...) note("hl.permission", "Windows has its own permissions") end
function hl.timer() return handle({}, {}) end
function hl.dispatch(d) note("hl.dispatch at load", "dispatchers only run from binds for now") end
function hl.version() return { tag = "caelestia-windows", branch = "windows" } end
hl.plugin = { load = function(p) note("hl.plugin.load(" .. tostring(p) .. ")", "Hyprland plugins are Linux .so files") end }
hl.notification = { create = function() return handle({}, {}) end, get = function() return {} end }
for _, q in ipairs({ "get_windows", "get_workspaces", "get_monitors", "get_layers", "get_devices", "get_workspace_windows",
                     "get_loaded_plugins" }) do hl[q] = function() return {} end end
for _, q in ipairs({ "get_window", "get_active_window", "get_urgent_window", "get_workspace", "get_active_workspace",
                     "get_active_special_workspace", "get_monitor", "get_active_monitor", "get_monitor_at",
                     "get_monitor_at_cursor", "get_cursor_pos", "get_last_window", "get_last_workspace",
                     "get_current_submap", "is_key_down", "get_scrolling_offset" }) do hl[q] = function() return nil end end
setmetatable(hl, { __index = function(_, k)
  return function() note("hl." .. tostring(k), "not in the Windows port yet") end
end })

-- ---------------------------------------------------------------- JSON
local function json(v)
  local t = type(v)
  if t == "nil" then return "null" end
  if t == "boolean" then return v and "true" or "false" end
  if t == "number" then return (v ~= v or v == math.huge or v == -math.huge) and "null" or tostring(v) end
  if t == "string" then
    return '"' .. v:gsub('[%c"\\]', function(c)
      local map = { ['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t' }
      return map[c] or string.format("\\u%04x", c:byte())
    end) .. '"'
  end
  if t == "function" then return '"<function>"' end
  if t == "table" then
    if getmetatable(v) == DSP then return json(v.call) end
    local n = #v
    local isArray = n > 0 or next(v) == nil
    if isArray then for k in pairs(v) do if type(k) ~= "number" then isArray = false break end end end
    local parts = {}
    if isArray then
      for i = 1, n do parts[#parts + 1] = json(v[i]) end
      return "[" .. table.concat(parts, ",") .. "]"
    end
    local keys = {}
    for k in pairs(v) do keys[#keys + 1] = tostring(k) end
    table.sort(keys)
    for _, k in ipairs(keys) do
      local val = v[k]; if val == nil then val = v[tonumber(k)] end
      parts[#parts + 1] = json(k) .. ":" .. json(val)
    end
    return "{" .. table.concat(parts, ",") .. "}"
  end
  return "null"
end

function __hl_dump(err)
  if err then R.error = tostring(err) end
  io.write(json(R), "\n")
end
