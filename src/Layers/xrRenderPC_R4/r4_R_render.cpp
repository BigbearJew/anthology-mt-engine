#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../xrRender/FBasicVisual.h"
#include "../../xrEngine/customhud.h"
#include "../../xrEngine/xr_object.h"
#include "../../xrEngine/IRenderable.h"
#include "../../xrEngine/SvpMotionEpoch.h"
#include "../../xrEngine/EngineThreading.h"
#include "../xrRender/SkeletonCustom.h"
#include "../../xrParticles/ParticlesAsyncManager.h"

#include "../xrRender/QueryHelper.h"
#include "UpscalerRuntime.h"
#include <dxgi1_4.h>

namespace
{
enum ERenderPhaseProfile : u32
{
	RenderPhaseVisibility,
	RenderPhaseGBuffer,
	RenderPhaseLightVisibility,
	RenderPhaseSSS,
	RenderPhaseSun,
	RenderPhaseLocalLights,
	RenderPhaseCombine,
	RenderPhaseHud,
	RenderPhaseCount
};

struct SRenderPhaseAccumulator
{
	u32 frames = 0;
	u64 ticks[RenderPhaseCount] = {};
	u64 maxTicks[RenderPhaseCount] = {};
	u64 drawCalls[RenderPhaseCount] = {};
	u64 frameDrawCalls = 0;
	u64 staticDips = 0;
	u64 dynamicDips = 0;
	u64 detailDips = 0;
	u64 localLights = 0;
};

struct SRenderPhaseToken
{
	ERenderPhaseProfile phase;
	u64 startedAt;
	u32 drawCalls;
	bool enabled;
};

SRenderPhaseAccumulator g_renderPhaseProfile;

bool RenderPhaseProfileEnabled()
{
	return mt_FrameProfile && mt_FrameProfileDetailed && CPU::qpc_freq &&
		!Device.dwPrecacheFrame && !Device.m_SecondViewport.IsSVPFrame();
}

SRenderPhaseToken BeginRenderPhase(ERenderPhaseProfile phase)
{
	const bool enabled = RenderPhaseProfileEnabled();
	return {phase, enabled ? CPU::QPC() : 0, enabled ? RCache.stat.calls : 0, enabled};
}

void EndRenderPhase(const SRenderPhaseToken& token)
{
	if (!token.enabled)
		return;

	const u64 elapsed = CPU::QPC() - token.startedAt;
	g_renderPhaseProfile.ticks[token.phase] += elapsed;
	g_renderPhaseProfile.maxTicks[token.phase] =
		std::max(g_renderPhaseProfile.maxTicks[token.phase], elapsed);
	g_renderPhaseProfile.drawCalls[token.phase] += RCache.stat.calls - token.drawCalls;
}

void FinishRenderPhaseFrame(u32 localLights)
{
	if (!RenderPhaseProfileEnabled())
		return;

	SRenderPhaseAccumulator& profile = g_renderPhaseProfile;
	++profile.frames;
	profile.frameDrawCalls += RCache.stat.calls;
	profile.staticDips += RCache.stat.r.s_static.dips;
	profile.dynamicDips += RCache.stat.r.s_dynamic.dips;
	profile.detailDips += RCache.stat.r.s_details.dips;
	profile.localLights += localLights;

	if (profile.frames < 300)
		return;

	const double averageMs = 1000.0 / (double(CPU::qpc_freq) * double(profile.frames));
	const double maximumMs = 1000.0 / double(CPU::qpc_freq);
	Msg("* [render-phase/profile] avg-ms visibility/gbuffer/light-vis/sss/sun/local/combine/hud="
		"%.2f/%.2f/%.2f/%.2f/%.2f/%.2f/%.2f/%.2f",
		profile.ticks[RenderPhaseVisibility] * averageMs,
		profile.ticks[RenderPhaseGBuffer] * averageMs,
		profile.ticks[RenderPhaseLightVisibility] * averageMs,
		profile.ticks[RenderPhaseSSS] * averageMs,
		profile.ticks[RenderPhaseSun] * averageMs,
		profile.ticks[RenderPhaseLocalLights] * averageMs,
		profile.ticks[RenderPhaseCombine] * averageMs,
		profile.ticks[RenderPhaseHud] * averageMs);
	Msg("* [render-phase/profile] max-ms visibility/gbuffer/light-vis/sss/sun/local/combine/hud="
		"%.2f/%.2f/%.2f/%.2f/%.2f/%.2f/%.2f/%.2f",
		profile.maxTicks[RenderPhaseVisibility] * maximumMs,
		profile.maxTicks[RenderPhaseGBuffer] * maximumMs,
		profile.maxTicks[RenderPhaseLightVisibility] * maximumMs,
		profile.maxTicks[RenderPhaseSSS] * maximumMs,
		profile.maxTicks[RenderPhaseSun] * maximumMs,
		profile.maxTicks[RenderPhaseLocalLights] * maximumMs,
		profile.maxTicks[RenderPhaseCombine] * maximumMs,
		profile.maxTicks[RenderPhaseHud] * maximumMs);
	Msg("* [render-phase/profile] avg-work draws/static/dynamic/details/local-lights="
		"%.1f/%.1f/%.1f/%.1f/%.1f phase-draws(gbuffer/sun/local/combine)=%.1f/%.1f/%.1f/%.1f",
		double(profile.frameDrawCalls) / profile.frames,
		double(profile.staticDips) / profile.frames,
		double(profile.dynamicDips) / profile.frames,
		double(profile.detailDips) / profile.frames,
		double(profile.localLights) / profile.frames,
		double(profile.drawCalls[RenderPhaseGBuffer]) / profile.frames,
		double(profile.drawCalls[RenderPhaseSun]) / profile.frames,
		double(profile.drawCalls[RenderPhaseLocalLights]) / profile.frames,
		double(profile.drawCalls[RenderPhaseCombine]) / profile.frames);
	profile = {};
}

class SvpQualityPassScope
{
	CRenderTarget* target;
public:
	explicit SvpQualityPassScope(CRenderTarget* value) : target(value && value->begin_svp_quality_pass() ? value : nullptr) {}
	~SvpQualityPassScope() { restore(); }
	void restore()
	{
		if (target)
		{
			target->end_svp_quality_pass();
			target = nullptr;
		}
	}
};

class MainSceneGpuProfileScope
{
	const bool mainView;
public:
	MainSceneGpuProfileScope() : mainView(!Device.m_SecondViewport.IsSVPFrame())
	{
		if (mainView)
			g_AnthologyUpscaler.BeginSceneGpuProfile();
	}
	~MainSceneGpuProfileScope()
	{
		if (mainView)
			g_AnthologyUpscaler.EndSceneGpuProfile();
	}
};
}

