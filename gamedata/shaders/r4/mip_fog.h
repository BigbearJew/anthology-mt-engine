//=================================================================================================
//Mip Fog for STALKER Anomaly 
//Inspired by Uncharted 4
//=================================================================================================
#define HEIGHTFOGOFFSET -0.25
#define HEIGHTFOGSCALE 0.1 

#define FOGMAXSHARPNESS 0.3

#define FOGDENSITY 0.5
#define HEIGHTFOGDENSITY 0.5

#define MIPFOGAMOUNT 0.9
#define SUNFOGAMOUNT 0.5
//=================================================================================================

float Calc_Height(float3 wpos)
{
    float Height = HEIGHTFOGSCALE * wpos.y + HEIGHTFOGOFFSET;
    Height *= HEIGHTFOGDENSITY;
    return exp2(clamp(-Height,-32.0,16.0));
}

float Calc_FinalFog(float fog, float height)
{
    float envMax = max(0.01, max(env_color.r, max(env_color.g, env_color.b))) * 0.5;
    float fogMin = min(fog_color.r, min(fog_color.g, fog_color.b));
    
    float density = (fogMin / envMax) * FOGDENSITY;
    float Fog = exp2(-fog * height * density);
    return saturate(1.0 - Fog);
}

float3 Calc_SunFog(float3 pos, float fog, float fogrough)
{
    float sunDot = saturate(dot(normalize(Ldynamic_dir), -normalize(pos)));

    float a = RoughToGlossExp(fogrough);
    float sunSpec = D_Blinn(a, sunDot) / PI;
    
    float3 FogTint = SRGBToLinear(Ldynamic_color.rgb);
    
    return sunSpec * FogTint * SUNFOGAMOUNT;
}

float3 Calc_MipFog(float3 sky, float fog, float fogrough)
{

    // Cubemap projection
    float3 skyabs = abs(sky);
    float skymax = max(skyabs.x, max(skyabs.y, skyabs.z));
    sky /= max(skymax,1e-5);
    
    if (sky.y < 0.999) 
        sky.y = sky.y * 2.0 - 1.0; 
    
    float FogMip = fogrough * CUBE_MIPS;
    
    float3 FarTint  = SRGBToLinear(fog_color.rgb);
    float3 NearTint = min(env_color.rgb, FarTint);
    float3 FogTint  = lerp(NearTint, FarTint, fog); 
    
    float3 s0 = env_s0.SampleLevel(smp_base, sky, FogMip).rgb;
    float3 s1 = env_s1.SampleLevel(smp_base, sky, FogMip).rgb;
    
    float3 SkyLinear = SRGBToLinear(lerp(s0, s1, env_color.w));
    float3 MipFog    = SkyLinear * FogTint;

    return MipFog * MIPFOGAMOUNT;
}

float3 Calc_Fog_Linear(float3 pos, float3 color)
{
    float3 sky = mul(m_inv_V, pos);
    float3 wpos = sky + eye_position;
    float distance = length(pos);

    float fog = max(0.0, (distance * fog_params.w) / max(0.001, 1.0 - fog_params.x));
    float fogsat = saturate(fog);

    float height = Calc_Height(wpos);
    float FinalFog = Calc_FinalFog(fog, height);
    
    float fogrough = 1.0 - fogsat;
    fogrough = fogrough * fogrough; 
    fogrough = lerp(1.0 - FOGMAXSHARPNESS, 1.0, fogrough);
    
    float3 MipFog = Calc_MipFog(sky, fogsat, fogrough);
    float3 SunFog = Calc_SunFog(pos, fogsat, fogrough);
    
    float3 FogColor = MipFog + SunFog;
    
    float fogalpha = fogsat * fogsat;
    float3 FogBlend = FinalFog * (1.0 - fogalpha) + fogalpha;
    
    return lerp(color, FogColor, FogBlend);
}

float3 Calc_Fog(float3 pos, float3 color)
{
    return LinearTosRGB(Calc_Fog_Linear(pos, SRGBToLinear(color)));
}