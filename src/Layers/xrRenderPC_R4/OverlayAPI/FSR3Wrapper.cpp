#include "stdafx.h"
#include "FSR3Wrapper.h"

namespace
{
void FsrMessage(FfxMsgType type, const wchar_t* message)
{
    string1024 text = {};
    const int written = WideCharToMultiByte(CP_UTF8, 0, message, -1, text, sizeof(text) - 1, nullptr, nullptr);
    text[written > 0 ? written - 1 : 0] = 0;
    Msg("%s [UPSCALER/FSR3] %s", type == FFX_MESSAGE_TYPE_ERROR ? "!" : "~", text);
}
}

bool CFSR3Wrapper::Create(const ContextParameters& params)
{
    Destroy();
    if (!params.device || !params.maxRenderSize.width || !params.maxRenderSize.height ||
        !params.displaySize.width || !params.displaySize.height || HW.FeatureLevel < D3D_FEATURE_LEVEL_11_0)
        return false;

    m_scratch.resize(ffxGetScratchMemorySizeDX11(1));
    FfxErrorCode code = ffxGetInterfaceDX11(&m_description.backendInterface, ffxGetDeviceDX11(params.device),
        m_scratch.data(), m_scratch.size(), 1);
    if (code != FFX_OK)
    {
        Msg("! [UPSCALER/FSR3] DX11 backend creation failed: %d", code);
        Destroy();
        return false;
    }

    m_description.maxRenderSize = params.maxRenderSize;
    m_description.maxUpscaleSize = params.displaySize;
    m_description.fpMessage = FsrMessage;
	// SSS/NVG/thermal have already authored a display-referred signal. Do not
	// advertise HDR without a true scene-color buffer and matching exposure.
#ifdef DEBUG
    m_description.flags |= FFX_FSR3UPSCALER_ENABLE_DEBUG_CHECKING;
#endif

    code = ffxFsr3UpscalerContextCreate(&m_context, &m_description);
    if (code != FFX_OK)
    {
        Msg("! [UPSCALER/FSR3] context creation failed: %d", code);
        Destroy();
        return false;
    }
	m_created = true;

	FfxFsr3UpscalerSharedResourceDescriptions shared = {};
	code = ffxFsr3UpscalerGetSharedResourceDescriptions(&m_context, &shared);
	if (code != FFX_OK)
	{
		Msg("! [UPSCALER/FSR3] shared resource description query failed: %d", code);
		Destroy();
		return false;
	}

	auto resolveFormat = [](FfxSurfaceFormat format)
	{
		switch (format)
		{
		case FFX_SURFACE_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
		case FFX_SURFACE_FORMAT_R32_UINT: return DXGI_FORMAT_R32_UINT;
		case FFX_SURFACE_FORMAT_R16G16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
		case FFX_SURFACE_FORMAT_R32G32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
		default: return DXGI_FORMAT_UNKNOWN;
		}
	};

	auto makeTexture = [&](const FfxCreateResourceDescription& requested, ID3D11Texture2D** output)
	{
		const FfxResourceDescription& resource = requested.resourceDescription;
		const DXGI_FORMAT format = resolveFormat(resource.format);
		if (resource.type != FFX_RESOURCE_TYPE_TEXTURE2D || format == DXGI_FORMAT_UNKNOWN ||
			!resource.width || !resource.height)
			return false;

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = resource.width;
		desc.Height = resource.height;
		desc.MipLevels = _max(1u, resource.mipCount);
		desc.ArraySize = _max(1u, resource.depth);
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (resource.usage & FFX_RESOURCE_USAGE_UAV)
			desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		if (resource.usage & FFX_RESOURCE_USAGE_RENDERTARGET)
			desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
		return SUCCEEDED(params.device->CreateTexture2D(&desc, nullptr, output));
	};

	if (!makeTexture(shared.dilatedDepth, &m_dilatedDepth) ||
		!makeTexture(shared.dilatedMotionVectors, &m_dilatedMotion) ||
		!makeTexture(shared.reconstructedPrevNearestDepth, &m_reconstructedPrevDepth))
	{
		Msg("! [UPSCALER/FSR3] shared resource creation failed");
		Destroy();
		return false;
	}

    m_created = true;
    return true;
}

