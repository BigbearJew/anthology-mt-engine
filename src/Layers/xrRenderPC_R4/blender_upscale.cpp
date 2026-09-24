#include "stdafx.h"
#include "blender_upscale.h"

CBlender_upscale::CBlender_upscale(bool auxiliary) : m_auxiliary(auxiliary)
{
    description.CLS = 0;
}

void CBlender_upscale::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);
	if (m_auxiliary)
	{
		if (C.iElement == 0)
		{
			C.r_Pass("stub_screen_space", "anthology_upscale_hud_depth", FALSE, TRUE, TRUE);
			C.PassSET_ZB(TRUE, TRUE, FALSE);
			C.r_dx10Texture("s_depth", r4_RT_upscale_depth);
			C.r_dx10Sampler("smp_nofilter");
		}
		else
		{
			C.r_Pass("stub_screen_space", "anthology_upscale_resolve", FALSE, FALSE, FALSE);
			C.r_dx10Texture("s_image", r4_RT_upscale_output);
			C.r_dx10Texture("s_position", r2_RT_P);
			C.r_dx10Texture("s_motion", r2_RT_ssfx_motion_vectors);
			C.r_dx10Sampler("smp_nofilter");
			C.r_dx10Sampler("smp_rtlinear");
		}
		C.r_End();
		return;
	}

	if (C.iElement == 4 || C.iElement == 5)
	{
		const bool colorMap = C.iElement == 5;
		C.r_Pass("stub_notransform_postpr",
			colorMap ? "anthology_upscale_postprocess_cm" : "anthology_upscale_postprocess",
			FALSE, FALSE, FALSE, FALSE, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
		C.r_dx10Texture("s_base0", r4_RT_upscale_post);
		C.r_dx10Texture("s_base1", r4_RT_upscale_post);
		C.r_dx10Texture("s_noise", "fx\\fx_noise2");
		if (colorMap)
		{
			C.r_dx10Texture("s_grad0", "$user$cmap0");
			C.r_dx10Texture("s_grad1", "$user$cmap1");
		}
		C.r_dx10Sampler("smp_rtlinear");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		return;
	}

	if (C.iElement == 2)
	{
		// Native-resolution menu composition. The normal world generic targets
		// are core-sized while an upscaler is active, so the menu uses the
		// display-sized upscale output and PDA target instead.
		C.r_Pass("stub_notransform_t", "distort", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_base", r4_RT_upscale_post);
		C.r_dx10Texture("s_distort", r2_RT_ui);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		return;
	}

	if (C.iElement == 3)
	{
		// One MRT pass exports aligned FP16 color and sampled R32 device depth.
		C.r_Pass("stub_notransform_t", "anthology_upscale_prepare_depth", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_generic0);
		C.r_dx10Texture("s_depth", r2_RT_depth);
		C.r_End();
		return;
	}

	if (C.iElement == 1)
	{
		// Spatial SVP fallback only needs the UNORM-to-FP16 color conversion.
		C.r_Pass("stub_notransform_t", "anthology_upscale_prepare", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_generic0);
		C.r_End();
		return;
	}

    C.r_Pass("stub_screen_space", "anthology_upscale_copy", FALSE, FALSE, FALSE);
    C.r_dx10Texture("s_image", C.iElement == 0 ? r4_RT_upscale_input : r4_RT_upscale_output);
    C.r_dx10Sampler("smp_rtlinear");
    C.r_End();
}
