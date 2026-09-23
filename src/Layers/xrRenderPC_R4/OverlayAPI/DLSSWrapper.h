#pragma once

#include <ngx/nvsdk_ngx.h>
#include <ngx/nvsdk_ngx_helpers.h>

// Direct NGX integration. Alpha7's replacement executable and Streamline
// interposer are intentionally not used.
class CDLSSWrapper
{
public:
    struct ContextParameters
    {
        u32 renderWidth = 0;
        u32 renderHeight = 0;
        u32 displayWidth = 0;
        u32 displayHeight = 0;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
    };

    struct DrawParameters
    {
		ID3D11Resource* unresolvedColor = nullptr;
		ID3D11Resource* motionVectors = nullptr;
		ID3D11Resource* depth = nullptr;
		ID3D11Resource* output = nullptr;
        u32 renderWidth = 0;
        u32 renderHeight = 0;
        bool reset = false;
        float jitterX = 0.f;
        float jitterY = 0.f;
		float frameTimeMs = 16.667f;
    };

    bool Probe(ID3D11Device* device);
    bool Create(const ContextParameters& params, u32 qualityPreset);
    bool Draw(const DrawParameters& params);
    void DestroyFeature();
    void Shutdown();
    bool IsAvailable() const { return m_available; }
    bool IsCreated() const { return m_handle != nullptr; }
    ~CDLSSWrapper();

private:
    NVSDK_NGX_PerfQuality_Value ResolveQuality(u32 qualityPreset) const;
    NVSDK_NGX_Parameter* m_parameters = nullptr;
    NVSDK_NGX_Handle* m_handle = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    bool m_initialized = false;
    bool m_available = false;
};
