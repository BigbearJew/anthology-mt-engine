#pragma once

#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/backends/dx11/ffx_dx11.h>

// Thin, renderer-owned FSR 3.1.2 upscaling wrapper. Frame generation is
// deliberately not exposed here: it needs a separate present/latency pass.
class CFSR3Wrapper
{
public:
    struct ContextParameters
    {
        FfxDimensions2D maxRenderSize = {0, 0};
        FfxDimensions2D displaySize = {0, 0};
        ID3D11Device* device = nullptr;
    };

    struct DrawParameters
    {
        ID3D11DeviceContext* deviceContext = nullptr;
        ID3D11Resource* unresolvedColor = nullptr;
        ID3D11Resource* motionVectors = nullptr;
        ID3D11Resource* depth = nullptr;
        ID3D11Resource* reactive = nullptr;
        ID3D11Resource* output = nullptr;
        u32 renderWidth = 0;
        u32 renderHeight = 0;
        u32 displayWidth = 0;
        u32 displayHeight = 0;
        bool reset = false;
        float jitterX = 0.f;
        float jitterY = 0.f;
        bool sharpening = true;
        float sharpness = 0.2f;
        float frameTimeMs = 16.667f;
        float nearPlane = VIEWPORT_NEAR;
        float farPlane = 500.f;
        float verticalFov = PI_DIV_3;
    };

    bool Create(const ContextParameters& params);
    bool Draw(const DrawParameters& params);
    void Destroy();
    bool IsCreated() const { return m_created; }
    ~CFSR3Wrapper();

private:
    bool m_created = false;
    FfxFsr3UpscalerContext m_context = {};
    FfxFsr3UpscalerContextDescription m_description = {};
    ID3D11Texture2D* m_dilatedDepth = nullptr;
    ID3D11Texture2D* m_dilatedMotion = nullptr;
    ID3D11Texture2D* m_reconstructedPrevDepth = nullptr;
    xr_vector<char> m_scratch;
};
