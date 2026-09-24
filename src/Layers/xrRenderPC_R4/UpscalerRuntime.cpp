#include "stdafx.h"
#include "UpscalerRuntime.h"
#include "../xrRender/xrRender_console.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/EngineThreading.h"
#include <dxgi1_4.h>

CAnthologyUpscalerRuntime g_AnthologyUpscaler;

namespace
{
u32 ResolveEffectiveQuality(u32 mode)
{
	const u32 configured = clampr(ps_r4_upscaler_quality, 0u, 5u);
	if (mode != AnthologyUpscalerDLSS || configured != 5)
		return configured;

	// Direct NGX has no arbitrary custom-ratio quality mode. Snap Custom to the
	// nearest supported preset and use that preset's render size, so a low-res
	// input can never be paired with DLAA.
	static const float presetScales[] = {1.0f, 2.0f / 3.0f, 0.5882353f, 0.5f, 1.0f / 3.0f};
	const float customScale = clampr(ps_r4_upscaler_custom_scale, 0.33f, 1.0f);
	u32 nearest = 0;
	float nearestDistance = fabsf(customScale - presetScales[0]);
	for (u32 i = 1; i < _countof(presetScales); ++i)
	{
		const float distance = fabsf(customScale - presetScales[i]);
		if (distance < nearestDistance)
		{
			nearest = i;
			nearestDistance = distance;
		}
	}
	return nearest;
}

LPCSTR QualityName(u32 quality)
{
	switch (quality)
	{
	case 0: return "native";
	case 1: return "quality";
	case 2: return "balanced";
	case 3: return "performance";
	case 4: return "ultra_performance";
	case 5: return "custom";
	default: return "unknown";
	}
}
}

float CAnthologyUpscalerRuntime::ResolveRenderScale() const
{
	const u32 quality = ResolveEffectiveQuality(m_mode);
    switch (quality)
    {
    case 0: return 1.f;
    case 1: return 2.f / 3.f;
    case 2: return 0.5882353f;
    case 3: return 0.5f;
    case 4: return 1.f / 3.f;
    default: return clampr(ps_r4_upscaler_custom_scale, 0.33f, 1.f);
    }
}

bool CAnthologyUpscalerRuntime::ProbeAndResolveMode()
{
    m_mode = clampr(ps_r4_upscaler, u32(AnthologyUpscalerOff), u32(AnthologyUpscalerDLSS));
    if (m_mode == AnthologyUpscalerDLSS && !m_dlss.Probe(HW.pDevice))
    {
        m_mode = AnthologyUpscalerOff;
        ps_r4_upscaler = AnthologyUpscalerOff;
    }
	if (m_mode == AnthologyUpscalerFSR3 && HW.FeatureLevel < D3D_FEATURE_LEVEL_11_0)
    {
        Msg("! [UPSCALER/FSR3] feature level 11.0 is required; native fallback selected");
        m_mode = AnthologyUpscalerOff;
		ps_r4_upscaler = AnthologyUpscalerOff;
	}
	if (m_mode == AnthologyUpscalerFSR3)
	{
		static HMODULE backendModule = nullptr;
		static HMODULE upscalerModule = nullptr;
		if (!backendModule)
			backendModule = LoadLibraryA("ffx_backend_dx11_x64.dll");
		if (!upscalerModule)
			upscalerModule = LoadLibraryA("ffx_fsr3upscaler_x64.dll");
		if (!backendModule || !upscalerModule)
		{
			Msg("! [UPSCALER/FSR3] runtime DLLs are missing; native fallback selected");
			m_mode = AnthologyUpscalerOff;
			ps_r4_upscaler = AnthologyUpscalerOff;
		}
	}
    return IsEnabled();
}

