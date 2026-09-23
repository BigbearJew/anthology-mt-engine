#pragma once

#include "../../xrEngine/RenderMotionDrawState.h"
#include "../../xrEngine/irenderable.h"

// Used only by the render thread, along with RCache. No allocations, locks or
// clock reads are added to packet/child draws.
inline RenderMotionDrawState& CurrentPipMotionDraw()
{
    static RenderMotionDrawState state;
    return state;
}

class PipMotionHistoryScope
{
    struct ConstantSink
    {
        void operator()(const RenderMotionDrawState& state) const
        {
            RCache.set_c("pip_motion_history", state.unknown, state.owner, 0.f, 0.f);
        }
    } sink;
    RenderMotionDrawScope<ConstantSink> scope;

    static RenderMotionDrawState Packet(IRenderable* owner, bool staticWorld)
    {
        const bool active = ps_scope_lense_temporal_mode != 0 && Device.m_SecondViewport.IsSVPActive();
        return RenderMotionDrawState::Packet(active, !Device.m_SecondViewport.IsSVPFrame(),
            staticWorld, owner ? owner->GetRenderSurfaceOwnerId() : 0.f);
    }

public:
    PipMotionHistoryScope(IRenderable* owner, bool staticWorld)
        : scope(CurrentPipMotionDraw(), Packet(owner, staticWorld), sink) {}

    explicit PipMotionHistoryScope(bool reliableHistory)
        : scope(CurrentPipMotionDraw(), CurrentPipMotionDraw().WithHistory(reliableHistory), sink, false) {}
};
