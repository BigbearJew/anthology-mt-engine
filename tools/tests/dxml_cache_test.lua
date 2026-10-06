-- Exercise the real shipped dxml_core and SLAXML, with no game addons loaded.
RegisterScriptCallback = function() end
UnregisterScriptCallback = function() end
AddScriptCallback = function() end
printf_me = function() end
getFS = function()
    return { file_list_open_ex = function() return {Size = function() return 0 end} end }
end
FS = {FS_ListFiles = 1, FS_RootOnly = 2}
k2t_table = function(t) for k in pairs(t) do t[k] = nil end end
try = function(fn, ...) return fn(...) end
trim = string.trim
-- These fixtures have no attributes; the parser still calls the split helper.
str_explode_lim = function(s, separator)
    assert(not s:find(separator, 1, true), 'fixture unexpectedly contains attributes')
    return {s}
end
str_explode = str_explode_lim

local function load_module(file)
    local fn = assert(loadfile(file .. '.script'))
    local env = setmetatable({}, {__index = _G})
    setfenv(fn, env)
    fn()
    return env
end
slaxml = load_module('slaxml')
local dxml = load_module('dxml_core')
local calls = 0
RegisterScriptCallback('on_xml_read', function(name, xml, flags)
    calls = calls + 1
    flags.cache = name == 'ui/cached.xml'
end)
local first = COnXmlRead('ui/cached.xml', '<root><value>1</value></root>')
assert(calls == 1 and first:find('value'))
assert(COnXmlRead('ui/cached.xml', '<root/>') == first and calls == 1)
COnXmlRead('ui/live.xml', '<root/>')
COnXmlRead('ui/live.xml', '<root/>')
assert(calls == 3)

local parser = slaxml.SLAXML()
local legacy = '<?xml version="1.0"?><root><?xml version="1.0"?><value>1</value></root>'
local serialized = parser:simple_xml(parser:simple_dom(legacy))
assert(serialized == '<?xml version="1.0"?><root><value>1</value></root>')