bool CAnthologyUpscalerRuntime::Initialize(u32 renderWidth, u32 renderHeight, u32 displayWidth, u32 displayHeight)
{
	ResetTemporalState();
	ReleaseGpuProfile();
	// A failed/recreated backend must never leave its automatic sampler bias
	// behind. The user-owned r__tf_mipbias value itself is never modified.
	SetTemporalUpscalerMipBias(0.0f, false);
    m_renderWidth = renderWidth;
    m_renderHeight = renderHeight;
    m_displayWidth = displayWidth;
    m_displayHeight = displayHeight;
	m_dispatchLogged = false;
    if (!IsEnabled())
        return false;

    bool created = false;
    if (m_mode == AnthologyUpscalerFSR3)
    {
        CFSR3Wrapper::ContextParameters params;
        params.maxRenderSize = {renderWidth, renderHeight};
        params.displaySize = {displayWidth, displayHeight};
        params.device = HW.pDevice;
        created = m_fsr3.Create(params);
    }
    else if (m_mode == AnthologyUpscalerDLSS)
    {
        CDLSSWrapper::ContextParameters params;
        params.renderWidth = renderWidth;
        params.renderHeight = renderHeight;
        params.displayWidth = displayWidth;
        params.displayHeight = displayHeight;
        params.device = HW.pDevice;
        params.context = HW.pContext;
		created = m_dlss.Create(params, ResolveEffectiveQuality(m_mode));
    }

    if (!created)
    {
        Msg("! [UPSCALER] requested backend could not be created; renderer stays native");
        m_mode = AnthologyUpscalerOff;
        ps_r4_upscaler = AnthologyUpscalerOff;
        return false;
    }

	const float actualScale = displayWidth ? float(renderWidth) / float(displayWidth) : 1.0f;
	const bool scaled = actualScale < 0.999f;
	const float automaticMipBias = scaled ? clampr(log2f(actualScale) - 1.0f, -3.0f, 0.0f) : 0.0f;
	SetTemporalUpscalerMipBias(automaticMipBias, scaled);
	const float effectiveMipBias = GetEffectiveTextureMipBias();
	const u32 configuredQuality = clampr(ps_r4_upscaler_quality, 0u, 5u);
	const u32 effectiveQuality = ResolveEffectiveQuality(m_mode);
	string64 qualityLabel = {};
	if (configuredQuality == effectiveQuality)
		xr_sprintf(qualityLabel, "%s(%u)", QualityName(effectiveQuality), effectiveQuality);
	else
	{
		xr_sprintf(qualityLabel, "%s(%u)->%s(%u)", QualityName(configuredQuality), configuredQuality,
			QualityName(effectiveQuality), effectiveQuality);
	}

	Msg("* [UPSCALER] backend=%s quality=%s render=%ux%u display=%ux%u scale=%.3f "
		"mip_bias(user=%.3f auto=%.3f effective=%.3f) frame_generation=off",
		m_mode == AnthologyUpscalerFSR3 ? "FSR3.1.2" : "DLSS", qualityLabel, renderWidth, renderHeight,
		displayWidth, displayHeight, actualScale, ps_r__tf_Mipbias, automaticMipBias, effectiveMipBias);
    return true;
}

void CAnthologyUpscalerRuntime::UpdateJitter(u32 frameIndex)
{
	if (!IsEnabled() || !m_renderWidth || !m_displayWidth || Device.m_SecondViewport.IsSVPFrame())
	{
		if (!Device.m_SecondViewport.IsSVPFrame())
			g_main_taa_jitter_pixels.set(0.f, 0.f);
		return;
	}
	if (m_jitterFrame == frameIndex)
		return;
	m_jitterFrame = frameIndex;

	const int phaseCount = _max(1, ffxFsr3UpscalerGetJitterPhaseCount(
		int(m_renderWidth), int(m_displayWidth)));
	float jitterX = 0.f;
	float jitterY = 0.f;
	// Device frames include lens captures. Indexing Halton with dwFrame skips
	// half of even-length sequences while ADS alternates main and SVP renders.
	const int jitterPhase = int(m_jitterPhase % u32(phaseCount));
	m_jitterPhase = u32((jitterPhase + 1) % phaseCount);
	if (ffxFsr3UpscalerGetJitterOffset(&jitterX, &jitterY, jitterPhase, phaseCount) != FFX_OK)
	{
		jitterX = 0.f;
		jitterY = 0.f;
	}
	g_main_taa_jitter_pixels.set(jitterX, jitterY);
}

