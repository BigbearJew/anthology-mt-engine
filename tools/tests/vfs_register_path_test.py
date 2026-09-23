"""Exercise production Register path normalization at the CRT size boundary."""
from pathlib import Path
import argparse, json, re, subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'_build/v1452-validation/vfs-path'; OUT.mkdir(parents=True,exist_ok=True)

def generate(source):
    text=source.read_text(encoding='utf-8')
    start=text.index('void CLocatorAPI::Register(')
    body=text[start:text.index('\tif (m_initial_build)',start)].split('{',1)[1]
    types=(ROOT/'src/xrCore/_types.h').read_text(encoding='utf-8')
    path_type=re.search(r'typedef char string_path\[[^;]+;',types)[0]
    return r'''
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
using LPCSTR=const char*;
typedef char string256[256];
''' + path_type + r'''
static int invalid_calls=0;
static void invalid(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) { ++invalid_calls; }
static errno_t xr_strcpy(char* dst,size_t size,const char* src) { return strcpy_s(dst,size,src); }
template<size_t N> void xr_strlwr(char (&value)[N]) { _strlwr_s(value,N); }
static std::string normalized_name(LPCSTR name) {
''' + body + r'''
return temp_file_name;
}
int main() {
    _set_invalid_parameter_handler(invalid);
    int count=0;
    std::vector<std::string> paths;
    for(size_t size : {size_t(1),size_t(80),size_t(254),size_t(255),size_t(256),size_t(257),size_t(258),size_t(259),size_t(300),size_t(519)})
        paths.emplace_back(size,'A');
    const std::string tail="bin/..\\appdata\\shaders_cache\\r4\\anthology_seasons145\\deffer_base_bump(tess_pn,use_tdetail,use_tdetail_bump,).ps\\153611110011010000000000010011110111001028412328101861000000";
    for(size_t root_size : {size_t(77),size_t(79),size_t(81)}) paths.push_back(std::string(root_size,'x')+"/"+tail);
    for(const auto& path : paths) {
        const auto value=normalized_name(path.c_str());
        std::string expected=path;
        std::transform(expected.begin(),expected.end(),expected.begin(),[](unsigned char c){ return char(tolower(c)); });
        if(invalid_calls || value!=expected) { printf("FAIL: CRT boundary at %zu bytes after %d cases\n",path.size(),count); return 7; }
        ++count;
    }
    printf("PASS: %d production VFS normalization cases, including 255/256 boundary and X/A/B shader paths\n",count);
    return 0;
}
'''

def run(source,label):
    (OUT/(label+'.cpp')).write_text(generate(source),encoding='utf-8')
    cmd=OUT/(label+'.cmd')
    cmd.write_text('@echo off\ncall "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
        f'cl /nologo /std:c++17 /EHsc {label}.cpp /Fe:{label}.exe /Fo:{label}.obj\n'
        f'if errorlevel 1 exit /b 1\n{label}.exe\n',encoding='ascii')
    result=subprocess.run(['cmd','/c',str(cmd)],cwd=OUT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    log=result.stdout.decode('cp1251',errors='replace')
    (OUT/(label+'.log')).write_text(log,encoding='utf-8')
    print(log.strip()); return result.returncode,log

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--negative-source',type=Path)
    args=parser.parse_args()
    code,log=run(ROOT/'src/xrCore/LocatorAPI.cpp','fixed'); assert code==0
    negative=False
    if args.negative_source:
        code,log=run(args.negative_source,'before')
        assert code==7 and 'CRT boundary at 256 bytes' in log
        negative=True
    (OUT/'result.json').write_text(json.dumps(dict(passed=True,cases=13,old_code_rejected=negative),indent=2),encoding='utf-8')