void CRender::render_menu()
{
	PIX_EVENT(render_menu);
	//	Globals
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	const bool nativeMenu = Target->upscaler_active();
	const ref_rt& menuColor = nativeMenu ? Target->rt_UpscalePost : Target->rt_Generic_0;
	const ref_rt& menuDistortion = nativeMenu ? Target->rt_ui_pda : Target->rt_Generic_1;
	ID3DDepthStencilView* menuDepth = nativeMenu ? nullptr : Target->main_depth();
	if (nativeMenu)
	{
		// Both native menu targets were SRVs during the previous composition.
		// Commit their unbind before either resource becomes an RTV again.
		RCache.set_Textures(nullptr);
		SRVSManager.Apply();
	}

	// Main Render
	{
		// The world buffers are intentionally low resolution with DLSS/FSR, but
		// menu text and controls must remain at the display resolution. Reuse the
		// full-size upscale output as a private menu color target.
		Target->u_setrt(menuColor, 0, 0, menuDepth);
		if (nativeMenu)
			rmNormal();
		g_pGamePersistent->OnRenderPPUI_main(); // PP-UI
	}

	// Distort
	{
		FLOAT ColorRGBA[4] = {127.0f / 255.0f, 127.0f / 255.0f, 0.0f, 127.0f / 255.0f};
		// rt_ui_pda is also display-sized and is idle while the main menu is
		// rendered, so it can hold the native distortion/magnifier mask without
		// allocating another permanent full-resolution render target.
		Target->u_setrt(menuDistortion, 0, 0, menuDepth);
		if (nativeMenu)
			rmNormal();
		HW.pContext->ClearRenderTargetView(menuDistortion->pRT, ColorRGBA);
		g_pGamePersistent->OnRenderPPUI_PP(); // PP-UI
	}

	// Actual Display
	Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
	rmNormal();
	if (nativeMenu)
		RCache.set_Element(Target->upscaler_menu_element());
	else
		RCache.set_Shader(Target->s_menu);
	RCache.set_Geometry(Target->g_menu);

	Fvector2 p0, p1;
	u32 Offset;
	auto C = color_rgba(255, 255, 255, 255);
	float _w = float(Device.dwWidth);
	float _h = float(Device.dwHeight);
	float d_Z = EPS_S;
	float d_W = 1.f;
	p0.set(.5f / _w, .5f / _h);
	p1.set((_w + .5f) / _w, (_h + .5f) / _h);

	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, Target->g_menu->vb_stride, Offset);
	pv->set(EPS, float(_h + EPS), d_Z, d_W, C, p0.x, p1.y);
	pv++;
	pv->set(EPS, EPS, d_Z, d_W, C, p0.x, p0.y);
	pv++;
	pv->set(float(_w + EPS), float(_h + EPS), d_Z, d_W, C, p1.x, p1.y);
	pv++;
	pv->set(float(_w + EPS), EPS, d_Z, d_W, C, p1.x, p0.y);
	pv++;
	RCache.Vertex.Unlock(4, Target->g_menu->vb_stride);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

extern u32 g_r;

