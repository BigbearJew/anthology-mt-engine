#ifndef ANTHOLOGY_PIP_TEMPORAL_INCLUDED
#define ANTHOLOGY_PIP_TEMPORAL_INCLUDED

#include "anthology_pip_motion_sample.h"

uniform float4 scope_lense_detail;
uniform float4 scope_lense_quality;
uniform float4 scope_lense_main_view;

Texture2D<float4> s_pip_main_effects;
Texture2D<float4> s_pip_capture_effects;
float4 scope_lense_live_effects;

float3 pip_refresh_effects(float3 color, float2 lens_uv, float2 captured_uv)
{
    [branch] if (scope_lense_live_effects.x > 0.5)
    {
        float2 main_uv = (lens_uv - 0.5) * scope_lense_main_view.xy + scope_lense_main_view.zw;
        float3 old_effects = s_pip_capture_effects.SampleLevel(smp_base, saturate(captured_uv), 0).rgb;
        float3 current_effects = s_pip_main_effects.SampleLevel(smp_base, saturate(main_uv + scope_lense_live_effects.yz), 0).rgb;
        color = max(0.0, color - old_effects + current_effects);
    }
    return color;
}

float3 pip_project_capture(float2 uv)
{
    // One camera transform for the whole image. Texture matches and current
    // depth cannot deform individual patches of a reused capture.
    float4 clip = mul(scope_lense_reproject, float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 1.0, 1.0));
    float2 captured_uv = clip.xy / max(clip.w, 0.0001) * float2(0.5, -0.5) + 0.5;
    float edge = min(min(captured_uv.x, captured_uv.y), min(1.0 - captured_uv.x, 1.0 - captured_uv.y));
    float valid = clip.w > 0.0001 ? smoothstep(0.0, 2.0 * max(screen_res.z, screen_res.w), edge) : 0.0;
    return float3(captured_uv, valid);
}

float3 pip_capture_color(float2 uv)
{
    return s_second_vp.SampleLevel(smp_base, clamp(uv, screen_res.zw * 0.5, 1.0 - screen_res.zw * 0.5), 0).rgb;
}

// Reconstruct subpixel capture addresses without repeated bilinear softening.
float3 pip_capture_dynamic(float2 uv)
{
    uint width, height;
    s_second_vp.GetDimensions(width, height);
    float2 size = float2(width, height);
    float2 grid = uv * size - 0.5;
    float2 base = floor(grid), f = frac(grid);
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 p0 = (base - 0.5) / size;
    float2 p12 = (base + 0.5 + w2 / w12) / size;
    float2 p3 = (base + 2.5) / size;
    float3 color =
        pip_capture_color(float2(p0.x, p0.y)) * w0.x * w0.y +
        pip_capture_color(float2(p12.x, p0.y)) * w12.x * w0.y +
        pip_capture_color(float2(p3.x, p0.y)) * w3.x * w0.y +
        pip_capture_color(float2(p0.x, p12.y)) * w0.x * w12.y +
        pip_capture_color(p12) * w12.x * w12.y +
        pip_capture_color(float2(p3.x, p12.y)) * w3.x * w12.y +
        pip_capture_color(float2(p0.x, p3.y)) * w0.x * w3.y +
        pip_capture_color(float2(p12.x, p3.y)) * w12.x * w3.y +
        pip_capture_color(p3) * w3.x * w3.y;
    // Keep negative cubic lobes from adding bright/dark silhouette outlines.
    int2 last = int2(width - 1, height - 1);
    float3 a = s_second_vp.Load(int3(clamp(int2(base), 0, last), 0)).rgb;
    float3 b = s_second_vp.Load(int3(clamp(int2(base) + int2(1,0), 0, last), 0)).rgb;
    float3 c = s_second_vp.Load(int3(clamp(int2(base) + int2(0,1), 0, last), 0)).rgb;
    float3 d = s_second_vp.Load(int3(clamp(int2(base) + 1, 0, last), 0)).rgb;
    return clamp(color, min(min(a,b),min(c,d)), max(max(a,b),max(c,d)));
}

