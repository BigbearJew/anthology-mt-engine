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
            static shared_str name("pip_motion_history");
            RCache.set_c(name, state.unknown, state.owner, 0.f, 0.f);
        }
    } sink;
    RenderMotionDrawScope<ConstantSink> scope;

    static RenderMotionDrawState Packet(IRenderable* owner, bool staticWorld)
    {
        const bool active = ps_scope_lense_temporal_mode != 0 && Device.m_SecondViewport.IsSVPActive();
        return RenderMotionDrawState::Packet(active, !Device.m_SecondViewport.IsSVPFrame(),
            staticWorld, owner ? owner->GetRenderSurfaceOwnerId() : 0.f);
    }

    explicit PipMotionHistoryScope(const RenderMotionDrawState& packet)
        // Binding a shader table already initializes the inactive constant to
        // zero. Do not dirty/upload its buffer again for every ordinary draw.
        : scope(CurrentPipMotionDraw(), packet, sink, packet.active || LegacyUploads()) {}

    static bool LegacyUploads()
    {
        static const bool legacy = strstr(Core.Params, "-cb_legacy_uploads") != nullptr;
        return legacy;
    }

public:
    PipMotionHistoryScope(IRenderable* owner, bool staticWorld)
        : PipMotionHistoryScope(Packet(owner, staticWorld)) {}

    explicit PipMotionHistoryScope(bool reliableHistory)
        : scope(CurrentPipMotionDraw(), CurrentPipMotionDraw().WithHistory(reliableHistory), sink, false) {}
};
