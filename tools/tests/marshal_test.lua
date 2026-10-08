local m = require('marshal')
local shared = {v = 12}
local source = {a = shared, b = shared}
source.self = source
local result = m.decode(m.encode(source))
assert(result.a == result.b and result.self == result and result.a.v == 12)

local value = 37
local closure = function(delta) return value + delta end
assert(m.decode(m.encode(closure))(5) == 42)

local nested = {leaf = true}
for i = 1, 80 do nested = {child = nested} end
local decoded = m.decode(m.encode(nested))
for i = 1, 80 do decoded = decoded.child end
assert(decoded.leaf)

-- Anomaly saves contain LuaJIT bytecode v1; native IX-Ray uses v2.
-- An incompatible function must report an error before applying upvalues.
local incompatible = string.char(142, 6, 2, 6, 0, 0, 0, 27, 76, 74, 1, 0, 0, 0, 0, 0, 0)
local ok, message = pcall(m.decode, incompatible)
assert(not ok and tostring(message):find('incompatible bytecode', 1, true))

local encoded = m.encode({first = {second = 'abc'}, number = 34})
for size = 1, #encoded - 1 do
    assert(not pcall(m.decode, encoded:sub(1, size)))
end
assert(m.decode(encoded).first.second == 'abc')