float4 sample_second_vp(float2 uv)
{
    float4 result = 0.0;
    [branch] if (scope_lense_motion.x > 0.5)
    {
        float4 address = pip_sample_motion_map(uv);
        if (address.z == 10001.0 && address.w > 0.0)
            result = float4(s_prev_frame.SampleLevel(smp_base, address.xy, 0).rgb, 1.0);
        else if (address.w >= 0.9999)
            result = float4(pip_refresh_effects(pip_capture_color(address.xy), uv, address.xy), 1.0);
        else
        {
            float2 main_uv = (uv - 0.5) * scope_lense_main_view.xy + scope_lense_main_view.zw;
            result = float4(s_prev_frame.SampleLevel(smp_base, saturate(main_uv), 0).rgb, 1.0);
            if (address.w > 0.0)
                result.rgb = lerp(result.rgb, pip_capture_color(address.xy), address.w);
        }
    }
    else
    {
        float3 projected = pip_project_capture(uv);
        result = float4(pip_capture_color(projected.xy) * projected.z, projected.z);
    }
    return result;
}

float3 pip_capture_detail(float2 captured_uv, bool dynamic_owner)
{
    // Fresh, cropped captures also need subpixel reconstruction at the lens.
    float3 center = pip_capture_dynamic(captured_uv);
    float sharpness = clamp(scope_lense_detail.x, 0.0, 2.0);
    [branch] if (abs(sharpness - 1.0) > 0.0001)
    {
        // Filter in capture space after one projection; use the real rendered
        // resolution even when the PiP image occupies a display-sized texture.
        float2 texel = 1.0 / max(scope_lense_quality.yz, 1.0);
        float3 left = pip_capture_color(captured_uv - float2(texel.x, 0.0));
        float3 right = pip_capture_color(captured_uv + float2(texel.x, 0.0));
        float3 top = pip_capture_color(captured_uv - float2(0.0, texel.y));
        float3 bottom = pip_capture_color(captured_uv + float2(0.0, texel.y));
        float3 lowpass = (center * 4.0 + left + right + top + bottom) * 0.125;
        float3 detail = center + (sharpness - 1.0) * (center - lowpass);
        float3 lo = min(center, min(min(left, right), min(top, bottom)));
        float3 hi = max(center, max(max(left, right), max(top, bottom)));
        center = clamp(detail, lo, hi);
    }
    return center;
}

float4 sample_second_vp_detail(float2 uv)
{
    float3 projected = float3(uv, 1.0);
    float2 main_uv = 0.0;
    bool use_main = false;
    bool current_sky = false;
    bool dynamic_owner = false;
    float coverage = 1.0;
    [branch] if (scope_lense_motion.x > 0.5)
    {
        float4 address = pip_sample_motion_map(uv, dynamic_owner);
        current_sky = address.z == 10001.0 && address.w > 0.0;
        coverage = address.w;
        if (current_sky) main_uv = address.xy;
        else if (address.w > 0.0)
            projected = float3(address.xy, 1.0);
        else
        {
            // Newly exposed/unknown surfaces have no trustworthy captured detail.
            main_uv = (uv - 0.5) * scope_lense_main_view.xy + scope_lense_main_view.zw;
            use_main = true;
        }
    }
    else
        projected = pip_project_capture(uv);
    float4 result = 0.0;
    if (current_sky)
        result = float4(s_prev_frame.SampleLevel(smp_base, main_uv, 0).rgb, 1.0);
    else if (use_main || coverage < 0.9999)
    {
        main_uv = (uv - 0.5) * scope_lense_main_view.xy + scope_lense_main_view.zw;
        result = float4(s_prev_frame.SampleLevel(smp_base, saturate(main_uv), 0).rgb, 1.0);
        if (coverage > 0.0)
            result.rgb = lerp(result.rgb, pip_capture_detail(projected.xy, dynamic_owner), coverage);
    }
    else
    {
        result = float4(pip_capture_detail(projected.xy, dynamic_owner) * projected.z, projected.z);
        if (scope_lense_motion.x > 0.5)
            result.rgb = pip_refresh_effects(result.rgb, uv, projected.xy);
    }
    return result;
}

#endif