void CRender::Render()
{
	PIX_EVENT(CRender_Render);
	Target->m_svpSceneFrame = u32(-1);

	rmNormal();

	bool _menu_pp = g_pGamePersistent ? g_pGamePersistent->OnRenderPPUI_query() : false;
	if (_menu_pp)
	{
		Device.m_SecondViewport.InvalidateSVPContent();
		render_menu();
		return;
	};

	IMainMenu* pMainMenu = g_pGamePersistent ? g_pGamePersistent->m_pMainMenu : 0;
	bool bMenu = pMainMenu ? pMainMenu->CanSkipSceneRendering() : false;

	if (!(g_pGameLevel && g_hud)
		|| bMenu)
	{
		Device.m_SecondViewport.InvalidateSVPContent();
		Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
		return;
	}

	if (m_bFirstFrameAfterReset)
	{
		Device.m_SecondViewport.InvalidateSVPContent();
		for (light* L : v_all_lights)//critical!!!
			L->m_moving_frames = 0;

		m_bFirstFrameAfterReset = false;
		return;
	}

	MainSceneGpuProfileScope mainSceneGpuProfile;
	if (Target->upscaler_active() && !Device.m_SecondViewport.IsSVPFrame())
		g_AnthologyUpscaler.UpdateJitter(Device.dwFrame);

	SvpQualityPassScope svpQualityScope(Target);

	//.	VERIFY					(g_pGameLevel && g_pGameLevel->pHUD);

	// Configure
	RImplementation.o.distortion = FALSE; // disable distorion
	Fcolor sun_color = ((light*)Lights.sun_adapted._get())->color;
	BOOL bSUN = ps_r2_ls_flags.test(R2FLAG_SUN) && (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b)>EPS) && !Core.ParamsData.test(ECoreParams::r4_dev);
	if (o.sunstatic) bSUN = FALSE;
	// Msg						("sstatic: %s, sun: %s",o.sunstatic?;"true":"false", bSUN?"true":"false");

	const SRenderPhaseToken visibilityPhase = BeginRenderPhase(RenderPhaseVisibility);
	// HOM
	ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
	HOM.Enable();
	HOM.Render(ViewBase);

	Target->phase_scene_prepare();

	//******* Main calc - DEFERRER RENDERER
	phase = PHASE_NORMAL;
	// phase_upscale leaves a display-sized viewport for the native HUD. The
	// scene targets above are core-sized, so restore their viewport before the
	// first world draw of the next frame.
	rmNormal();
	
	/*if (RImplementation.o.ssfx_core) // SSS23: DEPRECATED
	{
		// HUD Masking rendering
		FLOAT ColorRGBA[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
		HW.pContext->ClearRenderTargetView(Target->rt_ssfx_hud->pRT, ColorRGBA);

		Target->u_setrt(Target->rt_ssfx_hud, NULL, NULL, HW.pBaseZB);
		r_dsgraph_render_hud(true);

		// Reset Depth
		HW.pContext->ClearDepthStencilView(HW.pBaseZB, D3D_CLEAR_DEPTH, 1.0f, 0);
	}*/

    GMBase.traverse(RImplementation.pLastSector, ViewBase, Device.vCameraPosition, Device.mFullTransform);
    GMBase.r_dsgraph_capture_static();
    GMBase.r_dsgraph_capture_dynamic();
	EndRenderPhase(visibilityPhase);

	const SRenderPhaseToken firstGBufferPhase = BeginRenderPhase(RenderPhaseGBuffer);
    if (RImplementation.o.ssfx_motionvectors)
    {
		Target->u_setrt(Target->get_core_width(), Target->get_core_height(), 0, 0, Target->rt_ssfx_motion_vectors->pRT, 0);

        FLOAT ColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        HW.pContext->ClearRenderTargetView(Target->rt_ssfx_motion_vectors->pRT, ColorRGBA);

        RCache.set_Stencil(FALSE);
        g_pGamePersistent->Environment().RenderSky(true);

        RCache.Index.Flush();
        RCache.Vertex.Flush();

        RCache.set_xform_world(Fidentity);
    }

	if (ps_r2_ls_flags.test(R2FLAG_TERRAIN_PREPASS))
	{
		Target->u_setrt(Target->get_core_width(), Target->get_core_height(), NULL, NULL, NULL,
			!RImplementation.o.dx10_msaa ? Target->main_depth() : Target->rt_MSAADepth->pZRT);
	}

	//******* Main render :: PART-0	-- first
	{
		PIX_EVENT(DEFER_PART0_SPLIT);
		// level, SPLIT
		Target->phase_scene_begin();
		GMBase.r_dsgraph_render_static(0);
		GMBase.r_dsgraph_render_dynamic(0);
		Target->disable_aniso();
	}
	EndRenderPhase(firstGBufferPhase);

	//  Redotix99: for 3D Shader Based Scopes 	
	if (scope_3D_fake_enabled)
	{
		ID3D11Resource* zbuffer_res;
		Target->main_depth()->GetResource(&zbuffer_res);
		HW.pContext->CopyResource(RImplementation.Target->rt_tempzb->pSurface, zbuffer_res);
	}

	if (RImplementation.o.dx10_msaa)
		RCache.set_ZB(RImplementation.Target->rt_MSAADepth->pZRT);

	{
		PIX_EVENT(DEFER_TEST_LIGHT_VIS);
		const SRenderPhaseToken lightVisibilityPhase = BeginRenderPhase(RenderPhaseLightVisibility);
		//******* Occlusion testing of volume-limited light-sources
		Target->phase_occq();
		LP_normal.clear();
		LP_pending.clear();
		GMBase.r_dsgraph_capture_lights();
		EndRenderPhase(lightVisibilityPhase);
	}

	const SRenderPhaseToken secondGBufferPhase = BeginRenderPhase(RenderPhaseGBuffer);
	//******* Main render :: PART-1 (second)
	{
		PIX_EVENT(DEFER_PART1_SPLIT);
		// level
		Target->phase_scene_begin();
		GMBase.r_dsgraph_capture_hud();
		GMBase.r_dsgraph_render_hud();
		GMBase.r_dsgraph_render_lods(true,true);
		// Details/grass are one of the largest avoidable draw-list costs in the
		// second camera.  Balanced and Performance deliberately omit them while
		// preserving the world geometry, lighting and the native PiP cadence.
		const bool reduced_svp_details = Device.m_SecondViewport.IsSVPFrame() &&
			ScopeLenseQualityTier() >= 2;
		if (Details && !reduced_svp_details)
			Details->Render();
		Target->phase_scene_end();
	}

	// Wall marks
	const bool reduced_svp_wallmarks = Device.m_SecondViewport.IsSVPFrame() &&
		ScopeLenseQualityTier() >= 3;
	if (Wallmarks && !reduced_svp_wallmarks)
	{
		PIX_EVENT(DEFER_WALLMARKS);
		Target->phase_wallmarks();

		Wallmarks->Render(); // wallmarks has priority as normal geometry
	}

	// full screen pass to mark msaa-edge pixels in highest stencil bit
	if (RImplementation.o.dx10_msaa)
	{
		PIX_EVENT(MARK_MSAA_EDGES);
		Target->mark_msaa_edges();
	}

	//	TODO: DX10: Implement DX10 rain.
	if (ps_r2_ls_flags.test(R3FLAG_DYN_WET_SURF))
	{
		PIX_EVENT(DEFER_RAIN);
		render_rain();
	}
	EndRenderPhase(secondGBufferPhase);

	const SRenderPhaseToken sssPhase = BeginRenderPhase(RenderPhaseSSS);
	{
		// Save previus and current matrices
		{
			static Fmatrix mm_saved_viewproj;

			if (!Device.m_SecondViewport.IsSVPFrame())
			{
				Target->Matrix_previous.mul(mm_saved_viewproj, Device.mInvView);
				Target->Matrix_current.set(Device.mProject);
				mm_saved_viewproj.set(Device.mFullTransform);
			}
		}

		if (RImplementation.o.ssfx_sss && !Device.m_SecondViewport.IsSVPFrame())
		{
			static bool sss_rendered, sss_extended_rendered;

			// SSS Shadows
			if (ps_ssfx_sss_quality.z > 0)
			{
				Target->phase_ssfx_sss();
				sss_rendered = true;
			}
			else
			{
				if (sss_rendered) // Clear buffer
				{
					sss_rendered = false;
					FLOAT ColorRGBA[4] = { 1,1,1,1 };
					HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss->pRT, ColorRGBA);
				}
			}

			if (ps_ssfx_sss_quality.w > 0)
			{
				// Extra lights
				Target->phase_ssfx_sss_ext(RImplementation.LP_normal);
				sss_extended_rendered = true;
			}
			else
			{
				if (sss_extended_rendered) // Clear buffer
				{
					sss_extended_rendered = false;
					FLOAT ColorRGBA[4] = { 1,1,1,1 };
					HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss_tmp->pRT, ColorRGBA);
				}
			}
		}
		else if (RImplementation.o.ssfx_sss)
		{
			// PiP uses the current capture only. Its shadow target is isolated even
			// at 100% resolution; main-camera temporal history is never overwritten.
			if (ps_ssfx_sss_quality.z > 0)
				Target->phase_ssfx_sss();
			else
			{
				const float neutral[4] = { 1, 1, 1, 1 };
				HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss->pRT, neutral);
			}
			FLOAT NeutralLocalSSS[4] = { 1, 1, 1, 1 };
			HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss_tmp->pRT, NeutralLocalSSS);
		}
	}
	EndRenderPhase(sssPhase);

	// Directional light - fucking sun
	const SRenderPhaseToken sunPhase = BeginRenderPhase(RenderPhaseSun);
	if (bSUN) //bSUN && Device.dwFrame & 1 --Delayed sun update. Worth to check it in future
	{
		PIX_EVENT(DEFER_SUN);
		RImplementation.stats.l_visible ++;
		// render_sun_cascades also performs direct-light accumulation; skipping it
		// would leave reduced-quality PiP without direct sun even when cached shadow
		// maps exist. Keep this coherent until a separate cached-cascade accumulation
		// path is implemented.
		render_sun_cascades();
		Target->increment_light_marker();
		Target->accum_direct_blend();
	}
	EndRenderPhase(sunPhase);

	phase = PHASE_NORMAL;
	const SRenderPhaseToken localLightPhase = BeginRenderPhase(RenderPhaseLocalLights);

	{
		PIX_EVENT(DEFER_SELF_ILLUM);
		Target->phase_accumulator();
		// Render emissive geometry, stencil - write 0x0 at pixel pos
		RCache.set_xform_project(Device.mProject);
		RCache.set_xform_view(Device.mView);
		// Stencil - write 0x1 at pixel pos - 
		if (!RImplementation.o.dx10_msaa)
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		else
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		//RCache.set_Stencil				(TRUE,D3DCMP_ALWAYS,0x00,0xff,0xff,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,D3DSTENCILOP_KEEP);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_ColorWriteEnable();
		GMBase.r_dsgraph_render_emissive(RImplementation.o.ssfx_bloom ? false : true);
	}

	// Tiers 2/3 deliberately skip SSFX bloom in phase_combine() and clear its
	// output. Do not submit the bloom-only emissive geometry when it has no
	// consumer; the regular emissive pass above is left unchanged.
	const bool reduced_svp_bloom = Device.m_SecondViewport.IsSVPFrame() &&
		ScopeLenseQualityTier() >= 2;
	if (RImplementation.o.ssfx_bloom && !reduced_svp_bloom)
	{
		// Render Emissive on `rt_ssfx_bloom_emissive`
		FLOAT ColorRGBA[4] = { 0,0,0,0 };
		HW.pContext->ClearRenderTargetView(Target->rt_ssfx_bloom_emissive->pRT, ColorRGBA);
		Target->u_setrt(Target->rt_ssfx_bloom_emissive, NULL, NULL,
			!RImplementation.o.dx10_msaa ? Target->main_depth() : Target->rt_MSAADepth->pZRT);
		GMBase.r_dsgraph_render_emissive(true, true);
	}

	// Lighting, non dependant on OCCQ
	{
		PIX_EVENT(DEFER_LIGHT_NO_OCCQ);
		Target->phase_accumulator();
		render_lights(LP_normal);
	}

	// Lighting, dependant on OCCQ
	{
		PIX_EVENT(DEFER_LIGHT_OCCQ);
		render_lights(LP_pending);
	}

	{
		const bool reduced_svp_volumetrics = Device.m_SecondViewport.IsSVPFrame() &&
			ScopeLenseQualityTier() >= 2;
		if (RImplementation.o.ssfx_volumetric && !reduced_svp_volumetrics)
			Target->phase_ssfx_volumetric_blur();
	}
	EndRenderPhase(localLightPhase);

	phase = PHASE_NORMAL;

	// Postprocess
	{
		PIX_EVENT(DEFER_LIGHT_COMBINE);
		const SRenderPhaseToken combinePhase = BeginRenderPhase(RenderPhaseCombine);
		Target->phase_combine();
		EndRenderPhase(combinePhase);
	}
	svpQualityScope.restore();

	if (Details)
		Details->details_clear();

	if (g_hud)
	{
		const SRenderPhaseToken hudPhase = BeginRenderPhase(RenderPhaseHud);
		if (g_hud->RenderActiveItemUIQuery())
			GMBase.r_dsgraph_render_hud_ui();
		if (g_hud->RenderCamAttachedUIQuery())
			GMBase.r_dsgraph_render_cam_ui();
		EndRenderPhase(hudPhase);
	}
	FinishRenderPhaseFrame(u32(LP_normal.v_point.size() + LP_normal.v_spot.size() +
		LP_normal.v_shadowed.size() + LP_pending.v_point.size() +
		LP_pending.v_spot.size() + LP_pending.v_shadowed.size()));

}
#include "../xrRender/CHudInitializer.h"

