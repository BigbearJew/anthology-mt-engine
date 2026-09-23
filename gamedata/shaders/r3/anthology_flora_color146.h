#ifndef ANTHOLOGY_FLORA_COLOR146
#define ANTHOLOGY_FLORA_COLOR146
// These are separate seasonal DDS resources, matching the SoC Update design.
// Missing season maps are bound to the stock base texture by the engine.
Texture2D s_base_green;
Texture2D s_base_autumn;
Texture2D s_base_dead;
Texture2D s_snow_tree;

uniform float4 anthology_flora_style;

#define ANTHOLOGY_SUMMER_COLOR_MOD_X float3(0.3f, 0.6075f, 0.25f)
#define ANTHOLOGY_SUMMER_COLOR_MOD_Y float3(0.3f, 0.6075f, 0.35f)
#define ANTHOLOGY_SUMMER_COLOR_MOD_Z float3(0.35f, 0.2275f, 0.25f)
#define ANTHOLOGY_AUTUMN_COLOR_MOD_X float3(0.25f, 0.4075f, 0.15f)
#define ANTHOLOGY_AUTUMN_COLOR_MOD_Y float3(0.5f, 0.2075f, 0.15f)
#define ANTHOLOGY_AUTUMN_COLOR_MOD_Z float3(0.3f, 0.2075f, 0.15f)
#define ANTHOLOGY_WINTER_COLOR_MOD float3(1.14f, 1.145f, 1.16f)

float4 anthology_flora_color( float2 tc )
{
    float4 flora;
    const float style = anthology_flora_style.x;

    // Default mode uses the original material without a seasonal map or tint.
    if (style >= 5.5f)
        return s_base.Sample(smp_base, tc);

    if (style < 1.5f)
    {
        flora = s_base_green.Sample(smp_base, tc);
        float3 modified =
            flora.xxx * ANTHOLOGY_SUMMER_COLOR_MOD_X +
            flora.yyy * ANTHOLOGY_SUMMER_COLOR_MOD_Y +
            flora.zzz * ANTHOLOGY_SUMMER_COLOR_MOD_Z * 5.0f;
        flora.xyz = lerp(flora.xyz, modified, 0.35f);
    }
    else if (style < 2.5f)
    {
        flora = s_base_autumn.Sample(smp_base, tc);
        float3 modified =
            flora.xxx * ANTHOLOGY_AUTUMN_COLOR_MOD_X +
            flora.yyy * ANTHOLOGY_AUTUMN_COLOR_MOD_Y +
            flora.zzz * ANTHOLOGY_AUTUMN_COLOR_MOD_Z * 5.0f;
        flora.xyz = lerp(flora.xyz, modified, 0.5f);
    }
    // Dead Autumn and Late Autumn share the authored dead foliage exactly.
    // Only mode 4 applies the winter tint and snow mask; mode 5 deliberately
    // keeps trees, bushes and level detail grass unmodified.
    else if (style < 3.5f || style >= 4.5f)
    {
        flora = s_base_dead.Sample(smp_base, tc);
    }
    else
    {
        const float3 snow = s_snow_tree.Sample(smp_base, tc).xyz;
        flora = s_base_dead.Sample(smp_base, tc);
        const float3 modified =
            flora.xxx * ANTHOLOGY_WINTER_COLOR_MOD +
            flora.xxx * ANTHOLOGY_WINTER_COLOR_MOD +
            flora.xxx * ANTHOLOGY_WINTER_COLOR_MOD;
        flora.xyz = lerp(flora.xyz, modified, 0.5f);
        flora.xyz = lerp(flora.xyz, modified * 1.5f, snow.x);
    }

    return flora;
}


#endif
