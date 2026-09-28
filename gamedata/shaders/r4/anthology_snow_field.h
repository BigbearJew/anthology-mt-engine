#ifndef ANTHOLOGY_SNOW_FIELD_H
#define ANTHOLOGY_SNOW_FIELD_H

// Height, density, random height variation, drift wavelength in metres.
uniform float4 anthology_snow_field;

float snow_field_hash(int2 cell)
{
    uint h = (uint)cell.x * 374761393u + (uint)cell.y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0x00ffffffu) * (1.0 / 16777216.0);
}
float snow_field_noise(float2 p)
{
    int2 cell = (int2)floor(p);
    float2 f = p - floor(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(snow_field_hash(cell), snow_field_hash(cell + int2(1,0)), u.x),
        lerp(snow_field_hash(cell + int2(0,1)), snow_field_hash(cell + 1), u.x), u.y);
}
float snow_field_depth(float2 worldXZ)
{
    float height = anthology_snow_field.x, density = anthology_snow_field.y;
    if (height <= 0.0 || density <= 0.0) return 0.0;
    float scale = max(0.5, anthology_snow_field.w);
    float coverage = density >= 1.0 ? 1.0 :
        smoothstep(0.0, 1.0, (snow_field_noise(worldXZ / (scale * 2.0) + float2(31,-17)) - (1.0-density)) * 5.0);
    float drift = .75 * snow_field_noise(worldXZ / scale) + .25 * snow_field_noise(worldXZ / (scale * .37));
    return height * coverage * lerp(1.0, .25 + .75 * drift, saturate(anthology_snow_field.z));
}
#endif
