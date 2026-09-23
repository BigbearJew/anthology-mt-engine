#pragma once

#include <cstdint>

// A capture replaces an internal engine frame, so every attempt must be
// followed by a main frame, including attempts that fail to publish a texture.
class SvpTemporalSchedule
{
    std::uint32_t decisionFrame = 0;
    std::uint32_t captureFrame = 0;
    bool hasDecision = false;
    bool hasCapture = false;
    bool captureDecision = false;

public:
    bool Cached(std::uint32_t frame, bool& decision) const
    {
        if (!hasDecision || decisionFrame != frame)
            return false;
        decision = captureDecision;
        return true;
    }

    void Reset()
    {
        hasCapture = false;
        // Keep this frame's decision stable across camera/weapon callbacks.
    }

    void Captured(std::uint32_t frame, std::uint32_t /*time*/)
    {
        captureFrame = frame;
        hasCapture = true;
    }

    bool Select(std::uint32_t frame, std::uint32_t /*time*/, bool active,
        bool textureReady, std::uint32_t mainInterval, bool cameraChanged, std::uint32_t legacyDelay = 0)
    {
        if (hasDecision && decisionFrame == frame)
            return captureDecision;

        const bool mustPresent = hasDecision && captureDecision && frame - decisionFrame == 1;
        decisionFrame = frame;
        hasDecision = true;
        mainInterval = mainInterval < 1 ? 1 : (mainInterval > 8 ? 8 : mainInterval);
        // Reuse is measured in presented main frames. A fixed time deadline
        // overrides the slider at low FPS and can halve presentation frequency.
        captureDecision = active && !mustPresent &&
            (legacyDelay >= 2 ? frame % legacyDelay == 0 :
                (!textureReady || !hasCapture || cameraChanged ||
                    frame - captureFrame > mainInterval));
        return captureDecision;
    }
};
