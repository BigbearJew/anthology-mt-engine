"""Run the production capture-invalidating expressions with the real scheduler."""

from pathlib import Path
import os
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "_build/v143-validation/capture-refresh"


def production_conditions():
    source = (ROOT / "src/xrEngine/device.cpp").read_text(encoding="utf-8")
    function = source.split("bool CRenderDevice::CSecondVPParams::IsSVPFrame()", 1)[1]
    conditions = re.search(
        r"const float angularLimit\s*=.*?const bool cameraChanged\s*=.*?;",
        function,
        re.S,
    )
    assert conditions, "Cannot find the production camera invalidation conditions"
    return conditions.group(0)


HARNESS = r'''
#include "../../../src/xrEngine/SvpTemporalSchedule.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

unsigned checks = 0;
void check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

float deg2rad(float degrees) { return degrees * 0.017453292519943295f; }
float clampr(float value, float low, float high) { return value < low ? low : (value > high ? high : value); }
float _cos(float value) { return std::cos(value); }
float _abs(float value) { return std::abs(value); }
float _max(float a, float b) { return a > b ? a : b; }

struct Vec
{
    float x, y, z;
    float distance_to_sqr(const Vec& v) const
    { return (x-v.x)*(x-v.x) + (y-v.y)*(y-v.y) + (z-v.z)*(z-v.z); }
    float dotproduct(const Vec& v) const { return x*v.x + y*v.y + z*v.z; }
};

struct Capture
{
    struct { Vec vCameraPosition{0,0,0}, vCameraDirection{0,0,1}, vCameraTop{0,1,0}; } Device;
    bool isTextureReady = true;
    Vec capturedPosition{0,0,0}, capturedDirection{0,0,1}, capturedTop{0,1,0};
    float capturedFov = 20, requestedFov = 20;
    int capturedNvg = 0, ps_scope_lense_head_nvg_active = 0;
    int capturedQuality = 100, ps_scope_lense_quality_percent = 100;

    bool Changed() const
    {
        // Replaced by the exact statements from device.cpp before compilation.
        @PRODUCTION_CONDITIONS@
        return cameraChanged;
    }

    void Publish()
    {
        capturedPosition = Device.vCameraPosition;
        capturedDirection = Device.vCameraDirection;
        capturedTop = Device.vCameraTop;
        capturedFov = requestedFov;
        capturedNvg = ps_scope_lense_head_nvg_active;
        capturedQuality = ps_scope_lense_quality_percent;
    }
};

void movement_cadence()
{
    for (unsigned fps : {30u, 36u, 60u, 90u, 110u})
    for (unsigned interval : {2u, 4u, 8u})
    for (float fov : {5.f, 20.f, 70.f})
    for (unsigned scenario = 0; scenario < 3; ++scenario)
    {
        Capture capture;
        capture.requestedFov = capture.capturedFov = fov;
        SvpTemporalSchedule schedule;
        unsigned captures = 0, mains = 0;
        bool ready = false;
        for (unsigned frame = 0; frame < (interval + 1) * 12; ++frame)
        {
            const float seconds = float(frame) / fps;
            if (scenario == 0)
                capture.Device.vCameraPosition.x = seconds * 2.6f;
            else if (scenario == 1)
            {
                capture.Device.vCameraPosition.y = std::sin(seconds * 8.f) * 0.04f;
                const float roll = deg2rad(std::sin(seconds * 8.f) * 0.25f);
                capture.Device.vCameraTop = {std::sin(roll), std::cos(roll), 0};
            }
            else
            {
                const float yaw = deg2rad(float(frame) * 0.1f);
                capture.Device.vCameraDirection = {std::sin(yaw), 0, std::cos(yaw)};
            }
            const bool changed = ready && capture.Changed();
            check(!changed, "walking, head bob or small aim movement must preserve capture coverage");
            const unsigned time = frame * 1000 / fps;
            if (schedule.Select(frame, time, true, ready, interval, changed))
            {
                if (ready) check(mains == interval, "normal camera motion must honor the selected main interval");
                capture.Publish();
                schedule.Captured(frame, time);
                ready = true;
                mains = 0;
                ++captures;
            }
            else
                ++mains;
        }
        check(captures == 12 && mains == interval, "all normal-motion cases retain the requested capture count");
    }
}

void forced_refresh()
{
    for (unsigned scenario = 0; scenario < 6; ++scenario)
    for (unsigned interval : {2u, 4u, 8u})
    {
        Capture capture;
        SvpTemporalSchedule schedule;
        check(schedule.Select(0, 0, true, false, interval, false), "first view captures");
        capture.Publish();
        schedule.Captured(0, 0);
        switch (scenario)
        {
        case 0: capture.Device.vCameraPosition.x = 2.f; break;
        case 1: capture.Device.vCameraDirection = {std::sin(deg2rad(20.f)), 0, std::cos(deg2rad(20.f))}; break;
        case 2: capture.Device.vCameraTop = {std::sin(deg2rad(20.f)), std::cos(deg2rad(20.f)), 0}; break;
        case 3: capture.requestedFov = 10.f; break;
        case 4: capture.ps_scope_lense_head_nvg_active = 1; break;
        case 5: capture.ps_scope_lense_quality_percent = 50; break;
        }
        check(capture.Changed(), "teleport, large rotation or imaging settings must invalidate the captured view");
        check(!schedule.Select(1, 28, true, true, interval, capture.Changed()), "forced refresh cannot discard the main after capture");
        check(schedule.Select(2, 56, true, true, interval, capture.Changed()), "invalidated capture refreshes after exactly one main frame");
        capture.Publish();
        schedule.Captured(2, 56);
        check(!capture.Changed(), "publishing a changed view clears invalidation");
        for (unsigned frame = 3; frame <= 2 + interval; ++frame)
            check(!schedule.Select(frame, frame * 28, true, true, interval, capture.Changed()), "forced-refresh recovery restores the full configured interval");
        check(schedule.Select(3 + interval, (3 + interval) * 28, true, true, interval, capture.Changed()), "normal cadence resumes after forced refresh");
    }
}

int main()
{
    movement_cadence();
    forced_refresh();
    std::printf("PASS: production capture refresh (%u checks, 135 normal-motion scenarios, 18 forced-refresh scenarios)\n", checks);
}
'''


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / "capture_refresh_test.cpp"
    # initializer_list is needed by the portable range-for test inputs.
    source.write_text("#include <initializer_list>\n" + HARNESS.replace(
        "@PRODUCTION_CONDITIONS@", production_conditions()), encoding="utf-8")
    executable = OUT / "capture_refresh_test.exe"
    compiler = shutil.which("cl.exe")
    setup = ""
    if compiler is None:
        vcvars = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / (
            "Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat")
        if not vcvars.is_file():
            raise RuntimeError("MSVC is required: run from a developer prompt or install VS 2022 Build Tools")
        setup = f'call "{vcvars}" >nul\nif errorlevel 1 exit /b %errorlevel%\n'
    script = OUT / "run.cmd"
    script.write_text(
        '@echo off\n' + setup +
        f'cl /nologo /std:c++17 /EHsc /W4 /WX /O2 "{source}" '
        f'/Fo"{OUT / "capture_refresh_test.obj"}" /Fe"{executable}"\n'
        'if errorlevel 1 exit /b %errorlevel%\n' +
        f'"{executable}"\nexit /b %errorlevel%\n', encoding="ascii")
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (OUT / "result.log").write_text(result.stdout, encoding="utf-8")
    print(result.stdout, end="")
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