void CRender::render_forward()
{
	RImplementation.o.distortion = RImplementation.o.distortion_enabled; // enable distorion

	//******* Main render - second order geometry (the one, that doesn't support deffering)
	//.todo: should be done inside "combine" with estimation of of luminance, tone-mapping, etc.
	{
		// level
		phase = PHASE_NORMAL;
		//	Igor: we don't want to render old lods on next frame.
		GMBase.r_dsgraph_render_static(1); // normal level, secondary priority
		CParticlesAsync::Wait();
		GMBase.r_dsgraph_render_dynamic(1);
		GMBase.fade_render(); // faded-portals
		GMBase.r_dsgraph_render_sorted(false); // strict-sorted geoms
		g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts
		// The contribution belongs to world effects; exclude weapon glass and UI.
		Target->end_svp_live_effects();
		GMBase.r_dsgraph_render_sorted_hud();
	}

	RImplementation.o.distortion = FALSE; // disable distorion
}

// Redotix99: for 3D Shader Based Scopes
void CRender::render_Reticle()
{
	VERIFY(0 == GMBase.RGraph.mapHUDSorted.Distort.size() + GMBase.RGraph.mapStaticSorted.Distort.size() + GMBase.RGraph.mapDynamicSorted.Distort.size());
	RImplementation.o.distortion = RImplementation.o.distortion_enabled;

	GMBase.r_dsgraph_render_ScopeSorted();

	RImplementation.o.distortion = FALSE;
}

void CRenderTarget::phase_svp_quality(ID3D11Texture2D* source)
{
	HW.pContext->CopyResource(rt_secondVP_capture->pSurface, source);

	u32 offset = 0;
	const u32 color = color_rgba(255, 255, 255, 255);
	const float width = float(rt_secondVP->dwWidth);
	const float height = float(rt_secondVP->dwHeight);

	u_setrt(rt_secondVP, nullptr, nullptr, nullptr);
	RImplementation.rmNormal();
	// The scene is rendered into the upper-left scaled viewport. The resolve
	// itself must cover the complete persistent lens texture.
	D3D_VIEWPORT resolve_viewport = {0.0f, 0.0f, width, height, 0.0f, 1.0f};
	HW.pContext->RSSetViewports(1, &resolve_viewport);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	FVF::TL* vertices = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
	vertices->set(0.0f, height, EPS_S, 1.0f, color, 0.0f, 1.0f); ++vertices;
	vertices->set(0.0f, 0.0f, EPS_S, 1.0f, color, 0.0f, 0.0f); ++vertices;
	vertices->set(width, height, EPS_S, 1.0f, color, 1.0f, 1.0f); ++vertices;
	vertices->set(width, 0.0f, EPS_S, 1.0f, color, 1.0f, 0.0f);
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	RCache.set_Element(s_svp_quality->E[0]);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
}

