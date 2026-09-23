"""Validate the integration boundaries around the separately tested C++ gate."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
R4 = ROOT / "src/Layers/xrRenderPC_R4"


def body(source, marker):
    start = source.index("{", source.index(marker))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start + 1:end - 1]


header = (R4 / "r4_rendertarget.h").read_text(encoding="utf-8")
target = (R4 / "r4_rendertarget.cpp").read_text(encoding="utf-8")
render = (R4 / "r4_R_render.cpp").read_text(encoding="utf-8")
constructor = body(target, "CRenderTarget::CRenderTarget()")
checks = 0


def check(value):
    global checks
    checks += 1
    assert value, f"Failed motion shader gate check {checks}"


check("bool m_svpMotionOwnerSupport = false;" in header)
check('xr_strconcat(motionHelper, RImplementation.getShaderPath(), "screenspace_mvectors.h");' in constructor)
check('IReader* motionSource = FS.r_open("$game_shaders$", motionHelper);' in constructor)
guarded = body(constructor, "if (motionSource)")
check("m_svpMotionOwnerSupport = IsPipMotionOwnerShaderCompatible(motionSource->pointer(), motionSource->length());" in guarded)
check("FS.r_close(motionSource);" in guarded)
check(constructor.index("IsPipMotionOwnerShaderCompatible(") < constructor.index(".create_parallel("))
supported = body(render, "bool CRenderTarget::svp_motion_supported() const")
check("return m_svpMotionOwnerSupport &&" in supported)
capture = body(render, "void CRenderTarget::phase_svp_scene()")
check("if (svp_motion_supported() && ensure_svp_motion_targets(" in capture)
motion = body(render, "void CRenderTarget::phase_svp_motion()")
check("!svp_motion_supported()" in motion)
generation_guard = "if (GetRenderSurfaceOwnerGeneration() != ownerGeneration)"
guarded = body(motion, generation_guard)
check(motion.index("draw_svp_scene(rt_svpMotionMap[next], -1);") < motion.index(generation_guard))
check(motion.index(generation_guard) < motion.index("t_svpMotionCurrent->surface_set(rt_svpMotionMap[next]->pSurface);"))
check(motion.index(generation_guard) < motion.index("m_svpMotionOutputFrame = Device.dwFrame;"))
for required in (
    "m_svpMotionHistory = false;", "m_svpMotionSeeded = false;",
    "u_setrt(rt_Generic_0, nullptr, nullptr, main_depth());", "RImplementation.rmNormal();", "return;",
):
    check(required in guarded)
print(f"v144 motion shader gate integration: {checks} checks passed")
