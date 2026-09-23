"""Execute production tree and Lua-grass routing, retaining SSS vertex passes."""
from pathlib import Path
import json, subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'_build/v146-validation/flora-routes'; OUT.mkdir(parents=True,exist_ok=True)
tree=(ROOT/'src/Layers/xrRender/Blender_tree.cpp').read_text(encoding='utf-8')
tree=tree[tree.rindex('void CBlender_Tree::Compile('):].rsplit('#endif',1)[0]
lua=(ROOT/'src/Layers/xrRenderDX10/dx10ResourceManager_Scripting.cpp').read_text(encoding='utf-8')
lua=lua[lua.index('\tadopt_compiler& _pass('):lua.index('\tadopt_compiler& _passgs(')]
prefix=r'''
#define USE_DX11
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using LPCSTR=const char*; using string256=char[256]; using string512=char[512];
int xr_strcmp(const char* a,const char* b) {return strcmp(a,b);}
void strconcat(size_t n,char* dst,const char* a,const char* b) {strcpy_s(dst,n,a); strcat_s(dst,n,b);}
enum {SE_R2_NORMAL_HQ,SE_R2_NORMAL_LQ,SE_R2_SHADOW};
enum {D3DCMP_ALWAYS,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,XRDX10RS_ALPHATOCOVERAGE,D3DRS_ZFUNC,D3DCMP_EQUAL,D3DBLEND_ZERO,D3DBLEND_ONE};
constexpr int TRUE=1, FALSE=0;
struct CRender {enum {MSAA_ATEST_DX10_0_ATOC=1};};
struct {struct {int dx10_msaa_alphatest=0; bool ssfx_branches=false;} o;} RImplementation;
struct Call {std::string vs,ps;};
std::vector<Call> calls;
struct CBlender_Compile {
 int iElement=0;
 std::vector<std::string> L_textures;
 int seasonal_bindings=0;
 struct {template<class... A> void SetRS(A...) {}} RS;
 template<class... A> void r_Stencil(A...) {}
 template<class... A> void r_ColorWriteEnable(A...) {}
 template<class... A> void r_StencilRef(A...) {}
 template<class... A> void r_dx10Sampler(A...) {}
 template<class T> void r_dx10Texture(const char* sampler,const T&) {if(strstr(sampler,"s_base_") || !strcmp(sampler,"s_snow_tree")) ++seasonal_bindings;}
 template<class... A> void r_Pass(const char* vs,const char* ps,A...) {calls.push_back({vs,ps});}
 void r_End() {}
};
struct IBlender {static void Compile(CBlender_Compile&) {}};
struct CBlender_Tree: IBlender {struct Option {bool value;} oNotAnTree{},oBlend{}; void Compile(CBlender_Compile&);};
void uber_deffer(CBlender_Compile&,bool,const char* vs,const char* ps,int,const char* =nullptr,bool=false) {calls.push_back({vs,ps});}
bool maps_exist=false;
bool anthology_prepare_flora_textures(const char*,string512& g,string512& a,string512& d) {
 strcpy_s(g,"green"); strcpy_s(a,"autumn"); strcpy_s(d,"dead"); return maps_exist;
}
struct adopt_compiler {
 CBlender_Compile* C;
 void TryEndPass() {}
'''
suffix=r'''
int main() {
 int count=0;
 for(int mask=0;mask<16;++mask) for(int element=0;element<2;++element) {
    CBlender_Tree tree; tree.oNotAnTree.value=mask&1; tree.oBlend.value=mask&2;
    RImplementation.o.ssfx_branches=bool(mask&4); RImplementation.o.dx10_msaa_alphatest=(mask&8)?1:0;
    CBlender_Compile c; c.iElement=element; calls.clear(); tree.Compile(c);
    const bool branch=element==SE_R2_NORMAL_HQ && tree.oBlend.value && RImplementation.o.ssfx_branches;
    const bool atoc=tree.oBlend.value && (mask&8);
    const std::string expectedVS=branch?"tree_branch":(tree.oNotAnTree.value?"tree_s":"tree");
    const std::string family=branch?"anthology_branch":"anthology_flora";
    assert(calls.size()==(atoc?2u:1u));
    assert(calls.back().ps==family && calls.back().vs==expectedVS);
    if(atoc) assert(calls.front().ps==family+"_atoc" && calls.front().vs==expectedVS);
    ++count;
 }
 for(const char* ps : {"deffer_grass","deffer_base","shadow_direct_grass"})
 for(int present=0;present<2;++present) for(int maps=0;maps<2;++maps) {
    CBlender_Compile c; if(present) c.L_textures.push_back("build_details");
    maps_exist=bool(maps); calls.clear(); adopt_compiler ac{&c}; ac._pass("deffer_grass",ps);
    const bool seasonal=present && maps && !strcmp(ps,"deffer_grass");
    assert(calls.size()==1 && calls[0].vs=="deffer_grass");
    assert(calls[0].ps==(seasonal?"deffer_anthology_ssfx_grass":ps));
    assert(c.seasonal_bindings==(seasonal?4:0));
    ++count;
 }
 printf("PASS: %d production tree/grass routes (SSS, HQ/LQ, ATOC, missing maps, unrelated Lua passes)\n",count);
}
'''
(OUT/'routes.cpp').write_text(prefix+lua+'};\n'+tree+suffix,encoding='utf-8')
cmd=OUT/'run.cmd'
cmd.write_text('@echo off\ncall "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
    'cl /nologo /std:c++17 /EHsc routes.cpp /Fe:routes.exe /Fo:routes.obj\nif errorlevel 1 exit /b 1\nroutes.exe\n',encoding='ascii')
result=subprocess.run(['cmd','/c',str(cmd)],cwd=OUT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
log=result.stdout.decode('cp1251',errors='replace'); (OUT/'result.log').write_text(log,encoding='utf-8')
print(log.strip()); assert result.returncode==0
(OUT/'result.json').write_text(json.dumps(dict(passed=True,cases=44),indent=2),encoding='utf-8')
