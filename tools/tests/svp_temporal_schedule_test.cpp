#include "../../src/xrEngine/SvpTemporalSchedule.h"

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

void inactive_and_activation()
{
    SvpTemporalSchedule schedule;
    check(!schedule.Select(10, 100, false, false, 4, false), "inactive viewport is a main frame");
    check(!schedule.Select(10, 100, true, false, 4, true), "activation cannot change an established main camera");
    check(schedule.Select(11, 110, true, false, 4, false), "activation requests its first texture on the next frame");
    schedule.Captured(11, 110);
    check(!schedule.Select(12, 120, true, true, 4, false), "first capture is followed by a main frame");
}

void cached_decision_lookup()
{
    SvpTemporalSchedule schedule;
    bool decision = false;
    check(!schedule.Cached(0, decision), "new scheduler has no cached decision, including frame zero");
    check(!schedule.Cached(50, decision), "lookup alone does not choose a camera");
    check(schedule.Select(50, 500, true, false, 4, false), "first capture establishes the cached decision");
    check(schedule.Cached(50, decision) && decision, "cached lookup returns the established capture decision");
    check(!schedule.Cached(51, decision), "capture decision cannot leak into a different frame");
    check(!schedule.Cached(49, decision), "capture decision is not valid for a previous frame");
    schedule.Reset();
    decision = false;
    check(schedule.Cached(50, decision) && decision, "reset preserves the current camera decision for render consumers");
    check(!schedule.Select(51, 510, true, false, 4, true), "next frame after reset preserves the mandatory main");
    decision = true;
    check(schedule.Cached(51, decision) && !decision, "cached lookup distinguishes a main decision from a cache miss");
    check(!schedule.Cached(50, decision), "advancing a frame expires the old cached decision");
    schedule.Reset();
    decision = true;
    check(schedule.Cached(51, decision) && !decision, "reset also preserves an established main decision");
    check(!schedule.Cached(52, decision), "reset does not preselect the next frame");
    check(schedule.Select(52, 520, true, false, 4, false), "capture recovery still runs after cache-only reads");
    decision = false;
    check(schedule.Cached(52, decision) && decision, "recovery publishes the next cached decision");
}

void successful_cadence(unsigned interval, unsigned expected_interval)
{
    SvpTemporalSchedule schedule;
    bool ready = false;
    unsigned captures = 0;
    unsigned mains = 0;
    unsigned mains_since_capture = 0;
    for (unsigned frame = 0; frame < (expected_interval + 1) * 6; ++frame)
    {
        const bool capture = schedule.Select(frame, frame, true, ready, interval, false);
        if (capture)
        {
            if (captures != 0)
                check(mains_since_capture == expected_interval, "interval counts presented main frames between captures");
            schedule.Captured(frame, frame);
            ready = true;
            mains_since_capture = 0;
            ++captures;
        }
        else
        {
            ++mains_since_capture;
            ++mains;
        }
    }
    check(captures == 6, "steady cadence produces the requested number of captures");
    check(mains == expected_interval * 6, "hidden captures do not consume the main-frame interval");
}

void failed_capture_retry()
{
    SvpTemporalSchedule schedule;
    for (unsigned frame = 0; frame < 6; ++frame)
        check(schedule.Select(frame, frame * 10, true, false, 8, false) == (frame % 2 == 0),
            "failed first capture retries after exactly one main frame");
    check(schedule.Select(6, 60, true, false, 8, false), "recovery capture is attempted");
    schedule.Captured(6, 60);
    for (unsigned frame = 7; frame <= 14; ++frame)
        check(!schedule.Select(frame, 60 + (frame - 6) * 2, true, true, 8, false), "successful recovery returns to the configured interval");
    check(schedule.Select(15, 78, true, true, 8, false), "configured cadence resumes after recovery");
}

void cadence_at_frame_rates()
{
    const unsigned rates[] = {30, 36, 60, 90, 110};
    const unsigned intervals[] = {1, 2, 4, 8};
    for (const unsigned rate : rates)
    {
        for (const unsigned interval : intervals)
        {
            SvpTemporalSchedule schedule;
            unsigned captures = 0;
            unsigned mains_since_capture = 0;
            bool ready = false;
            bool previous_capture = false;
            const unsigned cycles = 12;
            for (unsigned frame = 0; frame < (interval + 1) * cycles; ++frame)
            {
                const std::uint32_t time = 700 + frame * 1000 / rate;
                const bool capture = schedule.Select(frame, time, true, ready, interval, false);
                check(!(capture && previous_capture), "frame-rate cadence always presents between hidden captures");
                if (capture)
                {
                    if (captures != 0 && mains_since_capture != interval)
                    {
                        std::fprintf(stderr, "Cadence at %u FPS, interval %u: only %u main frames\n",
                            rate, interval, mains_since_capture);
                        check(false, "elapsed time cannot override the selected main-frame interval");
                    }
                    schedule.Captured(frame, time);
                    ready = true;
                    mains_since_capture = 0;
                    ++captures;
                }
                else
                    ++mains_since_capture;
                previous_capture = capture;
            }
            check(captures == cycles, "frame-rate cadence retains the selected capture count");
            check(mains_since_capture == interval, "last capture is followed by the full main-frame interval");
            std::printf("Cadence: %u FPS, interval %u: %u captures / %u main frames\n",
                rate, interval, captures, interval * cycles);
        }
    }
}

