#pragma once

#include <luabind/luabind.hpp>

// Preserve Lua's number-to-text conversion without accepting arbitrary objects.
template<class T>
static void set_lua_text(T* self, const luabind::object& value)
{
    lua_State* L = value.lua_state();
    value.pushvalue();
    if (!lua_isnil(L, -1) && !lua_isstring(L, -1))
    {
        lua_pop(L, 1);
        luaL_error(L, "SetText expects text, a number, or nil");
        return;
    }
    const char* text = lua_tostring(L, -1);
    self->SetText(text ? text : "");
    lua_pop(L, 1);
}
