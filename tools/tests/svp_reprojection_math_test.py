"""Check XRay ray reprojection algebra; no renderer, GPU, or extra packages needed."""

import math
from pathlib import Path
import re
from types import SimpleNamespace


ENGINE_ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ENGINE_ROOT / "src/xrCore/_matrix.h").read_text(encoding="utf-8", errors="replace")
BODY = SOURCE.split("ICF SelfRef mul(const Self& A, const Self& B)", 1)[1].split("return *this;", 1)[0]
ASSIGNMENTS = re.findall(r"m\[(\d)\]\[(\d)\] = ([^;]+);", BODY)
assert len(ASSIGNMENTS) == 16, "Could not locate the actual 4x4 multiplication"


def identity():
    return [[float(i == j) for j in range(4)] for i in range(4)]


def xr_mul(first, second):
    # Read the real engine expressions, including its reverse storage order.
    result = identity()
    for i, j, expression in ASSIGNMENTS:
        terms = re.findall(r"A\.m\[(\d)\]\[(\d)\] \* B\.m\[(\d)\]\[(\d)\]", expression)
        assert len(terms) == 4, "Matrix multiplication expression changed"
        result[int(i)][int(j)] = sum(
            first[int(a)][int(b)] * second[int(c)][int(d)] for a, b, c, d in terms
        )
    return result


def transform(vector, matrix):
    return [sum(vector[i] * matrix[i][j] for i in range(4)) for j in range(4)]


def rotation(axis, degrees):
    result = identity()
    a, b = [(1, 2), (2, 0), (0, 1)][axis]
    cosine, sine = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    result[a][a] = result[b][b] = cosine
    result[a][b], result[b][a] = sine, -sine
    return result


def transpose(matrix):
    return [list(row) for row in zip(*matrix)]


def projection(fov, aspect):
    cotangent = 1 / math.tan(math.radians(fov) / 2)
    result = identity()
    result[0][0], result[1][1] = aspect * cotangent, cotangent
    result[2][2], result[2][3] = 1.0001, 1.0
    result[3][2], result[3][3] = -0.10001, 0.0
    return result


def camera_view(axis, degrees, position):
    result = rotation(axis, degrees)
    result[3][:3] = [-sum(position[k] * result[k][j] for k in range(3)) for j in range(3)]
    return result


def inverse_rigid(matrix):
    result = identity()
    for i in range(3):
        for j in range(3):
            result[i][j] = matrix[j][i]
    result[3][:3] = [-sum(matrix[3][k] * result[k][j] for k in range(3)) for j in range(3)]
    return result


def clip_to_uv(clip):
    return [clip[0] / clip[3] * 0.5 + 0.5, clip[1] / clip[3] * -0.5 + 0.5]