void publication_and_same_frame_stability()
{
    SvpTemporalSchedule schedule;
    check(schedule.Select(20, 200, true, false, 8, false), "missing texture requests capture");
    schedule.Captured(20, 200);
    check(schedule.Select(20, 201, true, true, 8, false), "publishing a texture does not turn its capture into a main frame");
    schedule.Reset();
    check(schedule.Select(20, 202, false, false, 1, true), "mid-frame reset preserves capture camera identity");
    check(!schedule.Select(21, 300, true, false, 8, true), "failed or invalidated capture still gets one following main frame");
    check(!schedule.Select(21, 500, true, false, 1, true), "elapsed time and settings cannot flip a cached main decision");
    check(schedule.Select(22, 501, true, false, 8, false), "invalidated texture retries after the mandatory main");
}

void invalidate_scope_and_reactivate()
{
    SvpTemporalSchedule schedule;
    check(schedule.Select(30, 300, true, false, 8, false), "initial scope captures");
    schedule.Captured(30, 300);
    check(!schedule.Select(31, 310, true, true, 8, false), "scope capture presents on the next main frame");
    schedule.Reset();
    check(!schedule.Select(31, 311, true, false, 8, false), "scope change does not publish a different camera mid-frame");
    check(schedule.Select(32, 320, true, true, 8, false), "reset rejects old capture metadata even if readiness was stale");
    schedule.Captured(32, 320);
    schedule.Reset();
    check(!schedule.Select(33, 330, false, false, 8, false), "deactivation returns to the main view");
    check(!schedule.Select(34, 340, false, false, 8, false), "inactive viewport remains on the main view");
    check(schedule.Select(35, 350, true, false, 8, false), "reactivation cannot reuse a previous owner's texture");
}

void camera_motion_and_stalls()
{
    SvpTemporalSchedule schedule;
    check(schedule.Select(40, 400, true, false, 8, false), "motion test captures initial view");
    schedule.Captured(40, 400);
    check(!schedule.Select(41, 410, true, true, 8, true), "camera motion cannot cause consecutive hidden frames");
    check(schedule.Select(42, 420, true, true, 8, true), "motion refreshes before the normal cadence");
    schedule.Captured(42, 420);
    check(!schedule.Select(43, 440, true, true, 8, false), "fresh view gets a main frame");
    check(!schedule.Select(44, 452, true, true, 8, false), "stationary view is reused for the selected interval");
    check(!schedule.Select(45, 453, true, true, 8, false), "33 ms elapsed time does not silently override the interval");
    check(!schedule.Select(46, 900, true, true, 8, false), "long stall preserves the explicit main-frame cadence");
    check(!schedule.Select(47, 910, true, true, 8, false), "recovering frame time does not introduce a hidden capture");
    for (unsigned frame = 48; frame <= 50; ++frame)
        check(!schedule.Select(frame, 920 + (frame - 48) * 10, true, true, 8, false), "remaining main frames complete after the stall");
    check(schedule.Select(51, 950, true, true, 8, false), "stalled sequence refreshes after its eighth main frame");
    schedule.Captured(51, 950);
    check(!schedule.Select(52, 2000, true, true, 8, true), "stall and camera invalidation cannot discard the required main frame");
    check(schedule.Select(53, 2010, true, true, 8, true), "camera invalidation still refreshes after the required main");
}

void unsigned_clock_rollover()
{
    const auto maximum = std::numeric_limits<std::uint32_t>::max();
    SvpTemporalSchedule schedule;
    check(schedule.Select(maximum - 1, maximum - 19, true, false, 8, false), "capture near unsigned frame and clock rollover");
    schedule.Captured(maximum - 1, maximum - 19);
    check(!schedule.Select(maximum, maximum - 9, true, true, 8, false), "mandatory main before rollover");
    check(!schedule.Select(0, 0, true, true, 8, false), "frame rollover preserves the reuse interval");
    check(!schedule.Select(1, 12, true, true, 8, false), "clock rollover does not alter the requested frame interval");
    check(!schedule.Select(2, 13, true, true, 8, false), "clock rollover cannot restore the removed 33 ms override");
    for (unsigned frame = 3; frame <= 6; ++frame)
        check(!schedule.Select(frame, 13 + (frame - 2) * 30, true, true, 8, false), "main-frame interval remains valid across frame rollover");
    check(schedule.Select(7, 163, true, true, 8, false), "capture resumes after eight main frames across rollover");

    SvpTemporalSchedule adjacent;
    check(adjacent.Select(maximum, maximum, true, false, 1, false), "capture on the final unsigned frame");
    check(!adjacent.Select(0, 0, true, false, 1, true), "failed capture receives its main frame across rollover");
    check(adjacent.Select(1, 1, true, false, 1, false), "failed capture retries across rollover");
}

