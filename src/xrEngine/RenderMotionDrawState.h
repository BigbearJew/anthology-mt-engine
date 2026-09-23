#pragma once

struct RenderMotionDrawState
{
    float unknown = 0.f;
    float owner = 0.f;
    bool active = false;
    bool mainView = false;

    static RenderMotionDrawState Packet(bool enabled, bool isMainView, bool staticWorld, float ownerId)
    {
        RenderMotionDrawState state;
        state.active = enabled;
        state.mainView = isMainView;
        if (enabled)
        {
            state.unknown = isMainView && !staticWorld ? 2.f : 0.f;
            // Missing dynamic owners must never masquerade as the static world.
            state.owner = staticWorld ? 0.f : (ownerId < 0.f ? ownerId : 2.f);
        }
        return state;
    }

    RenderMotionDrawState WithHistory(bool reliable) const
    {
        auto state = *this;
        if (active && mainView)
            state.unknown = reliable ? 0.f : 2.f;
        return state;
    }

    bool SameConstant(const RenderMotionDrawState& rhs) const
    {
        return unknown == rhs.unknown && owner == rhs.owner;
    }
};

// Each packet forces its constant after binding the shader table. A nested
// skeletal draw changes only validity and restores its caller for raw siblings.
template<class Sink>
class RenderMotionDrawScope
{
    RenderMotionDrawState& current;
    const RenderMotionDrawState previous;
    Sink& sink;

public:
    RenderMotionDrawScope(RenderMotionDrawState& drawState, const RenderMotionDrawState& next,
        Sink& constantSink, bool force = true)
        : current(drawState), previous(drawState), sink(constantSink)
    {
        current = next;
        if (force || !current.SameConstant(previous))
            sink(current);
    }

    ~RenderMotionDrawScope()
    {
        const bool changed = !current.SameConstant(previous);
        current = previous;
        if (changed)
            sink(current);
    }

    RenderMotionDrawScope(const RenderMotionDrawScope&) = delete;
    RenderMotionDrawScope& operator=(const RenderMotionDrawScope&) = delete;
};
