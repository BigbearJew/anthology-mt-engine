"""Run production LZO/archive decode bodies against bounds and malformed input."""
from pathlib import Path
import argparse
import json
import subprocess

engine = Path(__file__).resolve().parents[2]
core = engine / 'src/xrCore'
out = engine / '_build/tests/archive_decode'
out.mkdir(parents=True, exist_ok=True)

def body(file, signature):
    text = (core / file).read_text(encoding='utf8')
    start = text.index(signature)
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

for name in ('rt_lzo1x_1.cpp', 'rt_lzo1x_d2.cpp'):
    text = (core / name).read_text(encoding='utf8')
    (out / name).write_text(text.replace('#include "stdafx.h"', '').replace('#pragma hdrstop', ''), encoding='utf8')

preamble = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
#include "rt_lzo1x.h"
using u8=unsigned char;using u32=uint32_t;
static thread_local lzo_align_t rtc_wrkmem[(LZO1X_1_MEM_COMPRESS+sizeof(lzo_align_t)-1)/sizeof(lzo_align_t)];
#define R_ASSERT2(ok,msg) do {if(!(ok)) throw std::runtime_error(msg);} while(0)
#define R_ASSERT3(ok,msg,name) R_ASSERT2(ok,msg)
static int live=0,mismatches=0,allocations=0;
template<class... A> void Msg(const char* format,A...) {if(strstr(format,"size mismatch recovered"))++mismatches;}
struct Block {size_t size;};
template<class T> T* xr_alloc(size_t count) {
    const size_t size=count*sizeof(T);auto* h=static_cast<Block*>(malloc(sizeof(Block)+size+16));assert(h);h->size=size;
    memset(h+1,0,size);memset(reinterpret_cast<char*>(h+1)+size,0xBD,16);++live;++allocations;return reinterpret_cast<T*>(h+1);
}
template<class T> void xr_free(T*& p) {
    if(!p)return;auto* h=reinterpret_cast<Block*>(p)-1;auto* tail=reinterpret_cast<u8*>(p)+h->size;
    for(int i=0;i<16;++i)assert(tail[i]==0xBD);free(h);p=nullptr;--live;
}
struct IReader {virtual ~IReader()=default;};
struct CTempReader:IReader {
    u8* data;u32 size;CTempReader(u8* p,u32 n,int):data(p),size(n){}~CTempReader(){xr_free(data);}
};
template<class T,class... A>T* xr_new(A... args){return new T(args...);}
struct Path {const char* operator*()const{return "fixture.xdb";}};
struct Archive {Path path;};
struct Desc {u32 size_real,size_compressed;};
'''
functions = body('rt_compressor.cpp', 'bool rtc_try_decompress(') + '\n' + body('rt_compressor.cpp', 'u32 rtc_decompress(')
archive = body('LocatorAPI.cpp', 'const auto decompress = ')
wrapper = 'CTempReader* decode(const std::vector<u8>& input,u32 declared) {\nArchive A;Desc desc{declared,u32(input.size())};const char* fname="fixture.dds";\n' + archive + ';\nreturn static_cast<CTempReader*>(decompress(input.data()));\n}\n'
checks = r'''
std::vector<u8> compress(const std::vector<u8>& input) {
    std::vector<u8> packed(input.size()+input.size()/16+128);lzo_uint n=packed.size();
    assert(lzo1x_1_compress(input.data(),input.size(),packed.data(),&n,rtc_wrkmem)==LZO_E_OK);packed.resize(n);return packed;
}
void check(const std::vector<u8>& original,u32 declared,bool expected) {
    const auto packed=compress(original);bool valid=false;allocations=0;
    try {auto* r=decode(packed,declared);valid=true;assert(r->size==original.size());assert(memcmp(r->data,original.data(),original.size())==0);delete r;}
    catch(const std::runtime_error&){}
    assert(valid==expected && live==0 && allocations<=2);
}
int main(int argc,char** argv) {
    std::vector<u8> original(8192);for(size_t i=0;i<original.size();++i)original[i]=u8(i*71);
    check(original,8192,true);check(original,8192-39,true);check(original,8192+39,true);
    std::vector<u8> boundary(65536+8192,31);check(boundary,8192,true);
    boundary.push_back(9);check(boundary,8192,false);
    auto packed=compress(original);packed.pop_back();bool rejected=false;
    try {auto* r=decode(packed,8192);delete r;}catch(const std::runtime_error&){rejected=true;}
    assert(rejected && live==0);
    u32 n=0;bool overrun=false;std::vector<u8> guarded(8192+16,0xAB);packed=compress(original);
    assert(!rtc_try_decompress(guarded.data(),8192-39,packed.data(),u32(packed.size()),n,overrun) && overrun);
    for(size_t i=8192-39;i<guarded.size();++i)assert(guarded[i]==0xAB);
    assert(rtc_decompress(guarded.data(),8192,packed.data(),u32(packed.size()))==8192);
    assert(memcmp(guarded.data(),original.data(),8192)==0 && mismatches==3);
    puts("8 checks passed: exact bytes, size recovery, bounded retry, invalid input, intact guards");
    if(argc==4) {
        std::ifstream f(argv[1],std::ios::binary);std::vector<u8> input{std::istreambuf_iterator<char>(f),{}};
        std::ifstream g(argv[2],std::ios::binary);std::vector<u8> reference{std::istreambuf_iterator<char>(g),{}};
        assert(!input.empty() && !reference.empty());auto* r=decode(input,u32(strtoul(argv[3],nullptr,10)));
        assert(r->size==reference.size() && memcmp(r->data,reference.data(),reference.size())==0);delete r;assert(live==0);
        puts("Real archive entry matches the independent reference byte-for-byte; guards intact");
    }
}
'''
(out / 'archive_test.cpp').write_text(preamble + functions + wrapper + checks, encoding='utf8')
cmd = '@echo off\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\ncl /nologo /O2 /EHsc /std:c++17 /wd5033 /I"' + str(core) + '" archive_test.cpp rt_lzo1x_1.cpp rt_lzo1x_d2.cpp /Fe:archive_test.exe\n'
(out / 'build.cmd').write_text(cmd, encoding='ascii')
r = subprocess.run(['cmd', '/c', str(out / 'build.cmd')], cwd=out, capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
ap = argparse.ArgumentParser()
ap.add_argument('--sample')
ap.add_argument('--reference')
ap.add_argument('--declared', default='44739408')
args = ap.parse_args()
command = [str(out / 'archive_test.exe')]
if args.sample:
    assert args.reference
    command += [str(Path(args.sample).resolve()), str(Path(args.reference).resolve()), args.declared]
r = subprocess.run(command, cwd=out, capture_output=True, text=True)
(out / 'validation.json').write_text(json.dumps(dict(exit_code=r.returncode,stdout=r.stdout,stderr=r.stderr),indent=2),encoding='utf8')
print(r.stdout, r.stderr)
raise SystemExit(r.returncode)