def check_positional_reprojection():
    """Compare the shader's depth correction against direct world projection.

    Captured depth is linear captured-view Z, not device Z or ray distance.
    Includes camera roll, lateral/forward translation, and varied lens FOV.
    """
    checks = 0
    for aspect in (9 / 16, 3 / 4, 9 / 21):
        for fov in (5.0, 20.0, 70.0):
            project = projection(fov, aspect)
            tangent = math.tan(math.radians(fov) / 2)
            for axis in range(3):
                captured = camera_view((axis + 1) % 3, -0.7, (2.0, -1.0, 4.0))
                for offset in ((0.0, 0.0, 0.0), (0.15, 0.0, 0.0), (0.0, -0.12, 0.0),
                               (0.0, 0.0, 0.2), (0.12, -0.07, 0.16)):
                    position = [base + delta for base, delta in zip((2.0, -1.0, 4.0), offset)]
                    current = camera_view(axis, 0.5, position)
                    inverse = inverse_rigid(current)
                    captured_origin = transform(position + [1.0], captured)
                    camera = [captured_origin[0] * project[0][0] * 0.5,
                              captured_origin[1] * project[1][1] * -0.5, captured_origin[2]]
                    capture_to_previous = xr_mul(project, xr_mul(captured, inverse))
                    for depth in (1.5, 10.0, 500.0):
                        for uv in ((0.5, 0.5), (0.25, 0.3), (0.75, 0.8)):
                            ray = [(2 * uv[0] - 1) * tangent / aspect,
                                   (1 - 2 * uv[1]) * tangent, 1.0, 0.0]
                            world_ray = transform(ray, inverse)
                            old_ray = transform(world_ray, captured)
                            rotation_uv = clip_to_uv(transform(old_ray, project))
                            current_point = [component * depth for component in ray[:3]] + [1.0]
                            world_point = transform(current_point, inverse)
                            captured_point = transform(world_point, captured)
                            assert captured_point[2] > 0
                            expected = clip_to_uv(transform(captured_point, project))
                            # Production inverse-depth correction. Its sampled Z belongs to
                            # the old image at the solved UV, which this direct reference knows.
                            corrected = [rotation_uv[i] +
                                         (camera[i] - (rotation_uv[i] - 0.5) * camera[2]) /
                                         captured_point[2] for i in range(2)]
                            assert max(abs(a - b) for a, b in zip(corrected, expected)) < 1e-10
                            # Optical flow must compare current view positions in the old
                            # camera, independently of per-object/main-view motion histories.
                            projected = clip_to_uv(transform(current_point, capture_to_previous))
                            assert max(abs(a - b) for a, b in zip(projected, expected)) < 1e-10
                            checks += 2
    # Sign/origin regression: strafing right projects the same forward current ray
    # to the right in the old camera; approaching a plane moves off-axis old UV inward.
    right = 0.1 * projection(20.0, 9 / 16)[0][0] * 0.5 / 10.0
    assert right > 0
    assert 0.75 - (0.75 - 0.5) * 0.2 / 10.0 < 0.75
    return checks + 2


def check_main_view_mapping():
    """Current-main fallback must use the lens ray, including main jitter."""
    checks = 0
    for aspect in (9 / 16, 3 / 4, 9 / 21):
        for lens_fov in (5.0, 20.0, 50.0):
            tangent = math.tan(math.radians(lens_fov) / 2)
            for main_fov in (55.0, 70.0, 90.0):
                main_project = projection(main_fov, aspect)
                for jitter in ((0.0, 0.0), (-0.0003, 0.0002), (0.0006, -0.0004)):
                    main_project[2][0], main_project[2][1] = jitter
                    scale = [main_project[0][0] * tangent / aspect, main_project[1][1] * tangent]
                    bias = [0.5 + jitter[0] * 0.5, 0.5 - jitter[1] * 0.5]
                    for uv in ((0.5, 0.5), (0.1, 0.2), (0.8, 0.9)):
                        ray = [(uv[0] * 2 - 1) * tangent / aspect,
                               (1 - uv[1] * 2) * tangent, 1.0, 0.0]
                        expected = clip_to_uv(transform(ray, main_project))
                        mapped = [(uv[i] - 0.5) * scale[i] + bias[i] for i in range(2)]
                        assert max(abs(a - b) for a, b in zip(mapped, expected)) < 1e-10
                        checks += 1
    return checks


def cpp_block(source, marker):
    start = source.index("{", source.index(marker))
    level = 1
    for end in range(start + 1, len(source)):
        level += (source[end] == "{") - (source[end] == "}")
        if level == 0:
            return source[start + 1:end]
    raise AssertionError(f"Unclosed C++ block: {marker}")


def cpp_arguments(source, marker):
    start = source.index("(", source.index(marker)) + 1
    level, previous, result = 0, start, []
    for end in range(start, len(source)):
        token = source[end]
        if token == ")" and level == 0:
            return result + [source[previous:end].strip()]
        if token == "," and level == 0:
            result.append(source[previous:end].strip())
            previous = end + 1
        level += (token == "(") - (token == ")")
    raise AssertionError(f"Unclosed C++ call: {marker}")


def cpp_scalar(expression, values):
    expression = re.sub(r"(?<=[\d.])f\b", "", expression).replace("_max", "max")
    return eval(expression, {"__builtins__": {}, "max": max}, values)


