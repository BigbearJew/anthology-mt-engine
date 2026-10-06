#pragma once

// The core XML parser remains usable without a game or a Lua VM.
inline bool TransformXmlFromLua(lua_State* state, LPCSTR filename, LPCSTR source, xr_string& result)
{
    if (!state || !source || !*source)
        return false;
    const int top = lua_gettop(state);
    // Do not trigger script autoload while XML or the VM itself is initializing.
    lua_pushliteral(state, "COnXmlRead");
    lua_rawget(state, LUA_GLOBALSINDEX);
    if (!lua_isfunction(state, -1))
    {
        lua_settop(state, top);
        return false;
    }
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(source);
    if (strlen(source) >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
        source += 3;
    lua_pushstring(state, filename);
    lua_pushstring(state, source);
    const int status = lua_pcall(state, 2, 1, 0);
    const bool valid = status == 0 && lua_type(state, -1) == LUA_TSTRING;
    if (valid)
        result = lua_tostring(state, -1);
    else if (status != 0)
        Msg("! DXML: %s: %s", filename, lua_tostring(state, -1) ? lua_tostring(state, -1) : "Lua callback error");
    else
        Msg("! DXML: %s: callback returned no XML string", filename);
    lua_settop(state, top);
    return valid;
}
