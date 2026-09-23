"""Execute production material-name preparation with poisoned input buffers.

The optional --negative-source must be the pre-fix uber_deffer.cpp. It must fail
the exact same fixtures, proving the test detects reads before bump resolution.
"""
from pathlib import Path
import argparse, json, re, subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / '_build/v1451-validation/material-init'
OUT.mkdir(parents=True, exist_ok=True)

STUBS = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using LPCSTR = const char*;
struct { LPCSTR Params = ""; } Core;
template<class... Args> void Msg(const char*, Args...) {}
using pcstr = const char*;
using LPSTR = char*;
using BOOL = bool;
using string256 = char[256];
using string512 = char[512];
using string_path = char[512];
const char* operator*(const std::string& s) { return s.c_str(); }
int checks = 0;
std::string bounded(const char* s) {
    if (!s) throw std::runtime_error("null texture name");
    const auto n = strnlen_s(s, 256);
    if (n == 256) throw std::runtime_error("read of unresolved bump name");
    return {s,n};
}
void xr_strcpy(char* dest,size_t size,const char* src) {
    const auto s=bounded(src);
    if(s.size()>=size) throw std::runtime_error("texture buffer overflow");
    memcpy(dest,s.c_str(),s.size()+1);
}
template<size_t N> void xr_strcpy(char (&dest)[N], const char* src) { xr_strcpy(dest,N,src); }
template<class... Args> void strconcat(size_t size,char* dest,Args... args) {
    const std::string s=(bounded(args)+...);
    if(s.size()>=size) throw std::runtime_error("concatenated texture name overflow");
    memcpy(dest,s.c_str(),s.size()+1);
}
template<size_t N> void xr_strcat(char (&dest)[N],const char* src) {
    strconcat(N,dest,dest,src);
}
int xr_strcmp(const char* a,const char* b) { return bounded(a).compare(bounded(b)); }
void fix_texture_name(char*) {}
struct FileSystem {
    std::set<std::string> files;
    std::vector<std::string> queries;
    bool exist(char*,const char*,const char* name,const char*) {
        queries.push_back(bounded(name));
        return files.count(queries.back())!=0;
    }
} FS;
bool hasBump = true;
struct ref_texture {
    void create(const char*) {}
    bool bump_exist() const { return hasBump; }
    const std::string& bump_get() const { static std::string s="grnd\\road_bump"; return s; }
};
struct Device {
    struct Description {
        const std::string& GetBumpName(const char*) const {
            static std::string s="detail\\road_bump"; return s;
        }
    } m_textures_description;
} device;
Device* DEV = &device;
struct CBlender_Compile {
    std::vector<std::string> L_textures{"grnd\\road"};
    const char* detail_texture="detail\\road";
    bool bDetail_Bump=false,bDetail_Diffuse=true,bUseSteepParallax=false;
};
struct MaterialNames {
    std::string ps,bump,bumpX,deadBump,winterBump,deadBumpX,winterBumpX,deadDetail,dead;
};
'''

FIXTURES = r'''
int main() {
 try {
    for(bool bump : {false,true}) for(bool seasonal : {false,true})
    for(bool support : {false,true}) for(bool detailBump : {false,true})
    for(bool hq : {false,true}) for(bool alpha : {false,true}) {
        hasBump=bump;
        FS.files.clear(); FS.queries.clear();
        if(seasonal) FS.files.insert("anthology_seasons\\dead\\grnd\\road");
        if(support) for(const char* season : {"dead","winter"}) {
            const std::string prefix=std::string("anthology_seasons\\")+season+"\\";
            for(const char* name : {"grnd\\road_bump","grnd\\road_bump#","detail\\road","detail\\road_bump","detail\\road_bump#"})
                FS.files.insert(prefix+name);
        }
        CBlender_Compile c; c.bDetail_Bump=detailBump;
        const auto result=prepare(c,hq,"base",alpha?"base_atoc":"base",alpha,nullptr,false,false);
        assert(result.bump==(bump?"grnd\\road_bump":""));
        assert(result.bumpX==(bump?"grnd\\road_bump#":""));
        if(seasonal) {
            assert(result.ps.find("anthology_ground")!=std::string::npos);
            if(bump) {
                assert(result.deadBump==(support?"anthology_seasons\\dead\\grnd\\road_bump":"grnd\\road_bump"));
                assert(result.winterBump==(support?"anthology_seasons\\winter\\grnd\\road_bump":"grnd\\road_bump"));
                assert(result.deadBumpX==(support?"anthology_seasons\\dead\\grnd\\road_bump#":"grnd\\road_bump#"));
            } else {
                assert(result.deadBump==result.dead && result.winterBump==result.dead);
                assert(result.deadBumpX==result.dead && result.winterBumpX==result.dead);
            }
            assert(result.deadDetail==(support?"anthology_seasons\\dead\\detail\\road":"detail\\road"));
        } else assert(result.ps.find("anthology_")==std::string::npos);
        ++checks;
    }
    printf("PASS: %d production material cases (flat/bump, seasonal/stock, support fallback, detail, HQ and alpha)\n",checks);
    return 0;
 } catch(const std::exception& e) { printf("FAIL: %s after %d cases\n",e.what(),checks); return 7; }
}
'''

def generate(source):
    text=source.read_text(encoding='utf-8')
    start=text.index('static bool anthology_starts_with_ci')
    end=text.index('\t// HQ',start)
    text=text[start:end].replace('void uber_deffer(', 'MaterialNames prepare(',1)
    # Poison both arrays even when the production declaration is zero-initialized.
    # Correct preparation must overwrite them before any texture helper reads them.
    text,n=re.subn(r'(\tstring256 fname[^;]+;)',
        r"\1\n\tstd::fill_n(fnameA,256,'!'); std::fill_n(fnameB,256,'!');",text,count=1)
    assert n==1
    return STUBS+text+'''\nreturn {ps,fnameA,fnameB,anthologyGroundBump,anthologyGroundWinterBump,
        anthologyGroundBumpX,anthologyGroundWinterBumpX,anthologyGroundDetail,anthologyGroundDead};\n}\n'''+FIXTURES

def run(source,label):
    cpp=OUT/(label+'.cpp'); cpp.write_text(generate(source),encoding='utf-8')
    cmd=OUT/(label+'.cmd')
    cmd.write_text('@echo off\ncall "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
        f'cl /nologo /std:c++17 /EHsc {label}.cpp /Fe:{label}.exe /Fo:{label}.obj\n'
        f'if errorlevel 1 exit /b 1\n{label}.exe\n',encoding='ascii')
    result=subprocess.run(['cmd','/c',str(cmd)],cwd=OUT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    log=result.stdout.decode(errors='replace'); (OUT/(label+'.log')).write_text(log,encoding='utf-8')
    return result.returncode,log

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--negative-source',type=Path)
    args=parser.parse_args()
    code,log=run(ROOT/'src/Layers/xrRender/uber_deffer.cpp','fixed')
    print(log.strip()); assert code==0
    negative=None
    if args.negative_source:
        code,log=run(args.negative_source,'before')
        print(log.strip()); assert code==7 and 'read of unresolved bump name' in log
        negative=True
    (OUT/'result.json').write_text(json.dumps(dict(passed=True,cases=64,old_code_rejected=negative),indent=2),encoding='utf-8')
