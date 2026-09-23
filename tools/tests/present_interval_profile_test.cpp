#include "../../src/Layers/xrRender/PresentIntervalProfile.h"

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

void capture_pacing()
{
    PresentIntervalProfile profile;
    std::uint32_t frame = 1;
    std::uint64_t ticks = 1000;
    profile.BeginFrame(frame, true, 1);
    check(!profile.Presented(ticks, true), "first successful Present establishes a baseline");
    for (unsigned i = 1; i <= PresentIntervalProfile::Capacity; ++i)
    {
        if (i % 4 == 0)
        {
            profile.BeginFrame(++frame, true, 1);
            profile.HiddenCapture();
            ticks += 7;
        }
        profile.BeginFrame(++frame, true, 1);
        profile.BeginFrame(frame, true, 1); // Begin and End can both observe the frame.
        ticks += 10;
        check(profile.Presented(ticks, true) == (i == PresentIntervalProfile::Capacity),
            "report only at the bounded window size");
    }
    const auto window = profile.FinishWindow();
    check(window.all.count == 256 && window.afterCapture.count == 64 && window.regular.count == 192,
        "every interval belongs to exactly one category");
    check(window.all.p50 == 10 && window.all.p95 == 17 && window.all.maximum == 17,
        "hidden work creates a long tail despite regular main render cost");
    check(window.regular.p50 == 10 && window.regular.p95 == 10 && window.regular.maximum == 10,
        "regular intervals exclude hidden capture work");
    check(window.afterCapture.p50 == 17 && window.afterCapture.p95 == 17 && window.afterCapture.maximum == 17,
        "capture intervals include the hidden capture and following main work");
    profile.BeginFrame(++frame, true, 1);
    profile.Presented(ticks + 10, true);
    check(profile.FinishWindow().all.p50 == 10, "reporting preserves the previous Present baseline");
}

void disabled_and_context_resets()
{
    PresentIntervalProfile profile;
    profile.BeginFrame(1, false, 0);
    profile.HiddenCapture();
    check(!profile.Presented(100, true), "disabled profiling does not collect a baseline");
    check(profile.FinishWindow().all.count == 0, "disabled window remains empty");
    profile.BeginFrame(2, true, 1);
    profile.Presented(1000, true);
    profile.BeginFrame(3, true, 1);
    profile.HiddenCapture();
    profile.BeginFrame(4, false, 1); // Profile disabled, menu, pause, or precache.
    profile.BeginFrame(5, true, 1);
    profile.Presented(50000, true);
    profile.BeginFrame(6, true, 1);
    profile.Presented(50010, true);
    auto window = profile.FinishWindow();
    check(window.all.count == 1 && window.regular.p50 == 10 && window.afterCapture.count == 0,
        "disabled time and pending captures do not contaminate a resumed window");
    profile.BeginFrame(7, true, 2); // PiP active/mode/interval changed.
    profile.Presented(90000, true);
    check(profile.FinishWindow().all.count == 0, "mode changes discard the old timing baseline");
    profile.BeginFrame(9, true, 2); // A frame never reached renderer Begin/End.
    profile.Presented(95000, true);
    check(profile.FinishWindow().all.count == 0, "missing frames exclude inactive-window stalls");
    profile.Reset(); // Renderer reset or destruction.
    profile.BeginFrame(10, true, 2);
    profile.Presented(99000, true);
    check(profile.FinishWindow().all.count == 0, "device reset removes the previous baseline");
}

void unsuccessful_and_clock_reset()
{
    PresentIntervalProfile profile;
    profile.BeginFrame(1, true, 0);
    profile.Presented(100, true);
    profile.BeginFrame(2, true, 0);
    profile.Presented(110, true);
    profile.BeginFrame(3, true, 0);
    profile.HiddenCapture();
    profile.BeginFrame(4, true, 0);
    check(!profile.Presented(0, false), "occluded/failed Present never completes a window");
    profile.BeginFrame(5, true, 0);
    profile.Presented(5000, true);
    profile.BeginFrame(6, true, 0);
    profile.Presented(5010, true);
    auto window = profile.FinishWindow();
    check(window.all.count == 1 && window.all.p50 == 10 && window.afterCapture.count == 0,
        "occlusion clears old samples, hidden capture, and timing baseline");
    profile.BeginFrame(7, true, 0);
    profile.Presented(1, true);
    check(profile.FinishWindow().all.count == 0, "non-monotonic timestamps are discarded");
}

void frame_wrap_and_multiple_captures()
{
    PresentIntervalProfile profile;
    const auto last = (std::numeric_limits<std::uint32_t>::max)();
    profile.BeginFrame(last - 1, true, 0);
    profile.Presented(100, true);
    profile.BeginFrame(last, true, 0);
    profile.HiddenCapture();
    profile.HiddenCapture();
    profile.BeginFrame(0, true, 0);
    profile.Presented(120, true);
    auto window = profile.FinishWindow();
    check(window.all.count == 1 && window.afterCapture.count == 1 && window.afterCapture.p50 == 20,
        "frame-number wrap and repeated capture marking preserve one presented interval");
    profile.BeginFrame(1, true, 0);
    profile.Presented(121, true);
    profile.BeginFrame(2, true, 0);
    profile.Presented(140, true);
    window = profile.FinishWindow();
    check(window.all.p50 == 1 && window.all.p95 == 19 && window.all.maximum == 19,
        "small windows use nearest-rank percentiles");
}
}

int main()
{
    capture_pacing();
    disabled_and_context_resets();
    unsuccessful_and_clock_reset();
    frame_wrap_and_multiple_captures();
    std::printf("PASS: %u Present interval profile checks\n", checks);
    return 0;
}
