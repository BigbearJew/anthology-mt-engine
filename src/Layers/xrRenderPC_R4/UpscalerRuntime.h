#pragma once

#include "OverlayAPI/FSR3Wrapper.h"
#include "OverlayAPI/DLSSWrapper.h"

enum EAnthologyUpscaler : u32
{
    AnthologyUpscalerOff = 0,
    AnthologyUpscalerFSR3 = 1,
    AnthologyUpscalerDLSS = 2,
};

class CAnthologyUpscalerRuntime
{
public:
    bool ProbeAndResolveMode();
    bool Initialize(u32 renderWidth, u32 renderHeight, u32 displayWidth, u32 displayHeight);
	void UpdateJitter(u32 frameIndex);
	void BeginSceneGpuProfile();
	void EndSceneGpuProfile();
	void BeginGpuProfile();
	void MarkGpuDispatch();
	void EndGpuProfile();
	bool Dispatch(ID3D11Resource* color, ID3D11Resource* motion, ID3D11Resource* depth,
		ID3D11Resource* output, bool resetHistory);
    void Shutdown();

    bool IsEnabled() const { return m_mode != AnthologyUpscalerOff; }
    u32 Mode() const { return m_mode; }
    u32 RenderWidth() const { return m_renderWidth; }
    u32 RenderHeight() const { return m_renderHeight; }
    float ResolveRenderScale() const;

private:
	void ResetTemporalState();
	void ReleaseGpuProfile();
	struct GpuProfileSample
	{
		ID3D11Query* disjoint = nullptr;
		ID3D11Query* timestamps[5] = {};
		bool pending = false;
		bool temporal = false;
	};
	GpuProfileSample m_gpuSamples[4];
	int m_gpuActiveSample = -1;
	bool m_gpuProfileUnavailable = false;
	u32 m_gpuProfileCount = 0;
	double m_gpuPrepareMs = 0.0;
	double m_gpuVendorMs = 0.0;
	double m_gpuSceneMs = 0.0;
	double m_gpuMaxMs = 0.0;
	u32 m_jitterFrame = u32(-1);
	u32 m_jitterPhase = 0;
	u32 m_lastDispatchFrame = 0;
	u32 m_lastDispatchTime = 0;
	bool m_historyValid = false;
	Fvector m_previousCameraPosition = {};
	Fvector m_previousCameraDirection = {};
	float m_previousFov = 0.f;
    CFSR3Wrapper m_fsr3;
    CDLSSWrapper m_dlss;
    u32 m_mode = AnthologyUpscalerOff;
    u32 m_renderWidth = 0;
    u32 m_renderHeight = 0;
    u32 m_displayWidth = 0;
    u32 m_displayHeight = 0;
	bool m_dispatchLogged = false;
};

extern CAnthologyUpscalerRuntime g_AnthologyUpscaler;
