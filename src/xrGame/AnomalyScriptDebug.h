#pragma once

#ifdef DEBUG_DRAW
struct lua_State;
class DBG_ScriptSphere;
class DBG_ScriptBox;
class DBG_ScriptLine;

enum DebugRenderType { eDBGLine, eDBGSphere, eDBGBox };

class DBG_ScriptObject
{
public:
    Fcolor color;
    bool visible = true;
    DBG_ScriptObject() { color.set(1.f, 0.f, 0.f, 1.f); }
    virtual ~DBG_ScriptObject() = default;
    virtual DBG_ScriptSphere* cast_dbg_sphere() { return nullptr; }
    virtual DBG_ScriptBox* cast_dbg_box() { return nullptr; }
    virtual DBG_ScriptLine* cast_dbg_line() { return nullptr; }
    virtual void Render() = 0;
};

class DBG_ScriptSphere final : public DBG_ScriptObject
{
public:
    Fmatrix matrix = Fidentity;
    DBG_ScriptSphere* cast_dbg_sphere() override { return this; }
    void Render() override;
};

class DBG_ScriptBox final : public DBG_ScriptObject
{
public:
    Fmatrix matrix = Fidentity;
    Fvector size = {1.f, 1.f, 1.f};
    DBG_ScriptBox* cast_dbg_box() override { return this; }
    void Render() override;
};

class DBG_ScriptLine final : public DBG_ScriptObject
{
public:
    Fvector point_a = {}, point_b = {};
    DBG_ScriptLine* cast_dbg_line() override { return this; }
    void Render() override;
};

void RegisterAnomalyScriptDebug(lua_State* L);
#endif
