#include "stdafx.h"
#include "UIProgressBar.h"
#include <luabind/luabind.hpp>

using namespace luabind;
namespace { void SetProgressColor(CUIProgressBar* bar, u32 color) { bar->m_UIProgressItem.SetTextureColor(color); } }

#pragma optimize("s",on)
void CUIProgressBar::script_register(lua_State *L)
{
	module(L)
	[
		class_<CUIProgressBar, CUIWindow>("CUIProgressBar")
		.def(						constructor<>())
		.def("SetColor", &SetProgressColor)
		.def("SetRange", &CUIProgressBar::SetRange)
		.def("SetProgressPos",			&CUIProgressBar::SetProgressPos)
		.def("GetProgressPos",			&CUIProgressBar::GetProgressPos)
		.def("ShowBackground", &CUIProgressBar::ShowBackground)
		.def("IsShownBackground", &CUIProgressBar::IsShownBackground)

		.def("GetRange_min",			&CUIProgressBar::GetRange_min)
		.def("GetRange_max",			&CUIProgressBar::GetRange_max)

	];
}
