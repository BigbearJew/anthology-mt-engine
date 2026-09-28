#pragma once

#include <cstring>
#include <cstddef>

// Compare the exact stored representation. Numeric comparisons would conflate
// signed zero and continuously dirty unchanged NaNs supplied by a shader input.
inline bool StoreConstantBufferValue(void* destination, const void* value, std::size_t bytes)
{
    if (std::memcmp(destination, value, bytes) == 0)
        return false;
    std::memcpy(destination, value, bytes);
    return true;
}
