#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// Measures successful Present completions, including hidden capture work
// between them. No clock calls or allocations belong in this accumulator.
class PresentIntervalProfile
{
public:
    static constexpr std::size_t Capacity = 256;
    struct Statistics
    {
        std::size_t count = 0;
        std::uint64_t p50 = 0;
        std::uint64_t p95 = 0;
        std::uint64_t maximum = 0;
    };
    struct Window
    {
        Statistics all;
        Statistics afterCapture;
        Statistics regular;
    };

    void Reset()
    {
        enabled = false;
        hasFrame = false;
        ClearSamples();
    }

    void BeginFrame(std::uint32_t frame, bool requested, std::uint64_t mode)
    {
        if (!requested)
        {
            if (enabled)
                Reset();
            return;
        }
        if (!enabled || !hasFrame || mode != context ||
            (frame != lastFrame && frame - lastFrame != 1))
            ClearSamples();
        enabled = true;
        hasFrame = true;
        lastFrame = frame;
        context = mode;
    }

    void HiddenCapture()
    {
        if (enabled)
            afterCapture = true;
    }

    bool Presented(std::uint64_t ticks, bool successful)
    {
        if (!enabled)
            return false;
        if (!successful || (hasPresent && ticks <= previousPresent))
        {
            ClearSamples();
            return false;
        }
        if (hasPresent && count < Capacity)
            samples[count++] = {ticks - previousPresent, afterCapture};
        previousPresent = ticks;
        hasPresent = true;
        afterCapture = false;
        return count == Capacity;
    }

    Window FinishWindow()
    {
        Window result = {Summarize(-1), Summarize(1), Summarize(0)};
        count = 0;
        // Preserve the previous successful Present across reporting windows.
        return result;
    }

private:
    struct Sample
    {
        std::uint64_t ticks;
        bool afterCapture;
    };
    std::array<Sample, Capacity> samples;
    std::size_t count = 0;
    std::uint64_t previousPresent = 0;
    std::uint64_t context = 0;
    std::uint32_t lastFrame = 0;
    bool enabled = false;
    bool hasFrame = false;
    bool hasPresent = false;
    bool afterCapture = false;

    void ClearSamples()
    {
        count = 0;
        hasPresent = false;
        afterCapture = false;
    }

    Statistics Summarize(int category) const
    {
        std::array<std::uint64_t, Capacity> sorted;
        Statistics result;
        for (std::size_t i = 0; i < count; ++i)
            if (category < 0 || samples[i].afterCapture == (category != 0))
                sorted[result.count++] = samples[i].ticks;
        if (result.count)
        {
            std::sort(sorted.begin(), sorted.begin() + result.count);
            result.p50 = sorted[(result.count * 50 + 99) / 100 - 1];
            result.p95 = sorted[(result.count * 95 + 99) / 100 - 1];
            result.maximum = sorted[result.count - 1];
        }
        return result;
    }
};
