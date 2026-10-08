#include "StdAfx.h"
#include "pch_script.h"
#include "UIGameCustom.h"
#include "Level.h"
#include "../../xrUI/Widgets/uistatic.h"
#include "../../xrUI/Widgets/UIDialogHolder.h"
#include "../../xrUI/Widgets/UIDialogWnd.h"

using namespace luabind;

static SDrawStaticStruct* get_optional_static(CUIGameCustom* self, const luabind::object& id)
{
    lua_State* L = id.lua_state();
    id.pushvalue();
    if (!lua_isnil(L, -1) && lua_type(L, -1) != LUA_TSTRING)
    {
        lua_pop(L, 1);
        luaL_error(L, "GetCustomStatic expects a name or nil");
        return nullptr;
    }
    const char* name = lua_tostring(L, -1);
    auto* result = name ? self->GetCustomStatic(name) : nullptr;
    lua_pop(L, 1);
    return result;
}


CUIGameCustom* get_hud(){
	return CurrentGameUI();
}

#pragma optimize("s",on)
void CUIGameCustom::script_register(lua_State *L)
{
	module(L)
		[
			class_< SDrawStaticStruct >("SDrawStaticStruct")
			.def_readwrite("m_endTime",		&SDrawStaticStruct::m_endTime)
			.def("wnd",					&SDrawStaticStruct::wnd),

			class_<CUIGameCustom, CDialogHolder>("CUIGameCustom")
			.def("TopInputReceiver", 		&CUIGameCustom::TopInputReceiver)
			.def("SetMainInputReceiver",	&CUIGameCustom::SetMainInputReceiver)
			.def("AddDialogToRender",		&CUIGameCustom::AddDialogToRender)
			.def("RemoveDialogToRender",	&CUIGameCustom::RemoveDialogToRender)
			.def("AddCustomStatic",			+[](CUIGameCustom* self, pcstr id, bool singleInstance)
            {
                return self->AddCustomStatic(id, singleInstance);
            })
            .def("AddCustomStatic", +[](CUIGameCustom* self, pcstr id) { return self->AddCustomStatic(id, false); })
			.def("AddCustomStatic",			&CUIGameCustom::AddCustomStatic)
			.def("AddHudMessage",			&CUIGameCustom::AddHudMessage)
			.def("RemoveCustomStatic",		&CUIGameCustom::RemoveCustomStatic)
			.def("HideActorMenu",			&CUIGameCustom::HideActorMenu)
			//Alundaio
			.def("ShowActorMenu",			&CUIGameCustom::ShowActorMenu)
			.def("UpdateActorMenu",			&CUIGameCustom::UpdateActorMenu)
			.def("CurrentItemAtCell",		&CUIGameCustom::CurrentItemAtCell)
			//-Alundaio
			.def("HidePdaMenu",				&CUIGameCustom::HidePdaMenu)
			.def("show_messages",			&CUIGameCustom::ShowMessagesWindow)
			.def("hide_messages",			&CUIGameCustom::HideMessagesWindow)
			.def("GetCustomStatic",			&CUIGameCustom::GetCustomStatic)
            .def("GetCustomStatic", &get_optional_static)
			.def("update_fake_indicators",	&CUIGameCustom::update_fake_indicators)
			.def("enable_fake_indicators",	&CUIGameCustom::enable_fake_indicators),
			def("get_hud",					&get_hud)
		];
}
