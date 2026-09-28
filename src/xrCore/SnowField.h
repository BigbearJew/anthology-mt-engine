#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include "RuntimeSeason.h"

namespace anthology { namespace snow {
enum Parameter
{
    Enabled, Height, Distance, Density, Variation, DriftSize, Resistance,
    FallEnabled, FlakeSize, FallSpeed, FallDensity, FallDistance, Wind, ParameterCount
};

inline std::atomic<float>* Parameters()
{
    static std::atomic<float> values[ParameterCount] = {
        1.f, .25f, 40.f, 1.f, .7f, 4.f, .6f,
        1.f, 1.f, 1.f, 1.f, 45.f, 1.f
    };
    return values;
}
inline float Get(Parameter parameter) { return Parameters()[parameter].load(std::memory_order_relaxed); }
inline float Minimum(Parameter parameter)
{
    const float values[] = {0,0,5,0,0,.5f,0,0,.25f,.25f,0,5,0};
    return values[parameter];
}
inline float Maximum(Parameter parameter)
{
    const float values[] = {1,1.2f,60,1,1,20,.9f,1,2,4,2,60,2};
    return values[parameter];
}
inline void Set(Parameter parameter, float value)
{
    if (std::isfinite(value)) Parameters()[parameter].store(
        std::max(Minimum(parameter), std::min(Maximum(parameter), value)), std::memory_order_relaxed);
}
inline bool Active() { return runtime_season() == 4 && Get(Enabled) >= .5f && Get(Height) > .001f && Get(Density) > 0.f; }
inline float Saturate(float value) { return std::max(0.f, std::min(1.f, value)); }
inline float Smooth(float value) { value = Saturate(value); return value * value * (3.f - 2.f * value); }
inline float Lerp(float a, float b, float value) { return a + (b - a) * value; }

// Keep integer hashing and operation order identical to anthology_snow_field.h.
inline float Hash(int x, int z)
{
    std::uint32_t h = std::uint32_t(x) * 374761393u + std::uint32_t(z) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0x00ffffffu) * (1.f / 16777216.f);
}
inline float Noise(float x, float z)
{
    const int ix = int(std::floor(x)), iz = int(std::floor(z));
    const float u = Smooth(x - float(ix)), v = Smooth(z - float(iz));
    return Lerp(Lerp(Hash(ix, iz), Hash(ix + 1, iz), u),
        Lerp(Hash(ix, iz + 1), Hash(ix + 1, iz + 1), u), v);
}
inline float Depth(float x, float z, float height, float density, float variation, float scale)
{
    if (height <= 0.f || density <= 0.f) return 0.f;
    scale = std::max(.5f, scale);
    const float cover = density >= 1.f ? 1.f :
        Smooth((Noise(x / (scale * 2.f) + 31.f, z / (scale * 2.f) - 17.f) - (1.f - density)) * 5.f);
    const float drift = .75f * Noise(x / scale, z / scale) + .25f * Noise(x / (scale * .37f), z / (scale * .37f));
    return height * cover * Lerp(1.f, .25f + .75f * drift, Saturate(variation));
}
inline float Depth(float x, float z)
{
    return Depth(x, z, Get(Height), Get(Density), Get(Variation), Get(DriftSize));
}
inline float SpeedFactor(float depth, float resistance)
{
    return 1.f - Saturate(resistance) * Saturate(depth / .6f);
}

// Render publishes ground/cover samples; physics reads them without ray casts,
// allocation, or a render/physics lock. Missing cells conservatively have no snow.
class ContactCache
{
public:
    static constexpr int Side = 256;
    static constexpr std::uint64_t Invalid = ~std::uint64_t(0);
    struct Cell
    {
        std::atomic<std::uint32_t> revision{0};
        std::atomic<bool> ready{false};
        std::atomic<std::uint64_t> key{Invalid};
        std::atomic<float> ground{0.f};
        std::atomic<float> remaining{1.f};
    };
    Cell cells[Side * Side];
    std::atomic<float> spacing{.5f};

    static std::uint64_t Key(int x, int z) { return (std::uint64_t(std::uint32_t(x)) << 32) | std::uint32_t(z); }
    static unsigned Index(int x, int z) { return (unsigned(x) & (Side - 1)) + (unsigned(z) & (Side - 1)) * Side; }
    void Clear(float step)
    {
        for (auto& cell : cells) cell.ready.store(false, std::memory_order_release);
        spacing.store(std::max(.1f, step), std::memory_order_release);
    }
    void Publish(int x, int z, float ground, float remaining)
    {
        Cell& cell = cells[Index(x, z)];
        if(cell.ready.load() && cell.key.load()==Key(x,z) && cell.ground.load()==ground)
        {
            cell.remaining.store(Saturate(remaining));
            return;
        }
        cell.revision.fetch_add(1);
        cell.ready.store(false);
        cell.ground.store(ground);
        cell.remaining.store(Saturate(remaining));
        cell.key.store(Key(x, z));
        cell.ready.store(true);
        cell.revision.fetch_add(1);
    }
    bool Read(int x, int z, float& ground, float& remaining) const
    {
        const Cell& cell = cells[Index(x, z)];
        const auto key = Key(x, z);
        const auto revision = cell.revision.load();
        if ((revision & 1) || !cell.ready.load() || cell.key.load() != key) return false;
        ground = cell.ground.load();
        remaining = cell.remaining.load();
        return cell.revision.load() == revision && cell.ready.load();
    }
    float At(float x, float y, float z) const
    {
        const float step = spacing.load(std::memory_order_acquire);
        const int ix = int(std::floor(x / step)), iz = int(std::floor(z / step));
        float ground[4], remaining[4];
        if (!Read(ix, iz, ground[0], remaining[0]) || !Read(ix+1, iz, ground[1], remaining[1]) ||
            !Read(ix, iz+1, ground[2], remaining[2]) || !Read(ix+1, iz+1, ground[3], remaining[3])) return 0.f;
        const float u = Saturate(x / step - ix), v = Saturate(z / step - iz);
        const float surface = Lerp(Lerp(ground[0], ground[1], u), Lerp(ground[2], ground[3], u), v);
        if (y < surface - .15f || y > surface + .45f) return 0.f;
        return Depth(x, z) * Lerp(Lerp(remaining[0], remaining[1], u), Lerp(remaining[2], remaining[3], u), v);
    }
};
inline ContactCache& Contacts() { static ContactCache cache; return cache; }

struct Contact { float x, y, z, radius; };
class ContactQueue
{
    std::mutex mutex;
    Contact entries[256];
    unsigned count = 0;
public:
    void Push(float x, float y, float z, float radius)
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (count < 256) entries[count++] = {x, y, z, radius};
    }
    unsigned Drain(Contact* output)
    {
        std::lock_guard<std::mutex> guard(mutex);
        const unsigned size = count;
        std::copy(entries, entries + size, output);
        count = 0;
        return size;
    }
};
inline ContactQueue& Touches() { static ContactQueue queue; return queue; }
inline float Movement(float x, float y, float z)
{
    if (!Active()) return 1.f;
    const float depth = Contacts().At(x,y,z);
    if (depth <= .002f) return 1.f;
    Touches().Push(x,y,z,.32f);
    return SpeedFactor(depth, Get(Resistance));
}
} }