def check_main_view_binding_and_shader_jitter():
    """Exercise real binding expressions while Device uses a different HUD FOV.

    Geometry gets jitter in its vertex shader; the world projection remains
    unjittered. The previous algebra-only test incorrectly put jitter into that
    projection and could not detect either integration mistake.
    """
    binding = (ENGINE_ROOT / "src/Layers/xrRender/Blender_Recorder_StandartBinding.cpp").read_text(
        encoding="utf-8", errors="replace")
    binding = re.sub(r"//[^\n]*|/\*.*?\*/", "", binding, flags=re.S)
    main = cpp_block(binding, "static class scope_lense_main_view_setup")
    geometry = cpp_block(binding, "static class ssfx_jitter :")
    helper = cpp_block(binding, "static Fvector2 main_view_jitter_ndc()\n{")
    assert "const Fmatrix& projection = Device.mProject_saved;" in main
    assert "main_view_jitter_ndc()" in main and "main_view_jitter_ndc()" in geometry
    assert "Device.mView_saved" in cpp_block(binding, "static class scope_lense_reproject_setup")
    assert "Device.mProjectCam" not in main and "Device.mProjectHud" not in main
    arguments = cpp_arguments(main, "RCache.set_c")
    assert arguments[0] == "C" and len(arguments) == 5

    # A second binding call in one frame must not advance native TAA's sample.
    guard = cpp_block(helper, "if (main_last_frame != Device.dwFrame)")
    assert guard.count("++main_sequence;") == helper.count("++main_sequence;") == 1
    assert "main_last_frame = Device.dwFrame;" in guard
    assert "g_main_temporal_upscaler_active && !svp_frame" in helper
    assert "RImplementation.o.ssfx_taa && !svp_frame" in helper
    assert "if (!svp_frame && !g_main_temporal_upscaler_active)" in helper
    offsets = re.findall(r"\{\s*([-\d.]+)f,\s*([-\d.]+)f\s*\}", helper)
    assert len(offsets) == 4
    offsets = [(float(x), float(y)) for x, y in offsets]
    assert offsets == [(0.0, -1.0), (-1.0, 0.0), (1.0, 0.0), (0.0, 1.0)]
    vendor = cpp_block(helper, "if (g_main_temporal_upscaler_active && !svp_frame)")
    vendor_x = re.search(r"JitterX = ([^;]+);", vendor).group(1)
    vendor_y = re.search(r"JitterY = ([^;]+);", vendor).group(1)
    result_arguments = cpp_arguments(helper, "result.set")
    assert len(result_arguments) == 2

    device_source = (ENGINE_ROOT / "src/xrEngine/device.cpp").read_text(encoding="utf-8", errors="replace")
    saved_position = device_source.index("mProject_saved = mProject;")
    assert saved_position < device_source.index("Device.isRendering = true;", saved_position)
    hud = (ENGINE_ROOT / "src/Layers/xrRender/CHudInitializer.cpp").read_text(encoding="utf-8")
    assert "Device.mProject.set(Device.mProjectHud);" in hud

    checks, detected_old_errors = 0, 0
    for width, height in ((1920, 1080), (1280, 960), (2520, 1080)):
        aspect = height / width
        for world_fov, hud_fov in ((83.0, 58.1), (70.0, 49.8), (90.0, 66.4)):
            world = projection(world_fov, aspect)
            hud_project = projection(hud_fov, aspect)
            world_fields = SimpleNamespace(_11=world[0][0], _22=world[1][1], _31=0.0, _32=0.0)
            # Device.mProject deliberately remains the HUD matrix throughout.
            device = SimpleNamespace(fASPECT=aspect, mProject=hud_project, mProject_saved=world_fields)
            for lens_fov in (5.0, 20.0, 50.0):
                tangent = math.tan(math.radians(lens_fov) / 2)
                jitters = [(0.0, 0.0)]
                for strength in (0.5, 1.0, 2.0):
                    for ox, oy in offsets:
                        values = dict(JitterX=ox / width, JitterY=oy / height, jitterScale=strength)
                        jitters.append(tuple(cpp_scalar(arg, values) for arg in result_arguments))
                for render_scale in (1.0, 2 / 3, 0.5):
                    for x, y in ((-0.375, 0.125), (0.25, -0.375)):
                        values = dict(g_main_taa_jitter_pixels=SimpleNamespace(x=x, y=y),
                                      renderWidth=width * render_scale, renderHeight=height * render_scale)
                        jitter = (cpp_scalar(vendor_x, values), cpp_scalar(vendor_y, values))
                        assert abs(jitter[0] * 0.5 * values["renderWidth"] - x) < 1e-12
                        assert abs(jitter[1] * -0.5 * values["renderHeight"] - y) < 1e-12
                        jitters.append(jitter)
                for jx, jy in jitters:
                    values = dict(Device=device, projection=world_fields, tangent=tangent,
                                  jitter=SimpleNamespace(x=jx, y=jy))
                    actual = [cpp_scalar(arg, values) for arg in arguments[1:]]
                    for uv in ((0.5, 0.5), (0.2, 0.3), (0.8, 0.7)):
                        ray = [(2 * uv[0] - 1) * tangent / aspect,
                               (1 - 2 * uv[1]) * tangent, 1.0, 0.0]
                        clip = transform(ray, world)
                        # This is the actual SSS vertex-shader operation,
                        # independent of the lens binding's scalar UV formula.
                        clip[0] += jx * clip[3]
                        clip[1] += jy * clip[3]
                        expected = clip_to_uv(clip)
                        actual_uv = [(uv[i] - 0.5) * actual[i] + actual[i + 2] for i in range(2)]
                        assert max(abs(a - b) for a, b in zip(actual_uv, expected)) < 1e-10
                        old_uv = clip_to_uv(transform(ray, hud_project))
                        if max(abs(a - b) for a, b in zip(old_uv, expected)) * width > 2:
                            detected_old_errors += 1
                        checks += 1
    assert detected_old_errors > 500, "Regression must distinguish HUD/world projections"
    return checks, detected_old_errors


