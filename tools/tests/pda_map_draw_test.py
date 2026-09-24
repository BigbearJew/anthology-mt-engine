"""Exercise production DrawTexture/Draw bodies with a counting UI backend."""
from pathlib import Path
import json, subprocess

engine = Path(__file__).resolve().parents[2]
w = engine / '_build/tests/pda_map'
w.mkdir(parents=True, exist_ok=True)
src = engine / 'src/xrGame/ui'
def method(file, signature):
    text = (src / file).read_text(encoding='utf8')
    start = text.index(signature); brace = text.index('{', start); depth = 1; end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end]

preamble = r'''
#include <cassert>
#include <cstdio>
#define PROF_EVENT(x) (void)0
struct Point { float x=0,y=0; };
struct Frect {
    float x1=0,y1=0,x2=0,y2=0;
    void add(float x,float y) { x1+=x;x2+=x;y1+=y;y2+=y; }
    void grow(float x,float y) { x1-=x;x2+=x;y1-=y;y2+=y; }
    bool intersected(const Frect& b) const { return !(x1>b.x2||x2<b.x1||y1>b.y2||y2<b.y1); }
};
struct Backend { int scissor=0; void PushScissor(const Frect&) { ++scissor; } void PopScissor() { --scissor; } } backend;
Backend& UI() { return backend; }
class CUIWindow {
public:
    int markers=0;
    virtual ~CUIWindow()=default;
    virtual void Draw() { ++markers; }
};
class CUIStatic : public CUIWindow {
    using inherited=CUIWindow;
public:
    int binds=0,texts=0;
    void Draw() override;
    virtual void DrawTexture() { ++binds; }
    void DrawText() { ++texts; }
};
class CUICustomMap : public CUIStatic {
public:
    Frect area{100,100,900,600};
    Frect& WorkingArea() { return area; }
    void Draw() override;
};
class CUILevelMap : public CUICustomMap {
    using inherited=CUICustomMap;
public:
    Frect bounds{110,110,300,300};
    Point m_TextureOffset;
    bool heading=false,stretch=true;
    bool Heading() { return heading; }
    bool GetStretchTexture() { return stretch; }
    void GetAbsoluteRect(Frect& r) { r=bounds; }
    void DrawTexture() override;
};
'''
body = '\n'.join([method('UIStatic.cpp','void CUIStatic::Draw()'),
    method('UIMap.cpp','void CUICustomMap::Draw()'), method('UIMap.cpp','void CUILevelMap::DrawTexture()')])
checks = r'''
void check(CUILevelMap map, bool visible) {
    CUIStatic& polymorphic=map; polymorphic.Draw();
    assert(map.binds==(visible?1:0));
    assert(map.markers==1 && map.texts==1 && backend.scissor==0);
}
int main() {
    CUILevelMap m; check(m,true);
    m.bounds={1000,100,1500,600};check(m,false);
    m.bounds={-500,100,0,600};check(m,false);
    m.bounds={100,-500,900,0};check(m,false);
    m.bounds={100,700,900,900};check(m,false);
    m.bounds={899,100,1500,600};check(m,true);
    m.bounds={-500,-500,2000,2000};check(m,true);
    m.bounds={900.4f,100,1500,600};check(m,true);
    m.bounds={1000,100,1500,600};m.m_TextureOffset={-200,0};check(m,true);
    m.m_TextureOffset={0,0};m.heading=true;check(m,true);
    m.heading=false;m.stretch=false;check(m,true);
    std::puts("11 map draw checks passed: no hidden DDS bind; markers and text preserved");
}
'''
(w/'map_draw_test.cpp').write_text(preamble+body+checks,encoding='utf8')
cmd = '@echo off\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\ncl /nologo /std:c++17 /EHsc /W4 /WX map_draw_test.cpp /Fe:map_draw_test.exe\nif errorlevel 1 exit /b 1\nmap_draw_test.exe\n'
(w/'test_map_draw.cmd').write_text(cmd,encoding='ascii')
r = subprocess.run(['cmd','/c',str(w/'test_map_draw.cmd')],cwd=w,capture_output=True,text=True)
(w/'map-draw-validation.json').write_text(json.dumps(dict(exit_code=r.returncode,stdout=r.stdout,stderr=r.stderr),indent=2),encoding='utf8')
print(r.stdout,r.stderr);raise SystemExit(r.returncode)
