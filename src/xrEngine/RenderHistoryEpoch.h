#pragma once

#include <cstdint>

// Hidden viewport ticks do not consume a main-view motion-history sample.
class RenderHistoryEpoch
{
public:
    enum class Step { Same, Reset, Advance };

    Step Begin(std::uint64_t mainSerial)
    {
        if (mainSerial != 0 && mainSerial == lastSerial)
            return Step::Same;
        const bool consecutive = lastSerial != 0 && mainSerial > lastSerial && mainSerial - lastSerial == 1;
        lastSerial = mainSerial;
        previousValid = consecutive;
        return consecutive ? Step::Advance : Step::Reset;
    }

    void Reset() { lastSerial = 0; previousValid = false; }
    bool Initialized() const { return lastSerial != 0; }
    bool PreviousValid() const { return previousValid; }

private:
    std::uint64_t lastSerial = 0;
    bool previousValid = false;
};