bool CAnthologyUpscalerRuntime::Dispatch(ID3D11Resource* color, ID3D11Resource* motion,
	ID3D11Resource* depth, ID3D11Resource* output, bool resetHistory)
{
	// Motion history refers to the last main render. A failed resolve, loading,
	// menu gap or a camera cut cannot reuse a vendor history from an older view.
	const u32 frameGap = Device.dwFrame - m_lastDispatchFrame;
	const bool cameraCut = m_historyValid &&
		(Device.vCameraPosition.distance_to_sqr(m_previousCameraPosition) > 25.f ||
		 Device.vCameraDirection.dotproduct(m_previousCameraDirection) < 0.5f ||
		 fabsf(Device.fFOV - m_previousFov) > 10.f);
	resetHistory = resetHistory || !m_historyValid || frameGap > 2 || cameraCut;
	const float frameTimeMs = !resetHistory ?
		clampr(float(Device.dwTimeContinual - m_lastDispatchTime), 1.f, 250.f) :
		clampr(Device.fTimeDelta * 1000.f, 1.f, 250.f);

	auto finishDispatch = [this, color, motion, depth, output](bool success)
	{
		m_historyValid = success && Device.dwPrecacheFrame == 0;
		if (success)
		{
			m_lastDispatchFrame = Device.dwFrame;
			m_lastDispatchTime = Device.dwTimeContinual;
			m_previousCameraPosition.set(Device.vCameraPosition);
			m_previousCameraDirection.set(Device.vCameraDirection);
			m_previousFov = Device.fFOV;
		}
		if (success && !m_dispatchLogged)
		{
			auto textureFormat = [](ID3D11Resource* resource)
			{
				ID3D11Texture2D* texture = nullptr;
				if (!resource || FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))))
					return DXGI_FORMAT_UNKNOWN;
				D3D11_TEXTURE2D_DESC desc = {};
				texture->GetDesc(&desc);
				texture->Release();
				return desc.Format;
			};
			Msg("* [UPSCALER] first vendor dispatch succeeded: color_fmt=%u motion_fmt=%u depth_fmt=%u output_fmt=%u",
				u32(textureFormat(color)), u32(textureFormat(motion)), u32(textureFormat(depth)),
				u32(textureFormat(output)));
			m_dispatchLogged = true;
		}
		return success;
	};

    if (m_mode == AnthologyUpscalerFSR3)
    {
        CFSR3Wrapper::DrawParameters params;
        params.deviceContext = HW.pContext;
        params.unresolvedColor = color;
        params.motionVectors = motion;
        params.depth = depth;
        params.output = output;
        params.renderWidth = m_renderWidth;
        params.renderHeight = m_renderHeight;
        params.displayWidth = m_displayWidth;
        params.displayHeight = m_displayHeight;
        params.reset = resetHistory;
		// Run RCAS inside the vendor dispatch. The old mandatory display 9-tap pass
		// erased much of the GPU saving and reprocessed an already reconstructed image.
		params.sharpening = ps_r4_upscaler_sharpness > EPS;
		params.sharpness = clampr(ps_r4_upscaler_sharpness, 0.f, 1.f);
		params.frameTimeMs = frameTimeMs;
		// The active camera may override its near plane. Reconstruct the same
		// finite, forward-Z projection that authored the exported hardware depth.
		const float projectionNear = -Device.mProject._43 / Device.mProject._33;
		params.nearPlane = projectionNear > EPS_S ? projectionNear : VIEWPORT_NEAR;
        params.farPlane = g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv ?
            g_pGamePersistent->Environment().CurrentEnv->far_plane : 500.f;
        params.verticalFov = deg2rad(Device.fFOV);
        params.jitterX = g_main_taa_jitter_pixels.x;
        params.jitterY = g_main_taa_jitter_pixels.y;
		return finishDispatch(m_fsr3.Draw(params));
    }
    if (m_mode == AnthologyUpscalerDLSS)
    {
        CDLSSWrapper::DrawParameters params;
        params.unresolvedColor = color;
        params.motionVectors = motion;
        params.depth = depth;
        params.output = output;
        params.renderWidth = m_renderWidth;
        params.renderHeight = m_renderHeight;
        params.reset = resetHistory;
		params.frameTimeMs = frameTimeMs;
		params.jitterX = g_main_taa_jitter_pixels.x;
		params.jitterY = g_main_taa_jitter_pixels.y;
		return finishDispatch(m_dlss.Draw(params));
    }
    return false;
}

