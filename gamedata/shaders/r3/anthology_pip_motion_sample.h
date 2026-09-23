#ifndef ANTHOLOGY_PIP_MOTION_SAMPLE_INCLUDED
#define ANTHOLOGY_PIP_MOTION_SAMPLE_INCLUDED

Texture2D<float4> s_pip_motion_map;
// Enabled, absolute depth tolerance, relative depth tolerance, map span in texels.
float4 scope_lense_motion;

bool pip_sample_owner_valid(float owner)
{
    return isfinite(owner) && (owner == 1.0 || (owner > -0.5 && owner <= -0.00006103515625));
}

float4 pip_sample_motion_map(float2 uv, out bool dynamic_owner)
{
    // A single initialized return also keeps FXC's nested-inline analysis sound.
    float4 result = 0.0;
    dynamic_owner = false;
    if (all(isfinite(uv)) && all(uv >= 0.0) && all(uv <= 1.0))
    {
        uint width = 0, height = 0;
        s_pip_motion_map.GetDimensions(width, height);
        if (width > 0 && height > 0)
        {
            int2 last = int2(width - 1, height - 1);
            float2 dimensions = float2(width, height);
            int2 nearest_pixel = clamp(int2(uv * dimensions), int2(0, 0), last);
            float4 reference = s_pip_motion_map.Load(int3(nearest_pixel, 0));
            dynamic_owner = pip_sample_owner_valid(reference.w) && reference.w < 0.0;
            bool valid_reference = all(isfinite(reference)) && pip_sample_owner_valid(reference.w) &&
                reference.z > 0.01 && reference.z < 10000.0 &&
                all(reference.xy >= 0.0) && all(reference.xy <= 1.0);
            // Current sky centres must not interpolate back across foreground.
            bool current_sky = all(isfinite(reference)) && reference.z == 10001.0 && reference.w == 1.0 &&
                all(reference.xy >= 0.0) && all(reference.xy <= 1.0);
            if (current_sky) result = float4(reference.xy, 10001.0, 1.0);
            else if (valid_reference)
            {
                float2 grid = uv * dimensions - 0.5;
                int2 base = int2(floor(grid));
                float2 fraction = frac(grid);
                float2 address_sum = 0.0;
                float inverse_depth_sum = 0.0;
                float weight_sum = 0.0;
                [unroll] for (int tap = 0; tap < 4; ++tap)
                {
                    int2 offset = int2(tap & 1, tap >> 1);
                    int2 pixel = clamp(base + offset, int2(0, 0), last);
                    float4 value = s_pip_motion_map.Load(int3(pixel, 0));
                    float depth_tolerance = max(scope_lense_motion.y, 0.001) +
                        max(scope_lense_motion.z, 0.0) * max(reference.z, value.z);
                    bool compatible = all(isfinite(value)) && value.w == reference.w &&
                        value.z > 0.01 && value.z < 10000.0 &&
                        all(value.xy >= 0.0) && all(value.xy <= 1.0) &&
                        abs(value.z - reference.z) <= depth_tolerance &&
                        length((value.xy - reference.xy) * dimensions) <= max(scope_lense_motion.w, 1.0);
                    if (compatible)
                    {
                        float2 weights = lerp(1.0 - fraction, fraction, float2(offset));
                        float weight = weights.x * weights.y;
                        address_sum += value.xy * weight;
                        inverse_depth_sum += weight / value.z;
                        weight_sum += weight;
                    }
                }
                if (weight_sum >= 0.75)
                    result = float4(address_sum / max(weight_sum, 0.75),
                        weight_sum / max(inverse_depth_sum, 0.000001), smoothstep(0.75, 1.0, weight_sum));
            }
        }
    }
    return result;
}

float4 pip_sample_motion_map(float2 uv)
{
    bool dynamic_owner;
    return pip_sample_motion_map(uv, dynamic_owner);
}
#endif
