#include "stdafx.h"
#include "UIStatic.h"
#include "UIAnimatedStatic.h"

#include <luabind/luabind.hpp>

using namespace luabind;

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


#pragma optimize("s",on)

void CUIStatic::script_register(lua_State *L)
{
	module(L)
	[
		class_<CUILines>("CUILines")
		.def("SetFont",				&CUILines::SetFont)
		.def("SetText",				&CUILines::SetText)
        .def("SetText", &set_lua_text<CUILines>)
		.def("SetTextST",			&CUILines::SetTextST)
		.def("GetText",				&CUILines::GetText)
		.def("SetElipsis",			&CUILines::SetEllipsis)
		.def("SetTextColor",		&CUILines::SetTextColor),


		class_<CUIStatic, CUIWindow>("CUIStatic")
		.def(						constructor<>())
		.def("SetTextureColor", 	&CUIStatic::SetTextureColor)
		.def("GetTextureColor", 	&CUIStatic::GetTextureColor)
		.def("AdjustHeightToText", 	&CUIStatic::AdjustHeightToText)
		.def("AdjustWidthToText", 	&CUIStatic::AdjustWidthToText)
		.def("GetStretchTexture", 	&CUIStatic::GetStretchTexture)														  
		.def("TextControl",			&CUIStatic::TextItemControl)
			
       .def("SetText",				(void (CUIStatic::*)(LPCSTR)) (&CUIStatic::SetText))
        .def("SetText", &set_lua_text<CUIStatic>)
       .def("SetTextST",			(void (CUIStatic::*)(LPCSTR)) (&CUIStatic::SetTextST))

       .def("GetText",				&CUIStatic::GetText)

       .def("SetTextX",				&CUIStatic::SetTextX)
       .def("SetTextY",				&CUIStatic::SetTextY)
	   .def("GetTextX",				&CUIStatic::GetTextX)
       .def("GetTextY",				&CUIStatic::GetTextY)

       .def("SetColor",				&CUIStatic::SetTextureColor)
       .def("GetColor",				&CUIStatic::GetTextureColor)

        .def("SetTextColor",		&CUIStatic::SetTextColor_script)

        .def("InitTexture",			&CUIStatic::InitTexture)
        .def("InitTexture", +[](CUIStatic* self, pcstr texture, pcstr) { self->InitTexture(texture, true); })
        .def("InitTexture",			+[](CUIStatic* self, pcstr texture) { self->InitTexture(texture); })
        .def("InitTextureEx",		&CUIStatic::InitTextureEx)
        .def("InitTextureEx",		+[](CUIStatic* self, pcstr texture, pcstr shader) { self->InitTextureEx(texture, shader); })
        .def("ResetOriginalRect", &CUIStatic::ResetOriginalRect)

		.def("SetTextureOffset",	&CUIStatic::SetTextureOffset)

		.def("SetTextureRect",		&CUIStatic::SetTextureRect_script)
		.def("GetTextureRect",		&CUIStatic::GetTextureRect_script)

        .def("SetOriginalRect",		&CUIStatic::SetTextureRect_script)
        .def("GetOriginalRect",		&CUIStatic::GetTextureRect_script)

		.def("SetStretchTexture",	&CUIStatic::SetStretchTexture)
        .def("GetStretchTexture",	&CUIStatic::GetStretchTexture)

        .def("SetTextAlign",		&CUIStatic::SetTextAlign_script)
        .def("GetTextAlign",		&CUIStatic::GetTextAlign_script)

        .def("EnableHeading", &CUIStatic::EnableHeading)
        .def("Heading", &CUIStatic::Heading)
        .def("SetConstHeading", &CUIStatic::SetConstHeading)
        .def("SetHeading",			&CUIStatic::SetHeading)
        .def("GetHeading",			&CUIStatic::GetHeading)

        .def("SetElipsis",			&CUIStatic::SetEllipsis),

		class_<CUITextWnd, CUIWindow>("CUITextWnd")
		.def(						constructor<>())
		.def("AdjustHeightToText",	&CUITextWnd::AdjustHeightToText)
		.def("AdjustWidthToText",	&CUITextWnd::AdjustWidthToText)
		.def("SetText",				&CUITextWnd::SetText)
        .def("SetText", &set_lua_text<CUITextWnd>)
		.def("SetTextST",			&CUITextWnd::SetTextST)
		.def("GetText",				&CUITextWnd::GetText)
		.def("SetFont",				&CUITextWnd::SetFont)
		.def("GetFont",				&CUITextWnd::GetFont)
		.def("SetTextColor",		&CUITextWnd::SetTextColor)
		.def("GetTextColor",		&CUITextWnd::GetTextColor)
		.def("SetTextComplexMode",	&CUITextWnd::SetTextComplexMode)
		.def("SetTextAlignment",	&CUITextWnd::SetTextAlignment)
		.def("SetVTextAlignment",	&CUITextWnd::SetVTextAlignment)
		.def("SetEllipsis",			&CUITextWnd::SetEllipsis)
		.def("SetTextOffset",		&CUITextWnd::SetTextOffset),
//		.def("",					&CUITextWnd::)

		class_<CUISleepStatic, CUIStatic>("CUISleepStatic")
		.def(						constructor<>())
	];
}