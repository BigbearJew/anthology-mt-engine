// Adapted from Ascii1457's Screen Space Shaders ICE VERSION (Update 13).
// https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders
#include "check_screenspace.h"
#include "anthology_ice_ray.h"
#if defined(SSFX_FOG) && !defined(ANTHOLOGY_ICE_FOG_READY)
#include "screenspace_fog.h"
#endif
uniform float4 anthology_flora_style;
Texture2D s_ice_normal;
Texture2D s_ice_cracks;
Texture2D s_ice_noise;
float4 anthology_ice(vf I)
{
	// 3d view space pos reconstruction math using object space
	float3 Pv = float3(I.tctexgen.z * (I.hpos.xy * pos_decompression_params.zw - pos_decompression_params.xy), I.tctexgen.z);

	// World Space UV
	float3 ws = mul(m_inv_V, Pv);
	float2 wUV = ws.xz + eye_position.xz;

	// Normal Texture
	float3	n0	= s_ice_normal.Sample(smp_base, wUV * 0.5f / G_SSR_ICE_BUMP_SIZE);
	float3	basenormal	= n0 * 2 - 1;

	// Load Data
	float2 PosTc = I.tctexgen.xy / I.tctexgen.z;
	gbuffer_data gbd = gbuffer_load_data( PosTc, I.hpos.xy );
	float4 _P = float4( gbd.P, 1.0f );

	// Bump intensity
	float3 Navg = normalize(float3(basenormal.x * G_SSR_ICE_BUMP_INTENSITY, basenormal.y * G_SSR_ICE_BUMP_INTENSITY, basenormal.z));

	float3	Nw	= mul (float3x3(I.M1, I.M2, I.M3), Navg);
			Nw	= normalize (Nw);
	
	float3 v2point	= normalize (I.v2point_w);
	float3 vreflect = reflect(v2point, Nw.xyz);

	float3 eyedir = normalize(Pv);
	float3 Nv = normalize(mul(m_V, Nw));
	
	// Some vars to put our reflection
	float3 refl_ray, refl_skybox;
	float3 reflection;
	float3 ssr_hit_uv = 0.0f;

#if 1
	
	// Blue Noise & Normal for noise
	float3 NN = normalize(float3(basenormal.x * 0.15f, basenormal.y * 0.15f, basenormal.z));
	float blue_noise = s_ice_noise.Sample(smp_linear, I.tbase * float2(0.05f, 0.05f) + NN).b * 1.5f;
	
	// Compute reflection bounce
	float3 wreflect = reflect(eyedir, Nv);

	// Don't trace rays which face the camera. Still worth to avoid the rays mess when you look down.
	float edgeFade = step(dot(-eyedir, wreflect), 0);
	
	// Trace a ray
	if (edgeFade > 0.02f)
		ssr_hit_uv = anthology_ice_ray(Pv, wreflect, blue_noise, 0);

	// Get current Skybox
	refl_skybox = SSFX_calc_sky(vreflect) * G_SSR_ICE_SKY_REFLECTION;

	if (all(ssr_hit_uv.xy))
	{
		// Get reflection pixel from scene screen
		refl_ray = SSFX_get_image(ssr_hit_uv.xy, 0);

		// Adjust reflection intensity using ssr_hit_uv.y and edgeFade
		float ray_fade = ssr_hit_uv.y * 5.0f;
		
		// Reflection fog fadeout
		float fog = 1.0 - saturate(( length(float3(Pv.x,Pv.y,ssr_hit_uv.z)) * fog_params.w + fog_params.x) * 1.4f);
		
		float refl_power = saturate(ray_fade * edgeFade * fog);
		
		// Fallback to Skybox
		reflection = lerp(refl_skybox, refl_ray * G_SSR_ICE_MAP_REFLECTION, refl_power);
	}
	else
	{
		// No reflection data, we use only refl_skybox
		reflection = refl_skybox;
	}

#else
	reflection = L_hemi_color.rgb;
#endif

	// Fresnel
	float fresnel		 = saturate (dot(vreflect, v2point));
	float fresnel_amount = pow(fresnel, 6);

	// Normal for the refraction
	float2 N_Scr = normalize(float3(basenormal.x * G_SSR_ICE_REFRACTION, basenormal.y * G_SSR_ICE_REFRACTION, basenormal.z)); 
	N_Scr -= float3(0.0125f, 0.0f, 0.0f);
	
	// Water Depth
	float waterDepth = saturate( _P.z - I.tctexgen.z );

	// Remove unwanted refractions... Only things under the water surface
	float refraction_discard = Pv.z < SSFX_get_depth(PosTc + N_Scr, 0);

	// Screen buffer
	float3 screen = SSFX_get_image(PosTc + N_Scr * refraction_discard, 0);
	
	// Detail texture ( Ice cracks )
	float3 base_detail = s_ice_cracks.Sample(smp_base, wUV * 0.5f / G_SSR_ICE_CRACKS_SIZE).rgb;

	// Fresnel Color
	float3 ice_color = lerp(G_SSR_ICE_FRESNEL_COLOR_CLOSE, G_SSR_ICE_FRESNEL_COLOR_FAR, fresnel_amount);
	
	// Parallax
	float3 parallax_detail = 0;

#if G_SSR_ICE_USE_PARALLAX == true
	float3	eye = mul (float3x3(I.M1.x, I.M2.x, I.M3.x,
								I.M1.y, I.M2.y, I.M3.y,
								I.M1.z, I.M2.z, I.M3.z), -I.v2point_w.xyz );

	float2	pDelta	= normalize(eye).xy * -(0.06f * G_SSR_ICE_PARALLAX_DEPTH); // "Depth" intensity
	float2	pOffsetStep	= (1.0f / G_SSR_ICE_PARALLAX_STEPS) * pDelta; // How much we advance per step
 	float2	pOffset	= wUV * 0.5f / G_SSR_ICE_CRACKS_SIZE + N_Scr; // Init UV

	[unroll (G_SSR_ICE_PARALLAX_STEPS)]
	for (int i = 0; i < G_SSR_ICE_PARALLAX_STEPS; i++)
	{
		pOffset += pOffsetStep; // Offset texture
		parallax_detail += s_ice_cracks.Sample(smp_base, pOffset.xy); // Sample and accumulate
	}

	// Normalize
	parallax_detail /= G_SSR_ICE_PARALLAX_STEPS;

	// Parallax intensity
	parallax_detail *= saturate(G_SSR_ICE_PARALLAX_INTENSITY);
#endif

	// Some level water meshes have no baked hemisphere light. Use the current
	// weather's diffuse sky there; a constant floor would make ice glow at night.
	float3 sky_light = L_ambient.rgb + L_hemi_color.rgb * (0.5f + 0.5f * abs(Nw.y));
	float3 ice_light = max(I.c0.rgb, sky_light);
	float3 light = saturate(dot(ice_light, float3(0.5f, 0.5f, 0.5f)));

	// Falloff to fade detail textures through the distance
	float falloff = fresnel_amount * 0.5f;
	
	// Detail Texture falloff and intensity adjust
	base_detail *= G_SSR_ICE_CRACKS_INTESNITY * (1.0f - falloff);

	// Refraction
	float3  acc = lerp(ice_color * light, screen, saturate(G_SSR_ICE_TRANSPARENCY) );

	// Add ice cracks
	acc += base_detail * light;

	// Reflection intensity
	acc = lerp(acc, reflection, fresnel_amount * G_SSR_ICE_REFLECTION);

	// Parallax
#if G_SSR_ICE_USE_PARALLAX == true
	acc = lerp(parallax_detail * light, acc, saturate(0.5f + falloff));
#endif

	// Ice border at the end to affect everything.
	acc = lerp(acc, screen, smoothstep(G_SSR_ICE_BORDER, 0.0f, waterDepth));

	// Specular
#if 1
	float3 Nf = normalize(float3(Nw.x * 1.5f, Nw.y * 0.5f, Nw.z)); // flatten the water normal to get better specular
	float3 spec = vibrance(L_sun_color.rgb, G_SSR_ICE_SPECULAR_VIBRANCE) * pow(abs(dot(normalize(v2point + L_sun_dir_w), Nf)), 512);
	acc += spec * G_SSR_ICE_SPECULAR_INTENSITY;
#endif

	// Alpha border
	//float border_alpha = smoothstep( 0.0f , 0.04f, waterDepth);
	
#ifdef SSFX_FOG
	float3 w_s = mul(m_inv_V, Pv );
	float fogging = SSFX_FOGGING(1.0 - I.fog, w_s.y);
#else
	float fogging = I.fog;
#endif

	// Done
	return  float4(acc, fogging  * fogging); // border_alpha
}