bool CFSR3Wrapper::Draw(const DrawParameters& params)
{
    if (!m_created || !params.deviceContext || !params.unresolvedColor || !params.motionVectors ||
        !params.depth || !params.output)
        return false;

    FfxFsr3UpscalerDispatchDescription desc = {};
    desc.commandList = ffxGetCommandListDX11(params.deviceContext);
    desc.color = ffxGetResourceDX11(params.unresolvedColor, GetFfxResourceDescriptionDX11(params.unresolvedColor), nullptr);
    desc.depth = ffxGetResourceDX11(params.depth, GetFfxResourceDescriptionDX11(params.depth), nullptr);
    desc.motionVectors = ffxGetResourceDX11(params.motionVectors, GetFfxResourceDescriptionDX11(params.motionVectors), nullptr);
    desc.exposure = ffxGetResourceDX11(nullptr, FfxResourceDescription{}, nullptr);
    desc.reactive = ffxGetResourceDX11(params.reactive,
        params.reactive ? GetFfxResourceDescriptionDX11(params.reactive) : FfxResourceDescription{}, nullptr);
    desc.transparencyAndComposition = desc.reactive;
    desc.dilatedDepth = ffxGetResourceDX11(m_dilatedDepth, GetFfxResourceDescriptionDX11(m_dilatedDepth), nullptr,
        FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.dilatedMotionVectors = ffxGetResourceDX11(m_dilatedMotion, GetFfxResourceDescriptionDX11(m_dilatedMotion), nullptr,
        FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.reconstructedPrevNearestDepth = ffxGetResourceDX11(m_reconstructedPrevDepth,
        GetFfxResourceDescriptionDX11(m_reconstructedPrevDepth), nullptr, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.output = ffxGetResourceDX11(params.output, GetFfxResourceDescriptionDX11(params.output), nullptr,
        FFX_RESOURCE_STATE_UNORDERED_ACCESS);

    desc.jitterOffset = {params.jitterX, params.jitterY};
    // SSS stores currentUV - previousUV. FSR expects current-to-previous
    // displacement, so convert that UV delta to signed render pixels.
    desc.motionVectorScale = {-float(params.renderWidth), -float(params.renderHeight)};
    desc.renderSize = {params.renderWidth, params.renderHeight};
    desc.upscaleSize = {params.displayWidth, params.displayHeight};
    desc.enableSharpening = params.sharpening;
    desc.sharpness = params.sharpness;
    desc.frameTimeDelta = params.frameTimeMs;
    desc.preExposure = 1.f;
    desc.reset = params.reset;
    desc.cameraNear = params.nearPlane;
    desc.cameraFar = params.farPlane;
    desc.cameraFovAngleVertical = params.verticalFov;
    desc.viewSpaceToMetersFactor = 1.f;

    const FfxErrorCode code = ffxFsr3UpscalerContextDispatch(&m_context, &desc);
    if (code != FFX_OK)
    {
        Msg("! [UPSCALER/FSR3] dispatch failed: %d", code);
        return false;
    }
    return true;
}

void CFSR3Wrapper::Destroy()
{
    if (m_created)
        ffxFsr3UpscalerContextDestroy(&m_context);
    m_created = false;
    _RELEASE(m_dilatedDepth);
    _RELEASE(m_dilatedMotion);
    _RELEASE(m_reconstructedPrevDepth);
    m_scratch.clear();
    ZeroMemory(&m_context, sizeof(m_context));
    ZeroMemory(&m_description, sizeof(m_description));
}

CFSR3Wrapper::~CFSR3Wrapper()
{
    Destroy();
}
