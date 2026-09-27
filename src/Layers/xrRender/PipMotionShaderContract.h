#pragma once

#include <cstddef>
#include <cstdint>

// The tested helper encodes normalized surface owners in the motion-vector mask.
// Unknown helpers must retain the established PiP path, even if they emit motion.
inline bool IsPipMotionOwnerShaderCompatible(const void* data, std::size_t size)
{
    // v149 and the reviewed v151 helper (preserves negative foliage masks).
    if (!data || size < 1000 || size > 4096)
        return false;
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t index = 0; index < size; ++index)
        hash = (hash ^ bytes[index]) * 1099511628211ull;
    if ((size == 1364 && hash == 0x2620ca06de84543cull) ||
        (size == 1378 && hash == 0x33af2d4645d9ffa6ull)) return true;

    // Git's Windows checkout may change LF to CRLF without changing the shader.
    // Normalize only that pair; altered source and extra lines still fail closed.
    hash = 14695981039346656037ull;
    std::size_t normalizedSize = 0;
    for (std::size_t index = 0; index < size; ++index)
    {
        if (bytes[index] == '\r' && index + 1 < size && bytes[index + 1] == '\n') continue;
        hash = (hash ^ bytes[index]) * 1099511628211ull;
        ++normalizedSize;
    }
    return normalizedSize == 1360 && hash == 0x389bf8377fc4b042ull;
}
