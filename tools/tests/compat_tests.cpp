#include "xrCore/xrCore.h"
#include "xrCore/EngineExternal.h"
#include "xrEngine/ConfigSun.h"
#include "xrEngine/AnomalyConfig.h"
#include "xrScripts/linker.h"
#include "xrScripts/script_engine.h"
#include "xrScripts/dxml_bridge.h"
#include "xrScripts/exports/script_fvector.h"
#include "xrScripts/exports/script_ini_file.h"
#include "xrScripts/exports/script_net_packet.h"
#include "xrCore/FormatParsers/XML/xrXMLParser.h"
#include "Layers/xrRender/ThmChunk.h"
#include "Layers/xrRender/TextureMipSelection.h"
#include <fstream>
#include <luabind/luabind.hpp>

static void* __cdecl test_luabind_allocator(luabind::memory_allocation_function_parameter, const void* pointer, size_t size)
{
    void* mutable_pointer = const_cast<void*>(pointer);
    if (!size) { xr_free(mutable_pointer); return nullptr; }
    return pointer ? Memory.mem_realloc(mutable_pointer, size) : Memory.mem_alloc(size);
}

static int checked_argument_probe(int value) { return value + 1; }

static void check(bool success, const char* label)
{
    if (!success)
    {
        fprintf(stderr, "FAIL: %s\n", label);
        exit(1);
    }
    printf("PASS: %s\n", label);
}

