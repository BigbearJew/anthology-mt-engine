#include "../../src/xrEngine/RenderHistoryEpoch.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
unsigned checks = 0;
void check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct PoseHistory
{
    RenderHistoryEpoch epoch;
    float previous = -999.f;
    float saved = -999.f;
    void main(std::uint64_t serial, float current)
    {
        const auto step = epoch.Begin(serial);
        if (step == RenderHistoryEpoch::Step::Same) return;
        previous = step == RenderHistoryEpoch::Step::Advance ? saved : current;
        saved = current;
    }
};
}

int main()
{
    using Step = RenderHistoryEpoch::Step;
    PoseHistory pose;
    check(!pose.epoch.Initialized() && !pose.epoch.PreviousValid(), "new history has no pose");
    pose.main(1, 10.f);
    check(pose.previous == 10.f && pose.saved == 10.f, "first draw initializes both matrices from current pose");
    check(!pose.epoch.PreviousValid(), "first pose is not a correspondence to an earlier main");
    pose.main(1, 11.f);
    check(pose.previous == 10.f && pose.saved == 10.f, "shadow and child draws do not advance history twice");
    // A hidden lens sees pose 15, but neither calls Begin nor writes saved pose.
    const float capturedPose = 15.f;
    check(pose.saved != capturedPose, "hidden lens pose does not replace previous main pose");
    pose.main(2, 20.f);
    check(pose.previous == 10.f && pose.saved == 20.f, "next main spans the hidden capture");
    check(pose.epoch.PreviousValid(), "adjacent main samples have valid history");
    pose.main(2, 21.f);
    check(pose.previous == 10.f && pose.epoch.PreviousValid(), "repeated draws preserve previous-pose validity");
    pose.main(4, 40.f);
    check(pose.previous == 40.f && !pose.epoch.PreviousValid(), "visibility gap cannot reuse stale bone transforms");
    pose.main(5, 50.f);
    check(pose.previous == 40.f && pose.epoch.PreviousValid(), "history recovers after one complete main sample");
    pose.epoch.Reset();
    pose.main(6, 600.f);
    check(pose.previous == 600.f && !pose.epoch.PreviousValid(), "pooled visual respawn never inherits earlier owner pose");
    check(pose.epoch.Begin(0) == Step::Reset && !pose.epoch.Initialized(), "serial zero never claims initialized history");
    check(pose.epoch.Begin(0) == Step::Reset, "repeated untracked renders remain safe resets");
    check(pose.epoch.Begin(10) == Step::Reset, "first tracked epoch after untracked draw resets");
    check(pose.epoch.Begin(9) == Step::Reset, "counter rollback invalidates history");
    check(pose.epoch.Begin(10) == Step::Advance, "history can recover after rollback");
    const auto last = std::numeric_limits<std::uint64_t>::max();
    check(pose.epoch.Begin(last) == Step::Reset, "large serial jump is a visibility gap");
    check(pose.epoch.Begin(0) == Step::Reset && !pose.epoch.PreviousValid(), "serial overflow cannot create false adjacency");
    std::printf("PASS: %u render-history epoch and pose checks\n", checks);
}
