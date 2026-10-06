local module = {}
local chunk = assert(loadfile("anthology_ixray_mcm.script"))
setmetatable(module, {__index = _G})
setfenv(chunk, module)
chunk()
assert(module.on_mcm_load() == nil, "must be absent on other engines")
module.has_ixray_dxml_callback = function() return true end
local console = {}
local current = {r_aa = "taa", rs_fps_limit = "143", r4_puddles = "off", r4_cas_sharpening = "0.37"}
function console:get_string(cmd) return current[cmd] end
function console:get_token_list(cmd)
    assert(cmd == "r_aa")
    return {"st_opt_off", "fxaa", "smaa", "taa"}
end
function console:get_variable_bounds(cmd)
    if cmd == "rs_fps_limit" then return {min = 0, max = 1000} end
    assert(cmd == "r4_cas_sharpening")
    return {min = 0, max = 1}
end
module.get_console = function() return console end
module.ui_mcm = setmetatable({}, {__index = function() error("MCM must not be read during discovery") end})
local tree = assert(module.on_mcm_load())
assert(tree.id == "anthology_ixray" and #tree.gr == 5, "hide unavailable commands")
local options = {}
for _, item in ipairs(tree.gr) do
    if item.cmd then
        assert(item.def == nil, "do not replace current settings with preset defaults")
        options[item.cmd] = item
    end
end
assert(options.r_aa.content[4][1] == "taa" and options.r_aa.restart)
assert(options.rs_fps_limit.max == 1000 and options.rs_fps_limit.val == 2)
assert(options.r4_cas_sharpening.step == 0.01 and options.r4_cas_sharpening.max == 1)
assert(options.r4_puddles.val == 1 and options.r4_puddles.restart)
current = {}
assert(module.on_mcm_load() == nil, "hide empty renderer menu")