void CRenderTarget::draw_svp_scene(const ref_rt& target, int element)
{
	if (element == 1)
		u_setrt(target, rt_svpMotionOwner, nullptr, nullptr);
	else
		u_setrt(target, nullptr, nullptr, nullptr);
	D3D_VIEWPORT viewport = {0.f, 0.f, float(target->dwWidth), float(target->dwHeight), 0.f, 1.f};
	HW.pContext->RSSetViewports(1, &viewport);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	u32 offset = 0;
	const u32 color = color_rgba(255, 255, 255, 255);
	FVF::TL* vertices = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
	vertices->set(0.f, 1.f, 0.f, 1.f, color, 0.f, 1.f); ++vertices;
	vertices->set(0.f, 0.f, 0.f, 1.f, color, 0.f, 0.f); ++vertices;
	vertices->set(1.f, 1.f, 0.f, 1.f, color, 1.f, 1.f); ++vertices;
	vertices->set(1.f, 0.f, 0.f, 1.f, color, 1.f, 0.f);
	RCache.Vertex.Unlock(4, g_combine->vb_stride);
	if (element >= 0)
		RCache.set_Element(s_svp_quality->E[element]);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
}

bool CRenderTarget::svp_scene_capture_required() const
{
	const bool headNvg = (ps_scope_lense_allow_nvg && ps_scope_lense_head_nvg_active) || ps_r2_nightvision > 0;
	// Native optic NVG is applied by the lens shader after scene sampling.
	// Keep engine imaging and HDR output in their established display domain.
	return !Device.m_SecondViewport.IsSVPThermal() && !headNvg &&
		ps_r2_heatvision == 0 && !RImplementation.o.dx11_hdr10;
}

void CRenderTarget::phase_svp_scene()
{
	m_svpSceneFrame = u32(-1);
	m_svpMotionDepthFrame = u32(-1);
	if (!svp_scene_capture_required() || !rt_secondVP_scene || !rt_secondVP_scene->valid())
		return;
	// Ordinary optics return before the display SMAA pass in phase_combine.
	// Resolve edges here, once per fresh capture, in the active PiP RT bank.
	const bool liveTaa = ps_ssfx_taa.x > 0 && RImplementation.o.ssfx_motionvectors &&
		!RImplementation.o.dx10_msaa;
	static const bool captureDiagnostics = strstr(Core.Params, "-season_diagnostics") != nullptr;
	static u32 lastCaptureSettings = u32(-1);
	const u32 captureSettings = u32(ps_scope_lense_temporal_mode) |
		(u32(ps_scope_lense_update_interval) << 4) | (u32(liveTaa) << 12);
	if (captureDiagnostics && lastCaptureSettings != captureSettings)
	{
		lastCaptureSettings = captureSettings;
		Msg("* [PiP/capture-AA] enabled=%u mode=%d interval=%d size=%ux%u",
			u32(liveTaa), ps_scope_lense_temporal_mode, ps_scope_lense_update_interval, m_renderWidth, m_renderHeight);
	}
	if (!liveTaa) phase_smaa();
	RCache.set_Stencil(FALSE);
	// Read generic0 while it still belongs to the active PiP bank, before LUT,
	// bloom composition and postprocess. Main view runs those effects once.
	unbind_svp_resources();
	if (liveTaa)
	{
		if (!t_svpTemporalPrevious) t_svpTemporalPrevious.create("$user$svp_taa_previous");
		if (!rt_svpTemporal[0] || rt_svpTemporal[0]->dwWidth != m_renderWidth || rt_svpTemporal[0]->dwHeight != m_renderHeight)
		{
			t_svpTemporalPrevious->surface_set(nullptr);
            rt_svpTemporal[0].destroy(); rt_svpTemporal[1].destroy();
            rt_secondVP_scene.destroy();
            rt_secondVP_scene.create("$user$viewport2_scene", m_renderWidth, m_renderHeight, rt_secondVP->fmt);
            rt_svpTemporal[0].create("$user$svp_taa0", m_renderWidth, m_renderHeight, D3DFMT_A16B16G16R16F);
			rt_svpTemporal[1].create("$user$svp_taa1", m_renderWidth, m_renderHeight, D3DFMT_A16B16G16R16F);
			const float clear[4] = {};
			HW.pContext->ClearRenderTargetView(rt_svpTemporal[0]->pRT, clear);
			HW.pContext->ClearRenderTargetView(rt_svpTemporal[1]->pRT, clear);
		}
		// History advances on captures, not presentation frames. Interval 4 must
		// retain its preceding capture instead of invalidating it every time.
		const u32 captureGap = u32(_max(1, ps_scope_lense_update_interval)) + 1;
		const bool valid = Device.dwFrame - m_svpTemporalFrame <= captureGap && !Device.dwPrecacheFrame &&
			m_svpTemporalWidth == m_renderWidth &&
			_abs(m_svpTemporalProjection._11 - Device.mProject._11) < .001f;
		Fmatrix inverse, currentToPrevious;
		inverse.invert(Device.mView);
		currentToPrevious.mul(valid ? m_svpTemporalView : Device.mView, inverse);
		t_svpTemporalPrevious->surface_set(rt_svpTemporal[m_svpTemporalIndex]->pSurface);
		RCache.set_Element(s_svp_quality->E[4]);
		RCache.set_c("svp_taa_control", valid ? 1.f : 0.f,
			m_svpReactiveMaskFrame[1] == Device.dwFrame ? 1.f : 0.f, 0.f, 0.f);
		RCache.set_c("svp_taa_previous_view", currentToPrevious);
		const u32 next = m_svpTemporalIndex ^ 1;
		draw_svp_scene(rt_svpTemporal[next], -1);
		unbind_svp_resources();
		t_svpTemporalPrevious->surface_set(nullptr);
		// Convert FP16 history into the existing lens texture format.
		rt_Generic_0->pTexture->surface_set(rt_svpTemporal[next]->pSurface);
		draw_svp_scene(rt_secondVP_scene);
		unbind_svp_resources();
		rt_Generic_0->pTexture->surface_set(rt_Generic_0->pSurface);
		m_svpTemporalIndex = next;
		m_svpTemporalFrame = Device.dwFrame;
		m_svpTemporalWidth = m_renderWidth;
		m_svpTemporalView = Device.mView;
		m_svpTemporalProjection = Device.mProject;
	}
	else
	{
        m_svpTemporalFrame = u32(-1);
        if (rt_secondVP_scene->dwWidth != Device.dwWidth || rt_secondVP_scene->dwHeight != Device.dwHeight)
        {
            rt_secondVP_scene.destroy();
            rt_secondVP_scene.create("$user$viewport2_scene", Device.dwWidth, Device.dwHeight, rt_secondVP->fmt);
        }
        draw_svp_scene(rt_secondVP_scene);
	}
	m_svpSceneFrame = Device.dwFrame;
	if (svp_motion_supported() && ensure_svp_motion_targets(rt_Position->dwWidth, rt_Position->dwHeight))
	{
		unbind_svp_resources();
		m_svpMotionDepthOwnerGeneration = GetRenderSurfaceOwnerGeneration();
		draw_svp_scene(rt_svpMotionDepth, 1);
		if (m_svpMotionDepthOwnerGeneration == GetRenderSurfaceOwnerGeneration())
			m_svpMotionDepthFrame = Device.dwFrame;
	}
	unbind_svp_resources();
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
	RImplementation.rmNormal();
}