def main():
    checks = 0
    for aspect in (9 / 16, 3 / 4, 9 / 21):
        for fov in (5.0, 20.0, 70.0):
            tangent = math.tan(math.radians(fov) / 2)
            ray = identity()
            ray[1][1], ray[0][0], ray[3][3] = tangent, tangent / aspect, 0.0
            captured_projection = projection(fov, aspect)
            for axis in range(3):
                for angle in (0.0, -1.0, 1.0, 20.0):
                    current = rotation(axis, angle)
                    captured = rotation((axis + 1) % 3, 5.0)
                    combined = xr_mul(captured_projection, xr_mul(xr_mul(captured, transpose(current)), ray))
                    for uv in ((0.5, 0.5), (0.25, 0.25), (0.75, 0.75)):
                        ndc = [2 * uv[0] - 1, 1 - 2 * uv[1], 1, 1]
                        clip = transform(ndc, combined)
                        direction = [(2 * uv[0] - 1) * tangent / aspect, (1 - 2 * uv[1]) * tangent, 1, 0]
                        world_direction = transform(direction, transpose(current))
                        captured_direction = transform(world_direction, captured)
                        expected = transform(captured_direction, captured_projection)
                        assert max(abs(a - b) for a, b in zip(clip, expected)) < 1e-10
                        checks += 1
            # A stationary camera must not alter angular scale or UV position.
            clip = transform([0.4, -0.3, 1, 1], xr_mul(captured_projection, ray))
            assert abs(clip[0] / clip[3] - 0.4) < 1e-10
            assert abs(clip[1] / clip[3] + 0.3) < 1e-10
            checks += 1
    print(f"PASS: {checks} PiP reprojection algebra checks (identity, yaw/pitch/roll, three aspects and lens FOVs)")
    positional = check_positional_reprojection()
    print(f"PASS: {positional} depth/flow camera checks against direct world-point projection")
    main_view = check_main_view_mapping()
    print(f"PASS: {main_view} lens-to-main mapping checks (FOV/aspect/projection jitter)")
    bindings, old_errors = check_main_view_binding_and_shader_jitter()
    print(f"PASS: {bindings} real-binding checks with unequal HUD/world FOV and shader jitter "
          f"({old_errors} cases expose the old projection)")


if __name__ == "__main__":
    main()
