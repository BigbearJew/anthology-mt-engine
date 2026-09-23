#pragma once

#include <d3dcommon.h>
#include <vector>

// Shader creation runs concurrently for different materials. Options belong to
// the calling compilation thread, including the clear after a tessellation pass.
class ShaderCompileOptions
{
public:
    static void add(const char* name, const char* value)
    {
        current().push_back({name, value});
    }

    static void clear() { current().clear(); }
    static const std::vector<D3D_SHADER_MACRO>& get() { return current(); }

private:
    static std::vector<D3D_SHADER_MACRO>& current()
    {
        static thread_local std::vector<D3D_SHADER_MACRO> options;
        return options;
    }
};
