#include "stdafx.h"
#include "r4_rendertarget.h"
#include "UpscalerRuntime.h"

void CRenderTarget::phase_upscale(bool temporal)
{
	const bool temporalOutputValid = rt_UpscaleOutput->pSurface &&
		rt_UpscaleOutput->pUAView && rt_UpscaleOutput->dwWidth == Device.dwWidth &&
		rt_UpscaleOutput->dwHeight == Device.dwHeight;
	const bool vendorDispatch = temporal && temporalOutputValid;
	if (vendorDispatch)
		g_AnthologyUpscaler.BeginGpuProfile();
	// Export display-referred color and device depth together. Integer pixel
	// loads keep both vendor inputs aligned with the motion-vector texels.
	if (vendorDispatch)
		u_setrt(rt_UpscaleInput, rt_UpscaleDepth, rt_UpscaleReactive, nullptr);
	else
		u_setrt(rt_UpscaleInput, nullptr, nullptr, nullptr);
	RImplementation.rmNormal();
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	const float prepareWidth = float(m_renderWidth);
	const float prepareHeight = float(m_renderHeight);
	const u32 prepareColor = color_rgba(255, 255, 255, 255);
	u32 prepareOffset = 0;
	FVF::TL* prepareVertices = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, prepareOffset);
	prepareVertices->set(0.f, prepareHeight, EPS_S, 1.f, prepareColor, 0.f, 1.f); ++prepareVertices;
	prepareVertices->set(0.f, 0.f, EPS_S, 1.f, prepareColor, 0.f, 0.f); ++prepareVertices;
	prepareVertices->set(prepareWidth, prepareHeight, EPS_S, 1.f, prepareColor, 1.f, 1.f); ++prepareVertices;
	prepareVertices->set(prepareWidth, 0.f, EPS_S, 1.f, prepareColor, 1.f, 0.f);
	RCache.Vertex.Unlock(4, g_combine->vb_stride);
	RCache.set_Element(s_upscale->E[vendorDispatch ? 3 : 1]);
	RCache.set_c("anthology_upscale_linear", vendorDispatch && g_AnthologyUpscaler.Mode() == AnthologyUpscalerFSR3 ? 1.f : 0.f,
		m_svpReactiveMaskFrame[0] == Device.dwFrame ? 1.f : 0.f, 0.f, 0.f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, prepareOffset, 0, 4, 0, 2);

    bool resolved = false;
	if (temporal && !temporalOutputValid)
	{
		m_upscalerResetHistory = true;
		static bool reportedInvalidOutput = false;
		if (!reportedInvalidOutput)
		{
			reportedInvalidOutput = true;
			Msg("! [UPSCALER/RT] vendor dispatch skipped: output UAV/dimensions are invalid; spatial fallback selected");
		}
	}
    if (vendorDispatch)
    {
        RCache.set_RT(nullptr, 0);
        RCache.set_RT(nullptr, 1);
        RCache.set_RT(nullptr, 2);
        RCache.set_RT(nullptr, 3);
        RCache.set_ZB(nullptr);
        RCache.set_Textures(nullptr);
        // set_Textures updates the engine-side SRV cache. External NGX/FSR
        // dispatches bypass RCache, so commit the null bindings immediately.
        SRVSManager.Apply();
        const bool resetHistory = m_upscalerResetHistory || Device.dwPrecacheFrame > 0;
		g_AnthologyUpscaler.MarkGpuDispatch();
        resolved = g_AnthologyUpscaler.Dispatch(
            rt_UpscaleInput->pSurface,
            rt_ssfx_motion_vectors->pSurface,
			rt_UpscaleDepth->pSurface,
            rt_UpscaleOutput->pSurface,
            rt_UpscaleReactive->pSurface,
            resetHistory);
		g_AnthologyUpscaler.EndGpuProfile();
		// Both integrations submit compute work outside RCache. Release every CS
		// resource/UAV slot before sampling the output as a pixel-shader SRV; this
		// also prevents stale vendor bindings from leaking into the next frame.
		ID3D11ShaderResourceView* nullSrvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
		ID3D11UnorderedAccessView* nullUavs[D3D11_PS_CS_UAV_REGISTER_COUNT] = {};
		HW.pContext->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSrvs);
		HW.pContext->CSSetUnorderedAccessViews(0, D3D11_PS_CS_UAV_REGISTER_COUNT, nullUavs, nullptr);
		HW.pContext->CSSetShader(nullptr, nullptr, 0);
		// NGX and FidelityFX issue commands directly on the immediate context and
		// may replace viewport, shaders, input layout, buffers and pipeline state.
		// Force XRay to bind its complete fullscreen-present state again instead
		// of trusting stale backend caches left from before the vendor dispatch.
		RCache.InvalidateExternalState();
		m_upscalerResetHistory = !resolved || Device.dwPrecacheFrame > 0;
    }

	m_upscaleLinearOutput = vendorDispatch && g_AnthologyUpscaler.Mode() == AnthologyUpscalerFSR3;
	// Temporal failure and PiP/SVP frames use a spatial reconstruction into the
	// same display-sized FP16 output.  The final postprocess pass consumes this
	// texture afterwards, so it never becomes part of temporal history.
	if (!resolved)
	{
		u_setrt(rt_UpscaleOutput, nullptr, nullptr, nullptr);
		D3D_VIEWPORT viewport = {0.f, 0.f, float(Device.dwWidth), float(Device.dwHeight), 0.f, 1.f};
		HW.pContext->RSSetViewports(1, &viewport);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);

		const float width = float(Device.dwWidth);
		const float height = float(Device.dwHeight);
		const u32 color = color_rgba(255, 255, 255, 255);
		u32 offset = 0;
		FVF::TL* vertices = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
		vertices->set(0.f, height, EPS_S, 1.f, color, 0.f, 1.f); ++vertices;
		vertices->set(0.f, 0.f, EPS_S, 1.f, color, 0.f, 0.f); ++vertices;
		vertices->set(width, height, EPS_S, 1.f, color, 1.f, 1.f); ++vertices;
		vertices->set(width, 0.f, EPS_S, 1.f, color, 1.f, 0.f);
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		RCache.set_Element(s_upscale->E[0]);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
	}
}


