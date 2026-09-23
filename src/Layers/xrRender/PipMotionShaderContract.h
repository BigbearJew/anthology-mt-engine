#pragma once

#include <cstddef>
#include <cstdint>

// The tested helper encodes normalized surface owners in the motion-vector mask.
// Unknown helpers must retain the established PiP path, even if they emit motion.
inline bool IsPipMotionOwnerShaderCompatible(const void* data, std::size_t size)
{
    if (!data || size != 1364)
        return false;
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t index = 0; index < size; ++index)
        hash = (hash ^ bytes[index]) * 1099511628211ull;
    return hash == 0x2620ca06de84543cull;
}
