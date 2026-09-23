#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

// Negative normal FP16 values strictly above -0.5. Zero remains static world;
// the AO flora sentinel (< -0.5) and positive temporal masks stay separate.
inline float EncodeRenderSurfaceOwner(std::uint16_t slot)
{
    const std::uint32_t half = 0x0400u + slot;
    const std::uint32_t bits = 0x80000000u | (((half >> 10) + 112u) << 23) | ((half & 1023u) << 13);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// Allocation/release happen with object lifetime, never from a draw. Reuse is
// FIFO: old releases no longer present in any retained capture/map need no epoch
// change. Reusing a still-retained ID invalidates all older reconstruction.
template<std::size_t Capacity = 13312>
class RenderSurfaceOwnerPool
{
    static_assert(Capacity > 0 && Capacity <= 13312, "FP16 surface owner capacity");
    struct ReleasedOwner
    {
        std::uint64_t mainSerial = 0;
        std::uint16_t slot = 0;
    };
    std::array<ReleasedOwner, Capacity> released{};
    std::array<bool, Capacity> live{};
    std::size_t virginCount = 0;
    std::size_t releasedCount = 0;
    std::size_t releasedHead = 0;
    std::atomic<std::uint64_t> generation{1};
    std::atomic<std::uint64_t> currentMainSerial{0};
    std::atomic<std::uint64_t> historyBoundary{0};
    std::mutex mutex;

    static void Advance(std::atomic<std::uint64_t>& target, std::uint64_t value)
    {
        auto old = target.load(std::memory_order_relaxed);
        while (old < value && !target.compare_exchange_weak(old, value, std::memory_order_release, std::memory_order_relaxed)) {}
    }

public:
    static constexpr std::uint16_t Invalid = 0xffffu;

    std::uint16_t Acquire()
    {
        std::lock_guard<std::mutex> guard(mutex);
        std::uint16_t slot;
        if (virginCount < Capacity)
            slot = static_cast<std::uint16_t>(virginCount++);
        else if (releasedCount != 0)
        {
            const auto oldest = released[releasedHead];
            releasedHead = (releasedHead + 1) % Capacity;
            --releasedCount;
            slot = oldest.slot;
            // Equal serial can still occur in the previous-main endpoint of
            // this capture. Only releases strictly before the boundary are safe.
            if (oldest.mainSerial >= historyBoundary.load(std::memory_order_acquire))
                generation.fetch_add(1, std::memory_order_release);
        }
        else
            return Invalid;
        live[slot] = true;
        return slot;
    }

    void Release(std::uint16_t slot)
    {
        if (slot == Invalid)
            return;
        std::lock_guard<std::mutex> guard(mutex);
        if (slot < virginCount && live[slot])
        {
            live[slot] = false;
            auto& entry = released[(releasedHead + releasedCount) % Capacity];
            entry.slot = slot;
            entry.mainSerial = currentMainSerial.load(std::memory_order_acquire);
            ++releasedCount;
        }
    }

    std::uint64_t Generation() const { return generation.load(std::memory_order_acquire); }
    void SetMainSerial(std::uint64_t serial) { Advance(currentMainSerial, serial); }
    void SetHistoryBoundary(std::uint64_t serial) { Advance(historyBoundary, serial); }
};
