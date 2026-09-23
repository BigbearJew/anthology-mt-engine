#include "../../src/xrEngine/RenderSurfaceOwner.h"
#include "../../src/xrEngine/RenderMotionDrawState.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

static unsigned checks = 0;
static void Check(bool value)
{
    ++checks;
    if (!value)
    {
        std::cerr << "Failed check " << checks << '\n';
        std::exit(1);
    }
}

struct Sink
{
    std::vector<RenderMotionDrawState> writes;
    void operator()(const RenderMotionDrawState& state) { writes.push_back(state); }
};

int main()
{
    RenderSurfaceOwnerPool<3> small;
    const auto a = small.Acquire();
    const auto b = small.Acquire();
    Check(a != b && small.Generation() == 1);
    small.Release(a);
    const auto c = small.Acquire();
    Check(c != a && c != b && small.Generation() == 1); // Prefer an unused slot.
    const auto recycled = small.Acquire();
    Check(recycled == a && small.Generation() == 2);
    Check(small.Acquire() == RenderSurfaceOwnerPool<3>::Invalid);

    RenderSurfaceOwnerPool<2> quarantine;
    const auto q0 = quarantine.Acquire();
    const auto q1 = quarantine.Acquire();
    quarantine.SetMainSerial(9);
    quarantine.Release(q0);
    quarantine.SetMainSerial(10);
    quarantine.Release(q1);
    quarantine.SetHistoryBoundary(10);
    Check(quarantine.Acquire() == q0 && quarantine.Generation() == 1); // Oldest, outside retained history.
    Check(quarantine.Acquire() == q1 && quarantine.Generation() == 2); // Boundary endpoint is still retained.
    for (unsigned i = 0; i < 32; ++i)
    {
        quarantine.SetMainSerial(11 + i);
        quarantine.Release(q1);
        quarantine.Release(q0);
        quarantine.SetHistoryBoundary(12 + i);
        quarantine.SetHistoryBoundary(1); // Boundary must never go backwards.
        Check(quarantine.Acquire() == q1 && quarantine.Acquire() == q0);
        Check(quarantine.Generation() == 2); // FIFO ring wrap preserves stable long-session history.
        Check(quarantine.Acquire() == RenderSurfaceOwnerPool<2>::Invalid);
    }
    quarantine.SetMainSerial(1); // Current main serial is monotonic too.
    quarantine.Release(q0); // Actual release at 42, older than retained boundary43.
    Check(quarantine.Acquire() == q0 && quarantine.Generation() == 2);
    Check(small.Generation() == 2); // Exhaustion cannot invalidate every frame.
    small.Release(RenderSurfaceOwnerPool<3>::Invalid);
    small.Release(b);
    small.Release(b); // A duplicate release must not produce duplicate live IDs.
    Check(small.Acquire() == b && small.Generation() == 3);
    Check(small.Acquire() == RenderSurfaceOwnerPool<3>::Invalid);

    RenderSurfaceOwnerPool<> full;
    std::set<float> owners;
    for (unsigned i = 0; i < 13312; ++i)
    {
        const auto slot = full.Acquire();
        const float owner = EncodeRenderSurfaceOwner(slot);
        Check(slot == i && owners.insert(owner).second);
        Check(std::isfinite(owner) && owner > -0.5f && owner <= -std::ldexp(1.f, -14));
        const unsigned half = 0x400u + i;
        const float expected = -std::ldexp(1.f + (half & 1023u) / 1024.f, int(half >> 10) - 15);
        Check(owner == expected); // Exactly representable in capture R16F.
    }
    Check(full.Acquire() == RenderSurfaceOwnerPool<>::Invalid);
    Check(full.Generation() == 1);

    RenderSurfaceOwnerPool<256> concurrent;
    std::array<std::array<std::uint16_t, 64>, 4> batches{};
    std::vector<std::thread> threads;
    for (auto& batch : batches)
        threads.emplace_back([&concurrent, &batch]() { for (auto& slot : batch) slot = concurrent.Acquire(); });
    for (auto& thread : threads)
        thread.join();
    std::set<std::uint16_t> live;
    for (const auto& batch : batches)
        for (auto slot : batch)
            Check(slot != RenderSurfaceOwnerPool<256>::Invalid && live.insert(slot).second);
    Check(concurrent.Generation() == 1);

    RenderMotionDrawState current;
    Sink sink;
    const float owner = EncodeRenderSurfaceOwner(12);
    {
        RenderMotionDrawScope<Sink> packet(current, RenderMotionDrawState::Packet(true, true, false, owner), sink);
        Check(current.unknown == 2.f && current.owner == owner); // Untracked rigid prop.
        {
            RenderMotionDrawScope<Sink> skin(current, current.WithHistory(true), sink, false);
            Check(current.unknown == 0.f && current.owner == owner); // Adjacent hardware skeleton.
        }
        Check(current.unknown == 2.f && current.owner == owner); // Raw sibling cannot inherit validity.
        {
            RenderMotionDrawScope<Sink> skin(current, current.WithHistory(false), sink, false);
            Check(current.unknown == 2.f && current.owner == owner); // First/gap/soft pose.
        }
    }
    Check(current.unknown == 0.f && current.owner == 0.f && sink.writes.size() == 4);
    const auto priorWrites = sink.writes.size();
    {
        RenderMotionDrawScope<Sink> packet(current, RenderMotionDrawState::Packet(true, true, true, 0.f), sink);
        Check(current.unknown == 0.f && current.owner == 0.f);
    }
    Check(sink.writes.size() == priorWrites + 1); // Force packet bind; avoid redundant zero reset.
    auto capture = RenderMotionDrawState::Packet(true, false, false, owner).WithHistory(false);
    Check(capture.unknown == 0.f && capture.owner == owner); // Capture does not need previous pose.
    auto missing = RenderMotionDrawState::Packet(true, true, false, 0.f).WithHistory(true);
    Check(missing.owner == 2.f); // A good skeleton cannot cure a missing identity.
    missing = RenderMotionDrawState::Packet(true, false, false, 0.f);
    Check(missing.unknown == 0.f && missing.owner == 2.f); // Unknown capture cannot equal world0.
    auto disabled = RenderMotionDrawState::Packet(false, true, false, owner).WithHistory(false);
    Check(disabled.unknown == 0.f && disabled.owner == 0.f);

    std::cout << "PASS: " << checks << " surface-owner/epoch/per-draw checks\n";
}
