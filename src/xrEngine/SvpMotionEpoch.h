#pragma once

#include <cstdint>

// Presented-main serials exclude hidden PiP renders. Times use the engine's
// wrapping 32-bit millisecond clock. Resource eligibility is checked by caller.
struct SvpMotionEpochInput
{
    std::uint64_t currentMainSerial = 0;
    std::uint64_t previousMainSerial = 0;
    std::uint32_t currentTime = 0;
    std::uint32_t previousTime = 0;
    std::uint32_t captureTime = 0;
    bool hasHistory = false;
    bool wasSeeded = false;
    bool newCapture = false;
    bool ownerGenerationMatches = false;
};

struct SvpMotionEpochDecision
{
    bool previousValid = false;
    std::uint32_t elapsed = 0;
    int mode = 0; // 0: depth/owner warmup; 1: new-capture seed; 2: advection.
    float alpha = 0.f;
};

inline SvpMotionEpochDecision SelectSvpMotionEpoch(const SvpMotionEpochInput& input)
{
    SvpMotionEpochDecision result;
    result.elapsed = input.currentTime - input.previousTime;
    // Zero/untracked serials and serial rollback/overflow cannot represent an
    // adjacent presented pose, even if the unsigned time difference is small.
    const bool adjacent = input.previousMainSerial != 0 &&
        input.currentMainSerial > input.previousMainSerial &&
        input.currentMainSerial - input.previousMainSerial == 1;
    result.previousValid = input.hasHistory && input.ownerGenerationMatches &&
        adjacent && result.elapsed > 0 && result.elapsed <= 250;
    if (!result.previousValid)
        return result;

    if (input.newCapture)
    {
        const std::uint32_t captureOffset = input.captureTime - input.previousTime;
        if (captureOffset <= result.elapsed)
        {
            result.mode = 1;
            result.alpha = float(captureOffset) / float(result.elapsed);
        }
    }
    else if (input.wasSeeded)
    {
        result.mode = 2;
    }
    // A capture missed during warmup cannot be seeded on a later main frame:
    // its pose time no longer lies in the available adjacent-main interval.
    return result;
}