bool CRenderTarget::svp_motion_supported() const
{
	return m_svpMotionOwnerSupport && ps_scope_lense_temporal_mode == 1 && !RImplementation.o.dx10_msaa &&
		RImplementation.o.ssfx_motionvectors && rt_Position && rt_Position->valid() &&
		rt_ssfx_motion_vectors && rt_ssfx_motion_vectors->valid() &&
		rt_Position->dwWidth == rt_ssfx_motion_vectors->dwWidth &&
		rt_Position->dwHeight == rt_ssfx_motion_vectors->dwHeight && svp_scene_capture_required();
}

bool CRenderTarget::ensure_svp_motion_targets(u32 width, u32 height)
{
	if (!width || !height)
		return false;
	if (rt_svpMotionDepth && rt_svpMotionDepth->valid() &&
		rt_svpMotionDepth->dwWidth == width && rt_svpMotionDepth->dwHeight == height)
		return rt_svpMotionOwner && rt_svpMotionOwner->valid() &&
			rt_svpMotionMap[0] && rt_svpMotionMap[0]->valid() &&
			rt_svpMotionMap[1] && rt_svpMotionMap[1]->valid();
	unbind_svp_resources();
	t_svpMotionPrevious->surface_set(nullptr);
	t_svpMotionCurrent->surface_set(nullptr);
	rt_svpMotionDepth.destroy();
	rt_svpMotionOwner.destroy();
	rt_svpMotionMap[0].destroy();
	rt_svpMotionMap[1].destroy();
	rt_svpMotionDepth.create("$user$svp_motion_depth", width, height, D3DFMT_R32F, 1);
	rt_svpMotionOwner.create("$user$svp_motion_owner", width, height, D3DFMT_R16F, 1);
	rt_svpMotionMap[0].create("$user$svp_motion_map0", width, height, D3DFMT_A32B32G32R32F, 1);
	rt_svpMotionMap[1].create("$user$svp_motion_map1", width, height, D3DFMT_A32B32G32R32F, 1);
	m_svpMotionHistory = false;
	m_svpMotionSeeded = false;
	m_svpMotionIndex = 0;
	m_svpMotionCaptureFrame = u32(-1);
	m_svpMotionOutputFrame = u32(-1);
	if (!rt_svpMotionDepth->valid() || !rt_svpMotionOwner->valid() ||
		!rt_svpMotionMap[0]->valid() || !rt_svpMotionMap[1]->valid())
		return false;
	const float clear[4] = {};
	HW.pContext->ClearRenderTargetView(rt_svpMotionMap[0]->pRT, clear);
	HW.pContext->ClearRenderTargetView(rt_svpMotionMap[1]->pRT, clear);
	return true;
}

extern Fvector2 GetPipMainViewJitterNdc();


void CRenderTarget::begin_svp_live_effects()
{
    const u32 view = Device.m_SecondViewport.IsSVPFrame() ? 1 : 0;
    m_svpReactiveBeforeFrame[view] = u32(-1);
    m_svpReactiveMaskFrame[view] = u32(-1);
    const bool vendorMask = view == 0 && m_upscalerActive;
    const bool pipMask = ps_scope_lense_live_effects && ps_scope_lense_update_interval > 1 &&
        Device.m_SecondViewport.IsSVPActive() && svp_motion_supported();
    const bool liveTaaMask = view == 1 && ps_ssfx_taa.x > 0 &&
        RImplementation.o.ssfx_motionvectors && !RImplementation.o.dx10_msaa;
    if (!vendorMask && !pipMask && !liveTaaMask)
        return;
    const u32 width = rt_Generic_0->dwWidth, height = rt_Generic_0->dwHeight;
    unbind_svp_resources();
    if (!t_svpReactiveBefore)
        t_svpReactiveBefore.create("$user$svp_reactive_before");
    t_svpReactiveBefore->surface_set(nullptr);
    if (!rt_svpReactiveBefore[view] || rt_svpReactiveBefore[view]->dwWidth != width ||
        rt_svpReactiveBefore[view]->dwHeight != height)
    {
        rt_svpReactiveBefore[view].destroy();
        rt_svpReactiveMask[view].destroy();
        rt_svpReactiveBefore[view].create(view ? "$user$svp_opaque_capture" : "$user$svp_opaque_main",
            width, height, rt_Generic_0->fmt, 1);
        rt_svpReactiveMask[view].create(view ? "$user$svp_reactive_capture" : "$user$svp_reactive_main",
            width, height, D3DFMT_A16B16G16R16F, 1);
    }
    if (rt_svpReactiveBefore[view]->valid() && rt_svpReactiveMask[view]->valid())
    {
        HW.pContext->CopyResource(rt_svpReactiveBefore[view]->pSurface, rt_Generic_0->pSurface);
        t_svpReactiveBefore->surface_set(rt_svpReactiveBefore[view]->pSurface);
        m_svpReactiveBeforeFrame[view] = Device.dwFrame;
    }
    u_setrt(rt_Generic_0, nullptr, nullptr, main_depth());
    RImplementation.rmNormal();
}

