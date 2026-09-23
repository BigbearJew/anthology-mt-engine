#include "../../src/xrEngine/SvpMotionEpoch.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
unsigned checks = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

bool close(float a, float b)
{
    return std::fabs(a - b) < 0.000001f;
}

SvpMotionEpochInput normalInput()
{
    SvpMotionEpochInput input;
    input.currentMainSerial = 42;
    input.previousMainSerial = 41;
    input.currentTime = 1010;
    input.previousTime = 1000;
    input.captureTime = 1005;
    input.hasHistory = true;
    input.wasSeeded = true;
    input.newCapture = true;
    input.ownerGenerationMatches = true;
    return input;
}

void checkWarmup(const SvpMotionEpochInput& input, bool previousValid, const char* message)
{
    const auto result = SelectSvpMotionEpoch(input);
    check(result.mode == 0 && close(result.alpha, 0.f) && result.previousValid == previousValid, message);
}

void boundaries()
{
    auto input = normalInput();
    auto result = SelectSvpMotionEpoch(input);
    check(result.previousValid && result.elapsed == 10 && result.mode == 1 && close(result.alpha, .5f),
        "capture between adjacent main poses has half-interval seed, not a full-frame displacement");
    input.captureTime = input.previousTime;
    result = SelectSvpMotionEpoch(input);
    check(result.mode == 1 && close(result.alpha, 0.f), "capture at previous main endpoint has alpha zero");
    input.captureTime = input.currentTime;
    result = SelectSvpMotionEpoch(input);
    check(result.mode == 1 && close(result.alpha, 1.f), "capture at current main endpoint has alpha one");
    input.captureTime = input.previousTime - 1;
    checkWarmup(input, true, "older capture is not silently clamped into the current pose interval");
    input.captureTime = input.currentTime + 1;
    checkWarmup(input, true, "future capture is not silently clamped into the current pose interval");

    input = normalInput();
    input.currentTime = input.previousTime;
    checkWarmup(input, false, "zero main time cannot divide or advance history");
    input.currentTime = input.previousTime + 250;
    result = SelectSvpMotionEpoch(input);
    check(result.previousValid && result.mode == 1 && result.elapsed == 250, "250ms boundary remains eligible");
    input.currentTime = input.previousTime + 251;
    checkWarmup(input, false, "main gap beyond 250ms resets temporal eligibility");
    input.currentTime = input.previousTime - 1;
    checkWarmup(input, false, "clock rollback is not mistaken for a small elapsed interval");

    input = normalInput();
    input.previousTime = 0xfffffffcu;
    input.currentTime = 4;
    input.captureTime = 0;
    result = SelectSvpMotionEpoch(input);
    check(result.elapsed == 8 && result.mode == 1 && close(result.alpha, .5f),
        "uint32 millisecond wrap preserves a capture between the two main poses");
    input.captureTime = 0xfffffffbu;
    checkWarmup(input, true, "capture before a wrapped interval remains outside it");
    input.captureTime = 5;
    checkWarmup(input, true, "capture after a wrapped interval remains outside it");

    input = normalInput();
    input.currentMainSerial = input.previousMainSerial;
    checkWarmup(input, false, "same-main invocation cannot consume history twice");
    input.currentMainSerial = input.previousMainSerial + 2;
    checkWarmup(input, false, "missing main map cannot be skipped by the advection chain");
    input.currentMainSerial = input.previousMainSerial - 1;
    checkWarmup(input, false, "serial rollback cannot reuse old camera matrices");
    input.previousMainSerial = 0;
    input.currentMainSerial = 1;
    checkWarmup(input, false, "untracked serial zero supplies no previous main pose");
    const auto lastSerial = std::numeric_limits<std::uint64_t>::max();
    input.previousMainSerial = lastSerial - 1;
    input.currentMainSerial = lastSerial;
    result = SelectSvpMotionEpoch(input);
    check(result.previousValid && result.mode == 1, "last nonzero serial remains valid before overflow");
    input.previousMainSerial = lastSerial;
    input.currentMainSerial = 0;
    checkWarmup(input, false, "uint64 serial wrap cannot create false pose adjacency");
}

