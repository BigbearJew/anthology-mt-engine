"""Source integration checks for the ordinary PiP capture early exit.

The GPU harness verifies scene sampling separately. These checks exercise the
actual C++ mode predicates and guard the cleanup/caller boundaries it cannot see.
"""

from itertools import product
from pathlib import Path
import re


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


def predicate(expression, values):
    for name, value in sorted(values.items(), key=lambda item: -len(item[0])):
        expression = expression.replace(name, repr(value))
    expression = expression.replace("&&", " and ").replace("||", " or ")
    expression = re.sub(r"!(?!=)", " not ", expression)
    expression = " ".join(expression.split())
    assert not re.search(r"[A-Za-z_]", re.sub(r"\b(True|False|and|or|not)\b", "", expression)), expression
    return bool(eval(expression.strip(), {"__builtins__": {}}, {}))


render = (R4 / "r4_R_render.cpp").read_text(encoding="utf-8")
combine_source = (R4 / "r4_rendertarget_phase_combine.cpp").read_text(encoding="utf-8")
combine = body(combine_source, "void CRenderTarget::phase_combine()")
required = body(render, "bool CRenderTarget::svp_scene_capture_required() const")
head_expression = re.search(r"const bool headNvg = (.*?);", required, re.S).group(1)
required_expression = re.search(r"return (.*?);", required, re.S).group(1)
capture = body(render, "void CRenderTarget::phase_svp_scene()")

assert capture.index("m_svpSceneFrame = u32(-1);") < capture.index("return;")
assert "!svp_scene_capture_required() || !rt_secondVP_scene || !rt_secondVP_scene->valid()" in capture
assert capture.index("draw_svp_scene(rt_secondVP_scene);") < capture.index("m_svpSceneFrame = Device.dwFrame;")
assert capture.index("return;") < capture.index("phase_smaa();") < capture.index("draw_svp_scene(rt_secondVP_scene);")
assert capture.count("phase_smaa();") == 1

outer_guard = "if (svp_frame && Device.m_SecondViewport.isCamReady)"
capture_block = body(combine, outer_guard)
exit_guard = "if (m_svpSceneFrame == Device.dwFrame && svp_scene_capture_required() &&\n\t\t\trt_secondVP_scene && rt_secondVP_scene->valid())"
exit_block = body(capture_block, exit_guard)
assert capture_block.index("phase_svp_scene();") < capture_block.index(exit_guard)
assert "return;" in exit_block
assert combine.index(outer_guard) < combine.index("phase_lut();")
for operation in ("phase_3DSSReticle();", "phase_ssfx_bloom();", "phase_smaa();", "phase_upscale(!svp_frame);", "phase_pp(false);"):
    assert combine.index(outer_guard) < combine.index(operation), operation

for cleanup in ("mapScopeHUDSorted.clear();", "RCache.set_xform_world(Fidentity);",
                "u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT, nullptr, nullptr, HW.pBaseZB);",
                "RImplementation.rmNormal();", "RCache.set_Stencil(FALSE);",
                "dbg_spheres.clear();", "dbg_lines.clear();", "dbg_planes.clear();"):
    assert cleanup in exit_block, cleanup

assert "if (!svp_frame)\n\t{\n\t\tstd::swap(rt_LUM_pool[0], rt_LUM_pool[1]);" in combine
assert "ps_ssfx_taa.x > 0 && !svp_frame && !m_upscalerActive" in combine
assert "if (ssfx_PrevPos_Requiered && !svp_frame)" in combine
assert "phase_upscale(!svp_frame);" in combine
assert render.index("Target->phase_combine();") < render.index("svpQualityScope.restore();")
assert render.index("svpQualityScope.restore();") < render.index("FinishRenderPhaseFrame(", render.index("svpQualityScope.restore();"))

cases = 0
for svp, camera, current_capture, target_valid, allow_nvg, head_nvg, engine_nvg, thermal, heat, hdr in product((False, True), repeat=10):
    values = {
        "ps_scope_lense_allow_nvg": allow_nvg,
        "ps_scope_lense_head_nvg_active": head_nvg,
        "ps_r2_nightvision": int(engine_nvg),
        "Device.m_SecondViewport.IsSVPThermal()": thermal,
        "ps_r2_heatvision": int(heat),
        "RImplementation.o.dx11_hdr10": hdr,
    }
    values["headNvg"] = predicate(head_expression, values)
    allowed = predicate(required_expression, values)
    values.update({"svp_frame": svp, "Device.m_SecondViewport.isCamReady": camera,
                   "rt_secondVP_scene": target_valid, "rt_secondVP_scene->valid()": target_valid,
                   "m_svpSceneFrame": 123 if current_capture and target_valid and allowed else 122,
                   "Device.dwFrame": 123, "svp_scene_capture_required()": allowed})
    exited = predicate(outer_guard[4:-1], values) and predicate(exit_guard[4:-1], values)
    expected = svp and camera and current_capture and target_valid and not (thermal or heat or hdr or engine_nvg or (allow_nvg and head_nvg))
    assert exited == expected, values
    cases += 1

print(f"PASS: {cases} PiP mode/capture combinations; queue/state cleanup and main history/caller boundaries")