void CRenderTarget::end_svp_live_effects()
{
    const u32 view = Device.m_SecondViewport.IsSVPFrame() ? 1 : 0;
    if (m_svpReactiveBeforeFrame[view] != Device.dwFrame)
        return;
    unbind_svp_resources();
    draw_svp_scene(rt_svpReactiveMask[view], 3);
    m_svpReactiveMaskFrame[view] = Device.dwFrame;
    unbind_svp_resources();
    t_svpReactiveBefore->surface_set(nullptr);
    u_setrt(rt_Generic_0, rt_Heat, rt_ssfx_motion_vectors, main_depth());
    RCache.set_CullMode(CULL_CCW);
    RCache.set_Stencil(FALSE);
    RImplementation.rmNormal();
}

void CRenderTarget::trim_svp_idle_resources()
{
	if (Device.m_SecondViewport.IsSVPActive())
	{
		m_svpLastActiveTime = Device.dwTimeGlobal;
		return;
	}
	if (Device.dwTimeGlobal - m_svpLastActiveTime < 5000 ||
		Device.dwTimeGlobal - m_svpBudgetCheckTime < 5000 ||
		(m_svpRtBank.empty() && !rt_svpTemporal[0] && !rt_svpMotionMap[0])) return;
	m_svpBudgetCheckTime = Device.dwTimeGlobal;
	IDXGIAdapter3* adapter = nullptr;
	if (!HW.m_pAdapter || FAILED(HW.m_pAdapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&adapter))) return;
	DXGI_QUERY_VIDEO_MEMORY_INFO memory = {};
	const HRESULT status = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory);
	adapter->Release();
	if (FAILED(status) || !memory.Budget || double(memory.CurrentUsage) < double(memory.Budget) * .90) return;
	// Release inactive lens resources instead of lowering texture quality or
	// waiting for the OS to evict live world textures. Histories restart on ADS.
	unbind_svp_resources();
	if (t_svpTemporalPrevious) t_svpTemporalPrevious->surface_set(nullptr);
	if (t_svpMotionPrevious) t_svpMotionPrevious->surface_set(nullptr);
	if (t_svpMotionCurrent) t_svpMotionCurrent->surface_set(nullptr);
	if (m_svpMainReflection) m_svpMainReflection->surface_set(nullptr);
	if (m_svpMainEffectPosition) m_svpMainEffectPosition->surface_set(nullptr);
	rt_svpTemporal[0].destroy(); rt_svpTemporal[1].destroy();
	rt_svpMotionMap[0].destroy(); rt_svpMotionMap[1].destroy();
	rt_svpMotionDepth.destroy(); rt_svpMotionOwner.destroy();
	rt_svpReactiveBefore[1].destroy(); rt_svpReactiveMask[1].destroy();
	if (!m_upscalerActive) { rt_svpReactiveBefore[0].destroy(); rt_svpReactiveMask[0].destroy(); }
	m_svpRtBank.clear(); m_svpDepth.destroy();
	m_svpSssHistory.destroy(); m_svpSsrHistory.destroy();
	m_svpRtBankWidth = m_svpRtBankHeight = 0;
	m_svpTemporalFrame = m_svpMotionDepthFrame = u32(-1);
	m_svpMotionHistory = m_svpMotionSeeded = false;
	Msg("* [GPU memory] released idle PiP targets: usage=%.1f MiB budget=%.1f MiB",
		double(memory.CurrentUsage) / (1024.0*1024.0), double(memory.Budget) / (1024.0*1024.0));
	u_setrt(rt_Generic_0, nullptr, nullptr, main_depth());
	RImplementation.rmNormal();
}

