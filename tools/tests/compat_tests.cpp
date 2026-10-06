#include "xrCore/xrCore.h"
#include "xrEngine/ConfigSun.h"
#include "xrScripts/linker.h"
#include "xrScripts/script_storage.h"
#include "xrScripts/dxml_bridge.h"
#include "xrCore/FormatParsers/XML/xrXMLParser.h"
#include <fstream>

static void check(bool success, const char* label)
{
    if (!success)
    {
        fprintf(stderr, "FAIL: %s\n", label);
        exit(1);
    }
    printf("PASS: %s\n", label);
}

class TestScriptStorage : public CScriptStorage
{
public:
    using CScriptStorage::reinit;
    void on_error(lua_State*) override { check(false, "unexpected script engine error"); }
};

static lua_State* xml_test_state = nullptr;
static bool transform_xml(LPCSTR name, LPCSTR source, xr_string& output)
{
    return TransformXmlFromLua(xml_test_state, name, source, output);
}

template<class Test>
static void with_ini(const char* text, Test test)
{
    IReader reader(const_cast<char*>(text), strlen(text));
    CInifile ini(&reader);
    test(ini);
}

int main(int argc, char** argv)
{
    const bool vfs = argc > 1 && strcmp(argv[1], "--vfs") == 0;
    const bool lua_test = argc > 1 && strcmp(argv[1], "--lua") == 0;
    Core._initialize("AnthologyIXRayTests", nullptr, vfs || lua_test,
        vfs ? "fsgame_ixray.ltx" : lua_test ? "fsgame_test.ltx" : nullptr);
    with_ini("@[new]\nvalue=17\n", [](const CInifile& ini) {
        check(ini.section_exist("new") && ini.r_u32("new", "value") == 17, "safe override creates section");
    });
    with_ini("[base]\na=1\nb=2\n@[base]\na=3\n", [](const CInifile& ini) {
        check(ini.r_u32("base", "a") == 3 && ini.r_u32("base", "b") == 2, "safe override preserves other keys");
    });
    with_ini("@[base]\na=3\n[base]\na=1\nb=2\n", [](const CInifile& ini) {
        check(ini.r_u32("base", "a") == 3 && ini.r_u32("base", "b") == 2, "base may be defined later");
    });
    with_ini("[parent]\na=4\n@[child]:parent\nb=5\n", [](const CInifile& ini) {
        check(ini.r_u32("child", "a") == 4 && ini.r_u32("child", "b") == 5, "created section inherits");
    });
    with_ini("@[new]\na=1\n@[new]\nb=2\n", [](const CInifile& ini) {
        check(ini.r_u32("new", "a") == 1 && ini.r_u32("new", "b") == 2, "repeated safe overrides merge");
    });
    with_ini("[base]\na=1\nb=2\n![base]\na=3\n!b\n", [](const CInifile& ini) {
        check(ini.r_u32("base", "a") == 3 && !ini.line_exist("base", "b"), "ordinary override and deletion preserved");
    });
    xr_string sun_text;
    for (unsigned hour = 0; hour < 24; ++hour)
    {
        char row[128];
        xr_sprintf(row, "[%02u:00:00]\nsun_altitude=%u\nsun_longitude=-30\n", hour, hour * 10);
        sun_text += row;
    }
    with_ini(sun_text.c_str(), [](const CInifile& ini) {
        ConfigSunTable table;
        Fvector2 angles;
        check(!table.sample(0.f, angles), "absent sun table leaves native weather active");
        check(table.load(ini), "complete sun table loads");
        check(table.sample(5400.f, angles) && fsimilar(angles.x, 15.f) && fsimilar(angles.y, -30.f), "sun interpolates between hours");
        check(table.sample(84600.f, angles) && fsimilar(angles.x, 115.f), "sun interpolates across midnight");
        check(table.sample(86400.f, angles) && fsimilar(angles.x, 0.f), "sun wraps exact day boundary");
        check(table.sample(-1800.f, angles) && fsimilar(angles.x, 115.f), "sun wraps negative clock");
        with_ini("[00:00:00]\nsun_altitude=0\n", [&](const CInifile& incomplete) {
            check(!table.load(incomplete) && !table.sample(0.f, angles), "incomplete reload cannot retain stale sun table");
        });
    });
    if (vfs || lua_test)
    {
        TestScriptStorage scripts;
        scripts.reinit();
        const char* program =
            "assert(type(require('lua_extensions')) == 'table')\n"
            "assert(string.trim('  anthology  ') == 'anthology')\n"
            "assert(table.size({first=1, second=2}) == 2)\n"
            "assert(bit_or(1,4)==5 and bit_and(5,1)==1 and bit_xor(5,1)==4 and bit_not(0)==-1)\n"
            "local m=require('marshal'); local t=m.decode(m.encode({v=27}))\n"
            "assert(t.v == 27 and type(require('lfs').attributes) == 'function')\n";
        const int result = luaL_dostring(scripts.lua(), program);
        if (result)
            fprintf(stderr, "%s\n", lua_tostring(scripts.lua(), -1));
        check(result == 0, "Anomaly Lua module uses native IX-Ray string/table/marshal/lfs implementations");
        if (lua_test)
        {
            string_path script_path;
            FS.update_path(script_path, "$game_scripts$", "path_probe.script");
            check(scripts.load_file_into_namespace(script_path, "path_probe"), "script file loads into its namespace");
            lua_getglobal(scripts.lua(), "path_probe");
            lua_getfield(scripts.lua(), -1, "source");
            const xr_string expected_source = xr_string("@") + script_path;
            check(lua_isstring(scripts.lua(), -1) && expected_source == lua_tostring(scripts.lua(), -1),
                "Lua debug source retains virtual game path for addon resource lookup");
            lua_pop(scripts.lua(), 2);
            xml_test_state = scripts.lua();
            xr_string output;
            lua_pushinteger(xml_test_state, 37);
            const int top = lua_gettop(xml_test_state);
            check(!transform_xml("test.xml", "<root/>", output) && lua_gettop(xml_test_state) == top,
                "absent DXML callback preserves Lua stack and original document");
            check(luaL_dostring(xml_test_state, "function COnXmlRead(n,s) assert(s=='<root/>'); return '<root><value>7</value></root>' end") == 0,
                "DXML callback fixture loads");
            check(transform_xml("test.xml", "\xef\xbb\xbf<root/>", output) && output.find("<value>7</value>") != xr_string::npos && lua_gettop(xml_test_state) == top,
                "DXML bridge strips BOM and copies returned string without stack leaks");
            luaL_dostring(xml_test_state, "function COnXmlRead() error('expected test failure') end");
            check(!transform_xml("test.xml", "<root/>", output) && lua_gettop(xml_test_state) == top,
                "Lua callback failure preserves original XML and restores stack");
            luaL_dostring(xml_test_state, "function COnXmlRead() return nil end");
            check(!transform_xml("test.xml", "<root/>", output) && lua_gettop(xml_test_state) == top,
                "invalid Lua result cannot replace XML");
            CXml::SetReadCallback(&transform_xml);
            CXml xml;
            luaL_dostring(xml_test_state, "function COnXmlRead(n,s) assert(s:find('<value[^>]*>5</value>')); return s:gsub('>5</value>', '>9</value>') end");
            check(xml.Load("$game_config$", "ui", "dxml_probe.xml") && xml.ReadInt("value", 0, -1) == 9,
                "native XML overrides run before DXML and replacement is parsed by IX-Ray");
            xml.SetLocalRoot(xml.NavigateToNode("value"));
            luaL_dostring(xml_test_state, "function COnXmlRead() return '<broken>' end");
            check(xml.Load("$game_config$", "ui", "dxml_probe.xml") && xml.GetLocalRoot() == nullptr && xml.ReadInt("value", 0, -1) == 5,
                "malformed DXML retains valid base and clears old local root");
            const int cache_result = luaL_dofile(xml_test_state, "dxml_cache_test.lua");
            if (cache_result) fprintf(stderr, "%s\n", lua_tostring(xml_test_state, -1));
            check(cache_result == 0, "installed Anomaly DXML caches opted-in documents and repeats uncached callbacks");
            CXml::SetReadCallback(nullptr);
            xml_test_state = nullptr;
            check(!CXml::HasReadCallback() && xml.Load("$game_config$", "ui", "dxml_probe.xml") && xml.ReadInt("value", 0, -1) == 5,
                "detaching Lua callback preserves native XML overrides");
        }
    }
    if (vfs)
    {
        string_path marker;
        check(FS.exist(marker, "$game_config$", "anthology_ixray_probe.ltx") != nullptr, "MO2-only config visible");
        CInifile ini(marker);
        check(ini.r_u32("ixray_mo2_probe", "version") == 1, "MO2 config parsed by IX-Ray");
        string_path output;
        FS.update_path(output, "$app_data_root$", "mo2_vfs_verified.txt");
        std::ofstream file(output);
        file << "MO2 VFS and IX-Ray DLTX tests passed\nconfig=" << marker << "\n";
        check(file.good(), "report written to isolated appdata");
    }
    Core._destroy();
    return 0;
}
