#pragma once

#include <cstdint>
#include <cstring>

// Policy only; D3D resources are released by the render owner after ClearState.
namespace texture_residency
{
using Bytes = std::uint64_t;
constexpr Bytes MiB = 1024ull * 1024ull;
constexpr unsigned ScanLimit = 128;
constexpr unsigned ReleaseLimit = 2;
constexpr Bytes ReleaseBytes = 64 * MiB;

inline void CanonicalizeFileName(char* name)
{
    if (!name || std::strchr(name, '$'))
        return; // Named render targets retain their API identity.
    for (; *name; ++name)
    {
        if (*name == '/') *name = '\\';
        else if (*name >= 'A' && *name <= 'Z') *name += 'a' - 'A';
    }
}

inline bool PdaMap(const char* name)
{
    return name && (std::strncmp(name, "map\\", 4) == 0 ||
        std::strcmp(name, "ui\\ui_global_map") == 0 || std::strcmp(name, "ui\\ui_nomap2") == 0);
}

inline bool DemandOnly(const char* name)
{
    // PDA maps join the existing loading-screen queue, including the global atlas.
    return name && !PdaMap(name) && (std::strncmp(name, "sky\\", 4) == 0 ||
        std::strncmp(name, "ui\\", 3) == 0);
}

inline bool Pressure(bool previous, Bytes local, Bytes budget, Bytes allocated,
    Bytes availableRam, Bytes totalRam)
{
    const Bytes ramReserve = totalRam / 16 > 512 * MiB ? totalRam / 16 : 512 * MiB;
    const bool ramLow = totalRam && availableRam < ramReserve * (previous ? 2 : 1);
    // Division first avoids overflow and preserves the 64-bit allocation total.
    return ramLow || (budget && (local >= budget / 100 * (previous ? 80 : 90) ||
        allocated >= budget / 100 * (previous ? 75 : 85)));
}

inline bool Expired(std::uint32_t now, std::uint32_t loadedAt,
    std::uint32_t usedAt, bool used, bool demandOnly, bool pressure)
{
    if (!demandOnly && !pressure) return false;
    const std::uint32_t grace = demandOnly ? (pressure ? 5000u : 15000u) : (used ? 60000u : 20000u);
    // A newly reloaded image always gets a full grace period, including tick wrap.
    return std::uint32_t(now - loadedAt) >= grace &&
        (!used || std::uint32_t(now - usedAt) >= grace);
}
}