void resets()
{
    auto input = normalInput();
    input.hasHistory = false;
    checkWarmup(input, false, "missing depth/owner map forces warmup even with stale seeded flag");
    input = normalInput();
    input.ownerGenerationMatches = false;
    checkWarmup(input, false, "recycled owner generation cannot seed from older identities");
    input.newCapture = false;
    checkWarmup(input, false, "recycled owner generation cannot advect older captured identities");
    input = normalInput();
    input.newCapture = false;
    input.wasSeeded = false;
    checkWarmup(input, true, "same capture cannot become seeded merely because one more main frame elapsed");
    input.wasSeeded = true;
    input.captureTime = 5;
    const auto result = SelectSvpMotionEpoch(input);
    check(result.mode == 2 && close(result.alpha, 0.f), "already-seeded capture uses one-step advection irrespective of capture age");

    // Model caller state updates after disabled/protected mode, reset or allocation.
    input = normalInput();
    input.hasHistory = false;
    input.wasSeeded = false;
    checkWarmup(input, false, "fresh capture without previous map writes depth/owner warmup");
    input.previousMainSerial = input.currentMainSerial;
    ++input.currentMainSerial;
    input.previousTime = input.currentTime;
    input.currentTime += 10;
    input.hasHistory = true;
    input.newCapture = false;
    checkWarmup(input, true, "warmup capture is not reseeded late using the wrong pose interval");
    input.previousMainSerial = input.currentMainSerial;
    ++input.currentMainSerial;
    input.previousTime = input.currentTime;
    input.currentTime += 10;
    input.captureTime = input.previousTime + 5;
    input.newCapture = true;
    const auto recovered = SelectSvpMotionEpoch(input);
    check(recovered.mode == 1 && close(recovered.alpha, .5f), "next fresh capture recovers after a complete main-depth sample");

    input.hasHistory = false; // Caller resets history when reuse/renderer mode changes.
    input.wasSeeded = false;
    input.newCapture = false;
    checkWarmup(input, false, "mode change cannot preserve stale temporal state");
}

void cadence(unsigned fps, unsigned interval)
{
    SvpMotionEpochInput input;
    input.ownerGenerationMatches = true;
    input.previousTime = 5000;
    std::uint64_t deviceFrame = 0;
    unsigned seeds = 0;
    unsigned advances = 0;
    for (unsigned main = 1; main <= 256; ++main)
    {
        input.currentMainSerial = main;
        input.currentTime = 5000u + (main * 1000u + fps / 2u) / fps;
        const auto elapsed = input.currentTime - input.previousTime;
        input.newCapture = (main - 1) % interval == 0;
        if (input.newCapture)
        {
            // Hidden capture consumes Device frame numbers but no main serial.
            ++deviceFrame;
            input.captureTime = input.previousTime + elapsed / 2u;
        }
        ++deviceFrame;
        const auto result = SelectSvpMotionEpoch(input);
        const int expected = main == 1 ? 0 : (input.newCapture ? 1 : (main > interval ? 2 : 0));
        check(result.mode == expected, "normal cadence chooses warmup/seed/advection independently of hidden frame numbers");
        check(result.previousValid == (main > 1), "every completed adjacent main has valid depth/owner history");
        check(result.elapsed == elapsed, "main time span is independent of hidden Device-frame count");
        check(deviceFrame > input.currentMainSerial, "cadence fixture actually includes hidden engine frames");
        if (result.mode == 1)
        {
            ++seeds;
            check(close(result.alpha, float(elapsed / 2u) / float(elapsed)), "hidden capture uses its measured sub-main pose time");
        }
        if (result.mode == 2) ++advances;
        input.hasHistory = true;
        input.wasSeeded = result.mode != 0;
        input.previousMainSerial = input.currentMainSerial;
        input.previousTime = input.currentTime;
    }
    check(seeds == 255u / interval, "each new post-warmup capture is seeded exactly once");
    check(advances == 256u - interval - seeds, "reuse interval controls the expected number of one-main advections");
}
}

int main()
{
    boundaries();
    resets();
    const unsigned rates[] = {30, 60, 90, 144};
    for (const auto fps : rates)
        for (unsigned interval = 1; interval <= 8; ++interval)
            cadence(fps, interval);
    std::printf("PASS: %u PiP motion epoch checks; intervals1..8 at30/60/90/144FPS, wrap/reset/owner-generation cases\n", checks);
}
