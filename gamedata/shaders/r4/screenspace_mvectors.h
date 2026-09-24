/**
 * @ Version: SCREEN SPACE SHADERS - UPDATE 23
 * @ Description: Motion Vectors - Common
 * @ Modified time: 2025-04-20 07:36:37
 * @ Author: https://www.moddb.com/members/ascii1457
 * @ Mod: https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders
 */

#ifndef SSFX_MV_LOADED

	#define SSFX_MV_LOADED

	uniform float4x4 m_wvp_prev;
	uniform float4x4 m_vp_prev;
	uniform float4 ssfx_jitter;
	uniform float4 pip_motion_history; // Unknown motion, surface owner, reserved, reserved

	float4 ssfx_mv_calc(float4 current, float4 previous, float IsHUD, float TAAMask)
	{
		// Temporal reconstruction needs weapon/hand velocity too. Motion blur
		// excludes the HUD through .z, without destroying the shared vectors.
		float2 motion_vectors = (current.xy / current.w) - (previous.xy / previous.w);
		
		// Negative native masks identify foliage to SSS AO. A zero HUD/history
		// mask must not erase them; owner IDs occupy only the unmasked channel.
		float mask = TAAMask;
		if (IsHUD > 0.0f) mask = max(mask, saturate(IsHUD));
		if (pip_motion_history.x > 0.0f) mask = max(mask, pip_motion_history.x);
		if (mask == 0.0f)
			mask = pip_motion_history.y;
		return float4(float2(motion_vectors.x, -motion_vectors.y) * 0.5f, IsHUD, mask);
	}

	float2 ssfx_taa_jitter(float4 hpos)
	{
		return float2( hpos.xy + ssfx_jitter.xy * hpos.w );
	}

#endif
