#include "StdAfx.h"
#include "pch_script.h"
#include "console_registrator.h"
#include "../xrEngine/XR_IOConsole.h"
#include "../xrEngine/xr_ioc_cmd.h"
#include "../xrScripts/script_engine.h"
#include "../xrCore/FormatParsers/XML/xrXMLParser.h"

using namespace luabind;

CConsole*	console()
{
	return Console;
}

int get_console_integer( CConsole* c, LPCSTR cmd )
{
	int min = 0, max = 0;
	int val = c->GetInteger ( cmd, min, max );
	return val;
}

float get_console_float( CConsole* c, LPCSTR cmd )
{
	float min = 0.0f, max = 0.0f;
	float val = c->GetFloat ( cmd, min, max );
	return val;
}

bool get_console_bool( CConsole* c, LPCSTR cmd )
{
	return c->GetBool( cmd );
}

void execute_console_command_deferred	(CConsole* c, LPCSTR string_to_execute)
{
	g_pEventManager->Event.Defer	("KERNEL:console", size_t(xr_strdup(string_to_execute)) );
}

luabind::object get_console_bounds(CConsole* c, LPCSTR cmd)
{
	luabind::object result = luabind::newtable(g_pScriptEngine->lua());
	IConsole_Command* command = c->GetCommand(cmd);
	if (auto* value = smart_cast<CCC_Float*>(command))
	{
		float minimum, maximum;
		value->GetBounds(minimum, maximum);
		result["min"] = minimum;
		result["max"] = maximum;
	}
	else if (auto* value = smart_cast<CCC_Integer*>(command))
	{
		int minimum, maximum;
		value->GetBounds(minimum, maximum);
		result["min"] = minimum;
		result["max"] = maximum;
	}
	return result;
}

luabind::object get_console_token_list(CConsole* c, LPCSTR cmd)
{
	luabind::object result = luabind::newtable(g_pScriptEngine->lua());
	xr_token* token = c->GetXRToken(cmd);
	for (int index = 1; token && token->name; ++token, ++index)
		result[index] = token->name;
	return result;
}

#pragma optimize("s",on)
void console_registrator::script_register(lua_State *L)
{
	module(L)
	[
		def("get_console",					&console),

		def("has_ixray_dxml_callback", &CXml::HasReadCallback),

		class_<CConsole>("CConsole")
			.def("get_variable_bounds", &get_console_bounds)
			.def("get_token_list", &get_console_token_list)
			.def("execute_deferred", &execute_console_command_deferred)
			.def("execute", &CConsole::Execute)
			.def("execute_script",			&CConsole::ExecuteScript)
			.def("show",					&CConsole::Show)
			.def("hide",					&CConsole::Hide)

			.def("get_string",				&CConsole::GetString)
			.def("get_integer",				&get_console_integer)
			.def("get_bool",				&get_console_bool)
			.def("get_float",				&get_console_float)
			.def("get_token",				&CConsole::GetToken)
	];
}