void CAnthologyUpscalerRuntime::Shutdown()
{
	ReleaseGpuProfile();
	ResetTemporalState();
	SetTemporalUpscalerMipBias(0.0f, false);
    m_fsr3.Destroy();
    m_dlss.Shutdown();
    m_mode = AnthologyUpscalerOff;
    m_renderWidth = m_renderHeight = m_displayWidth = m_displayHeight = 0;
	m_dispatchLogged = false;
	g_main_taa_jitter_pixels.set(0.f, 0.f);
}

void CAnthologyUpscalerRuntime::ResetTemporalState()
{
	m_jitterFrame = u32(-1);
	m_jitterPhase = 0;
	m_lastDispatchFrame = m_lastDispatchTime = 0;
	m_historyValid = false;
}

void CAnthologyUpscalerRuntime::ReleaseGpuProfile()
{
	for (GpuProfileSample& sample : m_gpuSamples)
	{
		_RELEASE(sample.disjoint);
		for (ID3D11Query*& timestamp : sample.timestamps)
			_RELEASE(timestamp);
		sample.pending = false;
		sample.temporal = false;
	}
	m_gpuActiveSample = -1;
	m_gpuProfileUnavailable = false;
	m_gpuProfileCount = 0;
	m_gpuPrepareMs = m_gpuVendorMs = m_gpuSceneMs = m_gpuMaxMs = 0.0;
}

