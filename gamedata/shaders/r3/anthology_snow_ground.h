#ifndef ANTHOLOGY_SNOW_GROUND_H
#define ANTHOLOGY_SNOW_GROUND_H

// A world-space snow surface for every terrain pass, independent of DDS names.
float anthology_snow_hash(float2 p)
{
    p = frac(p * float2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return frac((p.x + p.y) * p.x);
}

float anthology_snow_noise(float2 p)
{
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(anthology_snow_hash(i), anthology_snow_hash(i + float2(1,0)), u.x),
                lerp(anthology_snow_hash(i + float2(0,1)), anthology_snow_hash(i + 1), u.x), u.y);
}

float anthology_snow_height(float2 p)
{
    return 0.16 * anthology_snow_noise(p * 0.55)
         + 0.045 * anthology_snow_noise(p * 2.1)
         + 0.008 * anthology_snow_noise(p * 11.0);
}

void anthology_snow_surface(float3 viewPos, float3 geometricNormalView, inout float3 normalView, inout float3 colour, inout float gloss)
{
    float3 world = mul(m_inv_V, float4(viewPos, 1)).xyz;
    float3 normalWorld = normalize(mul((float3x3)m_inv_V, geometricNormalView));
    float cover = smoothstep(0.32, 0.72, normalWorld.y);
    float h = anthology_snow_height(world.xz);
    const float d = 0.035;
    float2 gradient = float2(anthology_snow_height(world.xz + float2(d,0)) - h,
                            anthology_snow_height(world.xz + float2(0,d)) - h) / d;
    float3 snowNormal = normalize(normalWorld + float3(-gradient.x, 0, -gradient.y));
    normalView = normalize(lerp(normalView, mul((float3x3)m_V, snowNormal), cover));
    // Filter the fine grain with distance to avoid sparkling at the horizon.
    float grain = anthology_snow_noise(world.xz * 48.0);
    float grainWeight = 1.0 - smoothstep(12.0, 35.0, length(viewPos));
    float tone = 0.65 + 0.10 * anthology_snow_noise(world.xz * 0.55)
              + (grain - 0.5) * 0.04 * grainWeight;
    colour = lerp(colour, tone * float3(0.94, 0.97, 1.0), cover);
    gloss = lerp(gloss, 0.035, cover);
}

f_deffer anthology_snow_ground(float3 viewPos, float3 normalView, float4 base)
{
    float3 n = normalize(normalView), colour = base.rgb;
    float gloss = 0.001;
    anthology_snow_surface(viewPos, normalView, n, colour, gloss);
#ifdef EXTEND_F_DEFFER
    return pack_gbuffer(float4(n, base.a), float4(viewPos + n * def_virtualh / 2.0, 0.95), float4(colour, gloss), 0xffffffff);
#else
    return pack_gbuffer(float4(n, base.a), float4(viewPos + n * def_virtualh / 2.0, 0.95), float4(colour, gloss));
#endif
}
#endif