class TestScriptStorage : public CScriptEngine
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
    check(SelectTextureMip(512, 256, 10, 2, true) == 2,
        "texture quality selects existing aligned mips");
    check(SelectTextureMip(256, 2, 9, 2, false) == 0 && SelectTextureMip(9, 5, 4, 1, false) == 0,
        "thin and odd DDS dimensions are never rounded beyond their pixel buffers");
    check(SelectTextureMip(12, 12, 4, 1, true) == 0 && SelectTextureMip(10, 10, 4, 1, false) == 1,
        "BC top-level alignment is respected without changing uncompressed dimensions");
    check(SelectTextureMip(512, 512, 1, 2, true) == 0 && SelectTextureMip(8, 8, 4, 99, true) == 1,
        "texture mip selection stays inside supplied chain and minimum size");
    const bool vfs = argc > 1 && strcmp(argv[1], "--vfs") == 0;
    const bool lua_test = argc > 1 && strcmp(argv[1], "--lua") == 0;
    Core._initialize("AnthologyIXRayTests", nullptr, vfs || lua_test,
        vfs ? "fsgame_ixray.ltx" : lua_test ? "fsgame_test.ltx" : nullptr);
    const std::wstring long_text(5000, L'\u042f');
    const xr_string long_utf8 = Platform::CP_TCHAR_TO_ANSI_U8(long_text.c_str());
    check(long_utf8.size() == 10000 && std::wstring(Platform::ANSI_TO_TCHAR(long_utf8.c_str())) == long_text,
        "long Cyrillic UTF-8 round trip exceeds old 256/4096 buffers safely");
    const xr_string long_cp1251 = Platform::TCHAR_TO_ANSI_U8(long_text.c_str());
    check(long_cp1251.size() == 5000 && (u8)long_cp1251.back() == 0xdf,
        "legacy wide-to-ANSI preserves CP1251 encoding for long UI text");
    check(Platform::CP_TCHAR_TO_ANSI_U8(L"").empty() && !*Platform::ANSI_TO_TCHAR(""),
        "empty Unicode conversion remains terminated");
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
    with_ini("[anomaly]\nhud_fov=0.65\n[native]\nanomaly_params=false\nscope_zoom_factor=25\n"
        "[hybrid]\nscope_zoom_factor=30\nironsight_zoom_factor=45\n[hud]\nhud_fov=40\n[empty_hud]\n", [](const CInifile& ini) {
        check(AnomalyConfig::Enabled(ini, "anomaly", true) && !AnomalyConfig::Enabled(ini, "anomaly", false) &&
            !AnomalyConfig::Enabled(ini, "native", true), "compatibility is opt-in and supports native per-item override");
        check(AnomalyConfig::ScopeZoom(ini, "anomaly", true) == 0.f &&
            AnomalyConfig::IronZoom(ini, "anomaly", true) == 0.f, "missing Anomaly zoom retains FOV-relative ironsight semantics");
        check(AnomalyConfig::ScopeZoom(ini, "native", false) == 25.f &&
            AnomalyConfig::IronZoom(ini, "hybrid", false) == 30.f &&
            AnomalyConfig::IronZoom(ini, "hybrid", true) == 45.f,
            "explicit IX-Ray scope and ironsight settings remain usable");
        check(fsimilar(AnomalyConfig::HudFov(ini, "anomaly", "empty_hud", true), .65f) &&
            AnomalyConfig::HudFov(ini, "anomaly", "hud", true) == 40.f &&
            AnomalyConfig::HudFov(ini, "anomaly", "empty_hud", false) == 0.f,
            "Anomaly item HUD FOV fallback preserves native HUD section priority");
    });
    check(fsimilar(AnomalyConfig::ZoomFov(0.f, 90.f, 1.25f), 77.31962f) &&
        fsimilar(AnomalyConfig::ZoomFov(0.f, 60.f, 1.25f), 49.58256f) &&
        AnomalyConfig::ZoomFov(20.f, 90.f, 1.25f) == 15.f,
        "Anomaly zoom follows changing FOV and explicit scope factors");
    check(fsimilar(AnomalyConfig::HudFovDegrees(.65f, 80.f), 52.f) &&
        AnomalyConfig::HudFovDegrees(40.f, 80.f) == 40.f,
        "HUD fractional conversion leaves native degrees unchanged");
    with_ini("[scope]\nscope_texture=\n", [](const CInifile& ini) {
        check(!AnomalyConfig::HasScopeTexture(ini.r_string("scope", "scope_texture")) &&
            !AnomalyConfig::HasScopeTexture("none") && !AnomalyConfig::HasScopeTexture("") &&
            AnomalyConfig::HasScopeTexture("wpn_crosshair"), "empty scope texture disables overlay without a null string comparison");
    });
    {
        CMemoryWriter chunks;
        chunks.w_u32(1); chunks.w_u32(2); chunks.w_u16(9);
        chunks.w_u32(2); chunks.w_u32(4); chunks.w_u32(73);
        chunks.w_u32(3); chunks.w_u32(1); chunks.w_u8(8);
        IReader reader(chunks.pointer(), chunks.size());
        bool repaired = false;
        check(FindTextureChunk(reader, 3, repaired) == 1 && reader.r_u8() == 8 && !repaired,
            "THM traversal skips valid intermediate chunks without repairing");
        check(FindTextureChunk(reader, 4, repaired) == 0 && !repaired,
            "absent optional THM chunk is not corruption");
        u32 wrong_size = 3;
        memcpy(static_cast<u8*>(chunks.pointer()) + 4, &wrong_size, sizeof(wrong_size));
        IReader malformed(chunks.pointer(), chunks.size());
        check(FindTextureChunk(malformed, 2, repaired) == 4 && malformed.r_u32() == 73 && repaired,
            "legacy THM incorrect preceding length is recovered within bounds");
        IReader truncated(chunks.pointer(), 7);
        repaired = false;
        check(FindTextureChunk(truncated, 2, repaired) == 0 && !repaired,
            "truncated THM header never reads outside the buffer");
    }
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
            luabind::allocator = &test_luabind_allocator;
            luabind::allocator_parameter = nullptr;
            luabind::open(scripts.lua());
            CScriptFvector::script_register(scripts.lua());
            CScriptIniFile::script_register(scripts.lua());
            CScriptNetPacket::script_register(scripts.lua());
            check(luaL_dostring(scripts.lua(),
                "local p=net_packet(); p:w_vec3(vector():set(3,4,5)); p:r_seek(0); "
                "local v=p:r_vec3(); assert(v.x==3 and v.y==4 and v.z==5 and p:r_tell()==12); "
                "p:r_seek(0); local out=vector(); p:r_vec3(out); assert(out.z==5 and p:r_tell()==12)") == 0,
                "network packet vector reader supports return-value and explicit destination forms");
            check(luaL_dostring(scripts.lua(),
                "local ini=create_ini_file('[probe]\\nanswer=42\\n'); "
                "local ok,k,v=ini:r_line('probe',0); assert(ok and k=='answer' and v=='42'); "
                "local ok2,k2,v2=ini:r_line('probe',0,'',''); assert(ok2 and k2==k and v2==v)") == 0,
                "INI line reader accepts both Anomaly short and native output-argument forms");
            check(luaL_dostring(scripts.lua(),
                "local v=vector():set(1,2,3); assert(v:add(4,5,6)==v); "
                "assert(v.x==5 and v.y==7 and v.z==9); "
                "v:add(1):add(vector():set(1,2,3)); "
                "assert(v.x==7 and v.y==10 and v.z==13)") == 0,
                "three-component vector add chains correctly and preserves native scalar/vector overloads");

            luabind::module(scripts.lua())[luabind::def("checked_argument_probe", &checked_argument_probe)];
            check(luaL_dostring(scripts.lua(),
                "assert(checked_argument_probe(8)==9); "
                "local ok,err=pcall(checked_argument_probe, {}); "
                "assert(not ok and tostring(err):find('no match',1,true))") == 0,
                "checked bindings reject incompatible arguments before entering native code");
            check(luaL_dostring(scripts.lua(),
                "class 'OptionalMethodProbe'; "
                "function OptionalMethodProbe:__init() end; "
                "assert(OptionalMethodProbe.missing_method==nil); "
                "function OptionalMethodProbe:answer() return 29 end; "
                "assert(OptionalMethodProbe():answer()==29)") == 0,
                "checked Lua bindings preserve nil for optional methods of script classes");
            const int marshal_result = luaL_dofile(scripts.lua(), "marshal_test.lua");
            if (marshal_result) fprintf(stderr, "%s\n", lua_tostring(scripts.lua(), -1));
            check(marshal_result == 0, "marshal preserves cycles, closures and deep tables; rejects incompatible bytecode and truncated input");
            g_pScriptEngine = &scripts;
            const int autoload_top = lua_gettop(scripts.lua());
            scripts.setup_auto_load();
            lua_settop(scripts.lua(), autoload_top);
            check(luaL_dostring(scripts.lua(),
                "assert(MixedCase_Probe.value==19); MixedCase_Probe.value=23; "
                "assert(MixedCase_Probe.value==23 and absent_ixray_probe==nil); "
                "assert(mixedcase_probe.value==19 and MixedCase_Probe.value==23)") == 0,
                "script autoload ignores filename case, caches exact namespaces and leaves missing globals nil");
            string_path global_path;
            FS.update_path(global_path, "$game_scripts$", "global_probe.script");
            scripts.xray_scripts["_g"] = global_path;
            scripts.process_file("_G", true);
            scripts.process_file("_g", true);
            check(luaL_dostring(scripts.lua(), "assert(ixray_namespace_probe == 1 and _g.ixray_namespace_probe == 2)") == 0,
                "global bootstrap and lowercase _g addon namespace remain distinct");
            string_path script_path;
            FS.update_path(script_path, "$game_scripts$", "path_probe.script");
            check(scripts.load_file_into_namespace(script_path, "path_probe"), "script file loads into its namespace");
            lua_getglobal(scripts.lua(), "path_probe");
            lua_getfield(scripts.lua(), -1, "source");
            const xr_string expected_source = xr_string("@") + script_path;
            check(lua_isstring(scripts.lua(), -1) && expected_source == lua_tostring(scripts.lua(), -1),
                "Lua debug source retains virtual game path for addon resource lookup");
            lua_pop(scripts.lua(), 2);
            FS.update_path(script_path, "$game_scripts$", "unlocalizer_probe.script");
            check(scripts.load_file_into_namespace(script_path, "unlocalizer_probe") &&
                luaL_dostring(scripts.lua(),
                    "local p=unlocalizer_probe; assert(p.parameters.value==17 and p.private_value==nil); "
                    "p.parameters.value=28; p.first=7; p.pending=11; "
                    "local a,b,c,d,e=p.read_values(); assert(a==28 and b==3 and c==7 and d==5 and e==11); "
                    "assert(p.exposed(6)==6 and p.local_scope()==9)") == 0,
                "Anomaly unlocalizers expose configured tables/functions while preserving closures and indented locals");
            check(luaL_dostring(scripts.lua(),
                "local a,b=unlocalizer_probe.source_text(); "
                "assert(a:find(\"local parameters = 'leave text intact'\",1,true)); "
                "assert(b:find('local parameters = second',1,true))") == 0,
                "unlocalizers leave multiline strings and comments intact");
            check(scripts.load_file_into_namespace(script_path, "ordinary_probe") &&
                luaL_dostring(scripts.lua(),
                    "assert(ordinary_probe.parameters==nil and ordinary_probe.exposed==nil); "
                    "local a,b,c,d,e=ordinary_probe.read_values(); assert(a==17 and b==3 and c==2 and d==5 and e==nil)") == 0,
                "scripts without unlocalizer configuration keep native local semantics");
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
            const int mcm_result = luaL_dofile(xml_test_state, "mcm_native_test.lua");
            if (mcm_result) fprintf(stderr, "%s\n", lua_tostring(xml_test_state, -1));
            check(mcm_result == 0, "native MCM uses available command tokens/bounds without changing settings at discovery");
            CXml::SetReadCallback(nullptr);
            xml_test_state = nullptr;
            g_pScriptEngine = nullptr;
            check(!CXml::HasReadCallback() && xml.Load("$game_config$", "ui", "dxml_probe.xml") && xml.ReadInt("value", 0, -1) == 5,
                "detaching Lua callback preserves native XML overrides");
            check(xml.Load("$game_config$", "ui", "legacy_declaration.xml") && xml.ReadInt("value", 0, -1) == 17 &&
                strcmp(xml.Read("text", 0, ""), "\xd1\xed\xe5\xe3") == 0,
                "legacy credit comments before declaration preserve BOM and CP1251 payload");
            xml.SetLocalRoot(xml.NavigateToNode("value"));
            check(!xml.Load("$game_config$", "ui", "nested_declaration.xml") &&
                !xml.GetRoot() && !xml.GetLocalRoot(),
                "nested declaration remains invalid and failed reload clears stale XML roots");
            check(!xml.Load("$game_config$", "ui", "comment_only.xml") && !xml.GetRoot(),
                "comment-only XML cannot report a loaded document");
            check(xml.Load("$game_config$", "ui", "xml_text.xml") &&
                strcmp(xml.Read("text", 0, ""), "<!-- credits --><?xml version=\"1.0\"?>") == 0,
                "declaration-like text inside CDATA remains unchanged");
        }
    }
    if (vfs)
    {
        string_path marker;
        check(FS.exist(marker, "$game_config$", "anthology_ixray_probe.ltx") != nullptr, "MO2-only config visible");
        CInifile ini(marker);
        check(ini.r_u32("ixray_mo2_probe", "version") == 1, "MO2 config parsed by IX-Ray");
        check(EngineExternal()[EEngineExternalGame::EnableAnomalyParams], "MO2 enables Anomaly parameters through native DLTX");
        string_path settings_path;
        FS.update_path(settings_path, "$game_config$", "system.ltx");
        CInifile settings(settings_path);
        check(AnomalyConfig::IronZoom(settings, "wpn_beretta", true) == 0.f,
            "actual MO2 Beretta configuration accepts omitted scope_zoom_factor");
        string_path inventory;
        FS.update_path(inventory, "$app_data_root$", "ixray_effective_config.ltx");
        std::ofstream config_output(inventory);
        for (const auto* section : settings.sections())
        {
            config_output << '[' << section->Name.c_str() << "]\n";
            for (const auto& item : section->Data)
                config_output << item.first.c_str() << " = " << (item.second.c_str() ? item.second.c_str() : "") << '\n';
            config_output << '\n';
        }
        check(config_output.good(), "effective Anomaly/MO2 configuration exported for parameter audit");
        // Keep the exact VFS winner for diagnosing packed script conflicts.
        if (auto* reader = FS.r_open("$game_scripts$", "bind_monster.script"))
        {
            string_path script_path;
            FS.update_path(script_path, "$app_data_root$", "ixray_effective_bind_monster.script");
            std::ofstream script_output(script_path, std::ios::binary);
            script_output.write(static_cast<const char*>(reader->pointer()), reader->length());
            check(script_output.good(), "effective packed monster binder exported");
            FS.r_close(reader);
        }
        string_path output;
        FS.update_path(output, "$app_data_root$", "mo2_vfs_verified.txt");
        std::ofstream file(output);
        file << "MO2 VFS and IX-Ray DLTX tests passed\nconfig=" << marker << "\n";
        check(file.good(), "report written to isolated appdata");
    }
    Core._destroy();
    return 0;
}