void native_cadence_and_mode_switches()
{
    SvpTemporalSchedule native;
    bool ready = false;
    for (unsigned frame = 10; frame < 20; ++frame)
    {
        const bool capture = native.Select(frame, frame * 10, true, ready, 8, true, 2);
        check(capture == (frame % 2 == 0), "native mode retains its authored even-frame cadence");
        if (capture)
        {
            native.Captured(frame, frame * 10);
            ready = true;
        }
    }

    SvpTemporalSchedule reuse_to_native;
    check(reuse_to_native.Select(11, 110, true, false, 8, false), "reuse mode can first capture on an odd internal frame");
    reuse_to_native.Captured(11, 110);
    check(reuse_to_native.Select(11, 111, true, true, 8, false, 2), "native toggle cannot replace an established capture camera");
    check(!reuse_to_native.Select(12, 120, true, true, 8, false, 2), "native parity cannot discard the main after a reuse capture");
    check(!reuse_to_native.Select(13, 130, true, true, 8, true, 2), "native mode resumes its authored parity after transition");
    check(reuse_to_native.Select(14, 140, true, true, 8, false, 2), "native capture resumes after safe transition");

    SvpTemporalSchedule native_to_reuse;
    check(native_to_reuse.Select(20, 200, true, false, 8, false, 2), "native mode starts a real capture");
    native_to_reuse.Captured(20, 200);
    check(native_to_reuse.Select(20, 201, true, true, 8, false), "reuse toggle preserves native capture identity within the frame");
    check(!native_to_reuse.Select(21, 210, true, true, 8, true), "reuse camera invalidation cannot follow a native capture immediately");
    check(native_to_reuse.Select(22, 220, true, true, 8, true), "reuse camera invalidation executes after one main frame");

    SvpTemporalSchedule mid_main;
    check(!mid_main.Select(31, 310, true, false, 8, false, 2), "native odd frame remains main even without a texture");
    check(!mid_main.Select(31, 311, true, false, 8, true), "reuse toggle cannot replace an established main camera");
    check(mid_main.Select(32, 320, true, false, 8, false), "reuse retries its first texture in the following frame");
}

void adverse_lifecycle_sequence()
{
    SvpTemporalSchedule schedule;
    bool ready = false;
    bool previous_capture = false;
    unsigned captures = 0;
    unsigned mains = 0;
    std::uint32_t random = 0x25c8d07u;
    std::uint32_t time = 0;
    for (std::uint32_t frame = 0; frame < 20000; ++frame)
    {
        random = random * 1664525u + 1013904223u;
        time += 1 + (random % 31);
        const bool active = (random & 3) != 0;
        if ((random & 15) == 4)
        {
            schedule.Reset();
            ready = false;
        }
        const unsigned legacy_delay = (random & 64) != 0 ? 2 : 0;
        const bool capture = schedule.Select(frame, time, active, ready, 1 + (random % 8), (random & 16) != 0, legacy_delay);
        check(!(previous_capture && capture), "adverse lifecycle never schedules consecutive hidden captures");
        check(active || !capture, "inactive frame never starts a capture");
        check(schedule.Select(frame, time + 500, !active, !ready, 99, true, legacy_delay == 0 ? 2 : 0) == capture,
            "all consumers agree on camera identity despite same-frame input changes");
        if (capture)
        {
            ++captures;
            if ((random & 32) != 0)
            {
                schedule.Captured(frame, time);
                ready = true;
            }
        }
        else
            ++mains;
        previous_capture = capture;
    }
    check(captures > 100, "adverse lifecycle exercised successful and failed capture attempts");
    check(mains >= captures, "main view receives at least half the decisions under continuous invalidation");
}
}

int main()
{
    cadence_at_frame_rates();
    inactive_and_activation();
    cached_decision_lookup();
    successful_cadence(1, 1);
    successful_cadence(4, 4);
    successful_cadence(8, 8);
    successful_cadence(0, 1);
    successful_cadence(100, 8);
    failed_capture_retry();
    publication_and_same_frame_stability();
    invalidate_scope_and_reactivate();
    camera_motion_and_stalls();
    unsigned_clock_rollover();
    native_cadence_and_mode_switches();
    adverse_lifecycle_sequence();
    std::printf("PASS: SvpTemporalSchedule (%u checks)\n", checks);
    return 0;
}