void CRenderTarget::draw_upscale_aux(int element)
{
    RImplementation.rmNormal();
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);
    const float w = float(Device.dwWidth), h = float(Device.dwHeight);
    u32 offset = 0;
    const u32 color = color_rgba(255, 255, 255, 255);
    FVF::TL* vertices = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
    vertices->set(0.f, h, EPS_S, 1.f, color, 0.f, 1.f); ++vertices;
    vertices->set(0.f, 0.f, EPS_S, 1.f, color, 0.f, 0.f); ++vertices;
    vertices->set(w, h, EPS_S, 1.f, color, 1.f, 1.f); ++vertices;
    vertices->set(w, 0.f, EPS_S, 1.f, color, 1.f, 0.f);
    RCache.Vertex.Unlock(4, g_combine->vb_stride);
    RCache.set_Element(s_upscale_aux->E[element]);
    RCache.set_c("anthology_upscale_linear", m_upscaleLinearOutput ? 1.f : 0.f,
        !Device.m_SecondViewport.IsSVPFrame() && RImplementation.o.ssfx_motionblur ? ps_ssfx_motionblur.y : 0.f, 0.f, 0.f);
    RCache.set_c("anthology_core_res", float(m_renderWidth), float(m_renderHeight),
        1.f / m_renderWidth, 1.f / m_renderHeight);
    RCache.set_c("anthology_jitter_uv", g_main_taa_jitter_pixels.x / m_renderWidth,
        g_main_taa_jitter_pixels.y / m_renderHeight, 0.f, 0.f);
    RCache.set_c("m_current", Matrix_current);
    RCache.set_c("m_previous", Matrix_previous);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
}

void CRenderTarget::phase_upscale_finish()
{
    const bool motionBlur = !Device.m_SecondViewport.IsSVPFrame() &&
        RImplementation.o.ssfx_motionblur && ps_ssfx_motionblur.y > 0.f;
    if (!m_upscaleLinearOutput && !motionBlur) return;
    u_setrt(rt_UpscalePost, nullptr, nullptr, nullptr);
    draw_upscale_aux(1);
    unbind_svp_resources();
    HW.pContext->CopyResource(rt_UpscaleOutput->pSurface, rt_UpscalePost->pSurface);
    m_upscaleLinearOutput = false;
}

void CRenderTarget::phase_upscale_reticle()
{
    // Keep low-resolution scene/position aliases for glass and fallback rays.
    HW.pContext->CopyResource(rt_Generic_2->pSurface, rt_Position->pSurface);
    // Newly exposed lens pixels use the reconstructed full-resolution main view.
    HW.pContext->CopyResource(rt_UpscalePost->pSurface, rt_UpscaleOutput->pSurface);
    rt_Generic_temp->pTexture->surface_set(rt_UpscalePost->pSurface);
    unbind_svp_resources();
    u_setrt(Device.dwWidth, Device.dwHeight, nullptr, nullptr, nullptr, rt_UpscaleHudDepth->pZRT);
    HW.pContext->ClearDepthStencilView(rt_UpscaleHudDepth->pZRT, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
    draw_upscale_aux(0);
    unbind_svp_resources();
    u_setrt(rt_UpscaleOutput, nullptr, nullptr, rt_UpscaleHudDepth->pZRT);
    RCache.set_CullMode(CULL_CCW);
    RCache.set_Stencil(FALSE);
    RCache.set_ColorWriteEnable();
    // The lens is already reconstructed by its own geometry/camera history.
    // Render it once at display resolution, outside the vendor history/jitter.
    g_upscale_reticle_pass = true;
    RImplementation.render_Reticle();
    g_upscale_reticle_pass = false;
    unbind_svp_resources();
    rt_Generic_temp->pTexture->surface_set(rt_Generic_temp->pSurface);
}
