#pragma once
#include <cstddef>

// Select an existing mip; never enlarge its dimensions to satisfy BC alignment.
inline std::size_t SelectTextureMip(std::size_t width, std::size_t height,
    std::size_t levels, int requested, bool compressed)
{
    std::size_t selected = 0;
    while (requested > 0 && selected + 1 < levels)
    {
        const std::size_t nextWidth = width / 2, nextHeight = height / 2;
        if (nextWidth < 4 || nextHeight < 4) break;
        if (compressed && ((nextWidth & 3) || (nextHeight & 3))) break;
        width = nextWidth;
        height = nextHeight;
        ++selected;
        --requested;
    }
    return selected;
}
