#include "stdafx.h"
#include "pch_script.h"
#include "AnomalyScriptDebug.h"
#include "Level.h"
#include "debug_renderer.h"

#ifdef DEBUG_DRAW
void DBG_ScriptSphere::Render() { Level().debug_renderer().draw_ellipse(matrix, color.get()); }
void DBG_ScriptBox::Render() { Level().debug_renderer().draw_obb(matrix, size, color.get()); }
void DBG_ScriptLine::Render() { Level().debug_renderer().draw_line(Fidentity, point_a, point_b, color.get()); }

void CLevel::ClearScriptDebug()
{
    for (auto& entry : scriptDebugObjects) xr_delete(entry.second);
    scriptDebugObjects.clear();
}

void CLevel::RenderScriptDebug()
{
    if (!m_debug_renderer) return;
    for (const auto& entry : scriptDebugObjects)
        if (entry.second->visible) entry.second->Render();
}

namespace
{
DBG_ScriptObject* get_object(LPCSTR name)
{
    if (!g_pGameLevel || !name) return nullptr;
    auto& queue = Level().scriptDebugObjects;
    auto it = queue.find(shared_str(name));
    return it == queue.end() ? nullptr : it->second;
}

void remove_object(LPCSTR name)
{
    if (!g_pGameLevel || !name) return;
    auto& queue = Level().scriptDebugObjects;
    auto it = queue.find(shared_str(name));
    if (it == queue.end()) return;
    xr_delete(it->second);
    queue.erase(it);
}

DBG_ScriptObject* add_object(LPCSTR name, DebugRenderType type)
{
    if (!g_pGameLevel || !name) return nullptr;
    if (type < eDBGLine || type > eDBGBox) return nullptr;
    remove_object(name);
    auto& queue = Level().scriptDebugObjects;
    // Prevent an erroneous addon from growing a persistent render queue forever.
    if (queue.size() >= 4096) return nullptr;
    DBG_ScriptObject* object = nullptr;
    switch (type)
    {
    case eDBGLine: object = new DBG_ScriptLine(); break;
    case eDBGSphere: object = new DBG_ScriptSphere(); break;
    case eDBGBox: object = new DBG_ScriptBox(); break;
    }
    queue.emplace(shared_str(name), object);
    return object;
}

DBG_ScriptObject* get_object_id(u32 id) { return get_object(std::to_string(id).c_str()); }
void remove_object_id(u32 id) { remove_object(std::to_string(id).c_str()); }
DBG_ScriptObject* add_object_id(u32 id, DebugRenderType type) { return add_object(std::to_string(id).c_str(), type); }
}

void RegisterAnomalyScriptDebug(lua_State* L)
{
    using namespace luabind;
    module(L)
    [
        class_<DBG_ScriptObject>("DBG_ScriptObject")
            .enum_("dbg_type")[value("line", int(eDBGLine)), value("sphere", int(eDBGSphere)), value("box", int(eDBGBox))]
            .def("cast_dbg_sphere", &DBG_ScriptObject::cast_dbg_sphere)
            .def("cast_dbg_box", &DBG_ScriptObject::cast_dbg_box)
            .def("cast_dbg_line", &DBG_ScriptObject::cast_dbg_line)
            .def_readwrite("color", &DBG_ScriptObject::color)
            .def_readwrite("visible", &DBG_ScriptObject::visible),
        class_<DBG_ScriptSphere, DBG_ScriptObject>("DBG_ScriptSphere")
            .def_readwrite("matrix", &DBG_ScriptSphere::matrix),
        class_<DBG_ScriptBox, DBG_ScriptObject>("DBG_ScriptBox")
            .def_readwrite("matrix", &DBG_ScriptBox::matrix)
            .def_readwrite("size", &DBG_ScriptBox::size),
        class_<DBG_ScriptLine, DBG_ScriptObject>("DBG_ScriptLine")
            .def_readwrite("point_a", &DBG_ScriptLine::point_a)
            .def_readwrite("point_b", &DBG_ScriptLine::point_b)
    ];
    module(L, "debug_render")
    [
        def("get_object", &get_object), def("get_object", &get_object_id),
        def("remove_object", &remove_object), def("remove_object", &remove_object_id),
        def("add_object", &add_object), def("add_object", &add_object_id)
    ];
}
#endif