void CAnthologyUpscalerRuntime::BeginSceneGpuProfile()
{
	m_gpuActiveSample = -1;
	if (!mt_FrameProfile || !mt_FrameProfileDetailed || Device.dwPrecacheFrame ||
		Device.m_SecondViewport.IsSVPFrame() || m_gpuProfileUnavailable)
		return;

	for (GpuProfileSample& sample : m_gpuSamples)
	{
		if (!sample.pending)
			continue;
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
		HRESULT ready = HW.pContext->GetData(sample.disjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (ready == S_FALSE)
			continue;
		if (FAILED(ready) || disjoint.Disjoint || !disjoint.Frequency)
		{
			sample.pending = false;
			continue;
		}
		UINT64 times[5] = {};
		bool complete = true;
		for (u32 i = 0; i < _countof(times); ++i)
		{
			if (!sample.temporal && i > 0 && i < 4)
				continue;
			ready = HW.pContext->GetData(sample.timestamps[i], &times[i], sizeof(times[i]), D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (ready != S_OK)
			{
				complete = false;
				if (FAILED(ready))
					sample.pending = false;
				break;
			}
		}
		if (!complete)
			continue;
		sample.pending = false;
		if (times[4] < times[0] || (sample.temporal &&
			(times[4] < times[3] || times[3] < times[2] || times[2] < times[1] || times[1] < times[0])))
			continue;
		const double toMs = 1000.0 / double(disjoint.Frequency);
		if (sample.temporal)
		{
			m_gpuPrepareMs += double(times[2] - times[1]) * toMs;
			m_gpuVendorMs += double(times[3] - times[2]) * toMs;
		}
		m_gpuSceneMs += double(times[4] - times[0]) * toMs;
		m_gpuMaxMs = std::max(m_gpuMaxMs, double(times[4] - times[0]) * toMs);
		if (++m_gpuProfileCount >= 120)
		{
			IDXGIAdapter3* adapter = nullptr;
			if (HW.m_pAdapter && SUCCEEDED(HW.m_pAdapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter))))
			{
				DXGI_QUERY_VIDEO_MEMORY_INFO local = {}, shared = {};
				if (SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) &&
					SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared)))
				{
					Msg("* [render-memory/profile] local=%.1f budget=%.1f shared=%.1f MiB",
						double(local.CurrentUsage) / 1048576., double(local.Budget) / 1048576., double(shared.CurrentUsage) / 1048576.);
				}
				adapter->Release();
			}
			Msg("* [render-gpu/profile] mode=%s samples=%u render=%ux%u display=%ux%u main=%.3f max=%.3f prepare=%.3f vendor=%.3f upscale=%.3f ms; asynchronous GPU timestamps",
				!IsEnabled() ? "native" : m_mode == AnthologyUpscalerFSR3 ? "FSR3" : "DLSS",
				m_gpuProfileCount, IsEnabled() ? m_renderWidth : Device.dwWidth,
				IsEnabled() ? m_renderHeight : Device.dwHeight, Device.dwWidth, Device.dwHeight,
				m_gpuSceneMs / m_gpuProfileCount, m_gpuMaxMs,
				m_gpuPrepareMs / m_gpuProfileCount, m_gpuVendorMs / m_gpuProfileCount,
				(m_gpuPrepareMs + m_gpuVendorMs) / m_gpuProfileCount);
			m_gpuProfileCount = 0;
			m_gpuPrepareMs = m_gpuVendorMs = m_gpuSceneMs = m_gpuMaxMs = 0.0;
		}
	}

	for (u32 i = 0; i < _countof(m_gpuSamples); ++i)
	{
		GpuProfileSample& sample = m_gpuSamples[i];
		if (sample.pending)
			continue;
		D3D11_QUERY_DESC description = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
		HRESULT result = S_OK;
		if (!sample.disjoint)
			result = HW.pDevice->CreateQuery(&description, &sample.disjoint);
		description.Query = D3D11_QUERY_TIMESTAMP;
		for (ID3D11Query*& timestamp : sample.timestamps)
		{
			if (SUCCEEDED(result) && !timestamp)
				result = HW.pDevice->CreateQuery(&description, &timestamp);
		}
		if (FAILED(result))
		{
			m_gpuProfileUnavailable = true;
			Msg("! [upscaler/gpu] timestamp queries unavailable: 0x%08x", result);
			return;
		}
		m_gpuActiveSample = int(i);
		sample.temporal = false;
		HW.pContext->Begin(sample.disjoint);
		HW.pContext->End(sample.timestamps[0]);
		return;
	}
	// Every slot is pending: skip this measurement without waiting on the GPU.
}

void CAnthologyUpscalerRuntime::BeginGpuProfile()
{
	if (m_gpuActiveSample < 0)
		return;
	GpuProfileSample& sample = m_gpuSamples[m_gpuActiveSample];
	sample.temporal = true;
	HW.pContext->End(sample.timestamps[1]);
}

void CAnthologyUpscalerRuntime::MarkGpuDispatch()
{
	if (m_gpuActiveSample >= 0)
		HW.pContext->End(m_gpuSamples[m_gpuActiveSample].timestamps[2]);
}

void CAnthologyUpscalerRuntime::EndGpuProfile()
{
	if (m_gpuActiveSample >= 0)
		HW.pContext->End(m_gpuSamples[m_gpuActiveSample].timestamps[3]);
}

void CAnthologyUpscalerRuntime::EndSceneGpuProfile()
{
	if (m_gpuActiveSample < 0)
		return;
	GpuProfileSample& sample = m_gpuSamples[m_gpuActiveSample];
	HW.pContext->End(sample.timestamps[4]);
	HW.pContext->End(sample.disjoint);
	sample.pending = true;
	m_gpuActiveSample = -1;
}