void CRenderTarget::phase_svp_motion()
{
	trim_svp_idle_resources();
	if (m_svpMotionOutputFrame == Device.dwFrame)
		return;
	m_svpMotionOutputFrame = u32(-1);
	const auto& viewport = Device.m_SecondViewport;
	const u64 ownerGeneration = GetRenderSurfaceOwnerGeneration();
	if (Device.m_SecondViewport.IsSVPFrame() || !Device.m_SecondViewport.IsSVPActive() ||
		!svp_motion_supported() || Device.dwPrecacheFrame || Device.Paused() ||
		!viewport.IsSVPTextureReady() || !rt_svpMotionDepth || !rt_svpMotionDepth->valid() ||
		m_svpMotionDepthFrame != viewport.GetSVPCaptureFrame() ||
		m_svpMotionDepthOwnerGeneration != ownerGeneration ||
		!g_pGamePersistent || !g_pGamePersistent->m_pGShaderConstants)
	{
		m_svpMotionHistory = false;
		m_svpMotionSeeded = false;
		return;
	}
	if (m_svpMotionSerial == Device.mMainRenderSerial && m_svpMotionHistory)
		return;

	const u32 captureFrame = viewport.GetSVPCaptureFrame();
	SvpMotionEpochInput epochInput;
	epochInput.currentMainSerial = Device.mMainRenderSerial;
	epochInput.previousMainSerial = m_svpMotionSerial;
	epochInput.currentTime = Device.dwTimeGlobal;
	epochInput.previousTime = m_svpMotionTime;
	epochInput.captureTime = viewport.GetSVPCaptureTime();
	epochInput.hasHistory = m_svpMotionHistory;
	epochInput.wasSeeded = m_svpMotionSeeded;
	epochInput.newCapture = captureFrame != m_svpMotionCaptureFrame;
	epochInput.ownerGenerationMatches = m_svpMotionOwnerGeneration == ownerGeneration;
	const SvpMotionEpochDecision epoch = SelectSvpMotionEpoch(epochInput);
	const bool previousValid = epoch.previousValid;

	const Fmatrix& currentView = Device.mView_saved;
	const Fmatrix& currentProjection = Device.mProject_saved;
	const Fmatrix& previousView = previousValid ? m_svpMotionView : currentView;
	const Fmatrix& previousProjection = previousValid ? m_svpMotionProjection : currentProjection;
	const Fmatrix& captureProjection = viewport.GetSVPCapturedProjection();
	const float scopeFov = clampr(g_pGamePersistent->m_pGShaderConstants->hud_params.y, 1.f, 170.f);
	const float tangent = tanf(deg2rad(scopeFov) * 0.5f);
	Fvector4 crop;
	crop.set(currentProjection._11 * tangent / _max(Device.fASPECT, 0.01f),
		currentProjection._22 * tangent, 0.5f + currentProjection._31 * 0.5f,
		0.5f - currentProjection._32 * 0.5f);
	const Fvector4& previousCrop = previousValid ? m_svpMotionCrop : crop;
	const Fvector2 jitterNdc = GetPipMainViewJitterNdc();
	Fvector2 jitter;
	jitter.set(jitterNdc.x * 0.5f, -jitterNdc.y * 0.5f);
	const Fvector2& previousJitter = previousValid ? m_svpMotionJitter : jitter;
	Fmatrix currentInverse, previousInverse, currentToCapture, previousToCapture;
	currentInverse.invert(currentView);
	previousInverse.invert(previousView);
	currentToCapture.mul(viewport.GetSVPCapturedView(), currentInverse);
	previousToCapture.mul(viewport.GetSVPCapturedView(), previousInverse);

	unbind_svp_resources();
	t_svpMotionCurrent->surface_set(nullptr);
	t_svpMotionPrevious->surface_set(rt_svpMotionMap[m_svpMotionIndex]->pSurface);
	RCache.set_Element(s_svp_quality->E[2]);
	RCache.set_c("pip_current_main", crop);
	RCache.set_c("pip_previous_main", previousCrop);
	RCache.set_c("pip_main_jitter", jitter.x, jitter.y, previousJitter.x, previousJitter.y);
	RCache.set_c("pip_current_ray", 1.f / currentProjection._11, 1.f / currentProjection._22,
		-currentProjection._31 / currentProjection._11, -currentProjection._32 / currentProjection._22);
	RCache.set_c("pip_previous_ray", 1.f / previousProjection._11, 1.f / previousProjection._22,
		-previousProjection._31 / previousProjection._11, -previousProjection._32 / previousProjection._22);
	// Scope raster jitter is zero; its actual unjittered projection is published with RGB.
	RCache.set_c("pip_capture_projection", captureProjection._11 * 0.5f, -captureProjection._22 * 0.5f,
		0.5f + captureProjection._31 * 0.5f, 0.5f - captureProjection._32 * 0.5f);
	RCache.set_c("pip_current_to_capture", currentToCapture);
	RCache.set_c("pip_previous_to_capture", previousToCapture);
	RCache.set_c("pip_motion_control", float(epoch.mode), previousValid ? 1.f : 0.f,
		epoch.alpha, float(epoch.elapsed) * 0.001f);
	RCache.set_c("pip_motion_limits", 0.02f, 0.005f, 64.f, 4.f);
	const u32 next = m_svpMotionIndex ^ 1;
	draw_svp_scene(rt_svpMotionMap[next], -1);
	unbind_svp_resources();
	t_svpMotionPrevious->surface_set(nullptr);
	if (GetRenderSurfaceOwnerGeneration() != ownerGeneration)
	{
		m_svpMotionHistory = false;
		m_svpMotionSeeded = false;
		u_setrt(rt_Generic_0, nullptr, nullptr, main_depth());
		RImplementation.rmNormal();
		return;
	}
	t_svpMotionCurrent->surface_set(rt_svpMotionMap[next]->pSurface);
	m_svpMotionIndex = next;
	m_svpMotionHistory = true;
	m_svpMotionSeeded = epoch.mode != 0;
	m_svpMotionCaptureFrame = captureFrame;
	m_svpMotionSerial = Device.mMainRenderSerial;
	m_svpMotionOwnerGeneration = ownerGeneration;
	m_svpMotionTime = Device.dwTimeGlobal;
	m_svpMotionView = currentView;
	m_svpMotionProjection = currentProjection;
	m_svpMotionCrop = crop;
	m_svpMotionJitter = jitter;
	if (mt_FrameProfile)
	{
		static u32 samples = 0, seeds = 0, propagated = 0, unavailable = 0;
		++samples;
		seeds += epoch.mode == 1 ? 1 : 0;
		propagated += epoch.mode == 2 ? 1 : 0;
		unavailable += epoch.mode == 0 ? 1 : 0;
		if (samples >= 240)
		{
			Msg("* [pip-motion/profile] main=%u seed=%u propagate=%u unavailable=%u interval=%d",
				samples, seeds, propagated, unavailable, ps_scope_lense_update_interval);
			samples = seeds = propagated = unavailable = 0;
		}
	}
	// During startup, keep the detailed v143 image until a capture can be seeded.
	if (m_svpMotionSeeded)
		m_svpMotionOutputFrame = Device.dwFrame;
	u_setrt(rt_Generic_0, nullptr, nullptr, main_depth());
	RImplementation.rmNormal();
}

void CRender::RenderToTarget(RRT target)
{
	ref_rt* RT = nullptr;

	switch (target)
	{
	case rtPDA:
		RT = &Target->rt_ui_pda;
		break;
	case rtSVP:
		RT = &Target->rt_secondVP;
		break;
	default:
		Debug.fatal(DEBUG_INFO, "None or wrong Target specified: %i", target);
		break;
	}

	ID3DTexture2D* pBuffer = nullptr;
	ID3DTexture2D* pSource = nullptr;
	const bool sceneCapture = target == rtSVP && Target->svp_scene_capture_required();
	if (sceneCapture)
	{
		if (Device.m_SecondViewport.isCamReady && Target->m_svpSceneFrame == Device.dwFrame &&
			Target->rt_secondVP_scene && Target->rt_secondVP_scene->valid())
			pSource = Target->rt_secondVP_scene->pSurface;
	}
	else if (SUCCEEDED(HW.m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBuffer)))
		pSource = pBuffer;

	if (pSource)
	{
		if (target == rtSVP)
			Target->unbind_svp_resources();
        if (target == rtSVP)
        {
            D3D11_TEXTURE2D_DESC sourceDesc = {};
            pSource->GetDesc(&sourceDesc);
            if ((*RT)->dwWidth != sourceDesc.Width || (*RT)->dwHeight != sourceDesc.Height)
            {
                const D3DFORMAT format = (*RT)->fmt;
                RT->destroy();
                RT->create("$user$viewport2", sourceDesc.Width, sourceDesc.Height, format, 1);
            }
        }
		HW.pContext->CopyResource((*RT)->pSurface, pSource);
		if (target == rtSVP)
		{
			Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT, nullptr, nullptr, HW.pBaseZB);
			RImplementation.rmNormal();
		}
	}
	else if (target == rtSVP)
	{
		// The caller's publication guard also checks isCamReady. Do not publish
		// an older pending image after a failed capture.
		Device.m_SecondViewport.InvalidateSVPContent();
	}
	_RELEASE(pBuffer);

	if (target == rtSVP && RImplementation.o.ssfx_water)
	{
		HW.pContext->CopyResource(Target->rt_ssfx_water->pSurface, Target->rt_ssfx_water_main->pSurface);
		HW.pContext->CopyResource(Target->rt_ssfx_temp->pSurface, Target->rt_ssfx_water_blur_main->pSurface);
	}
}
