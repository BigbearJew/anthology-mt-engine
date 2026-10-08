#include "stdafx.h"
#include "player_hud.h"
#include "../xrEngine/ObjectAnimator.h"

struct AnthologyHudLayer
{
    shared_str name, pivot;
    CObjectAnimator animation;
    Fmatrix transform;
    float amount = 0.f, power = 1.f, elapsed = 0.f;
    u8 part = 2;
    bool active = true, looped = false;
};

void player_hud::play_script_layer(LPCSTR name, u8 part, float speed, float power,
    bool looped, bool no_restart, LPCSTR pivot)
{
    if (!name || !*name || part > 2 || !_valid(speed) || speed <= 0.f || !_valid(power)) return;
    AnthologyHudLayer* layer = nullptr;
    for (auto* candidate : script_layers)
        if (candidate->name == name) { layer = candidate; break; }
    if (!layer)
    {
        string_path path;
        if (!FS.exist(path, "$game_anims$", name) && !FS.exist(path, "$level$", name))
        {
            Msg("! HUD blend animation is missing: %s", name);
            return;
        }
        if (script_layers.size() >= 32)
        {
            Msg("! HUD blend animation limit reached: %s", name);
            return;
        }
        layer = new AnthologyHudLayer();
        layer->name = name;
        layer->animation.Load(name);
        layer->transform.identity();
        script_layers.push_back(layer);
    }
    if (!no_restart || !layer->animation.IsPlaying() || layer->looped != looped)
    {
        layer->animation.Play(looped);
        layer->elapsed = 0.f;
        layer->amount = 0.f;
    }
    layer->animation.Speed() = speed;
    layer->power = power;
    layer->part = part;
    layer->looped = looped;
    layer->pivot = pivot;
    layer->active = true;
}

void player_hud::stop_script_layer(LPCSTR name, bool force)
{
    for (auto* layer : script_layers)
        if (!name || layer->name == name)
        {
            layer->active = false;
            if (force) layer->amount = 0.f;
        }
}

float player_hud::set_script_layer_time(LPCSTR name, float seconds)
{
    if (!name || !_valid(seconds) || seconds <= 0.f) return 0.f;
    for (auto* layer : script_layers)
        if (layer->name == name && layer->animation.IsPlaying())
        {
            const float remaining = _max(0.f, layer->animation.GetLength() - layer->elapsed);
            return layer->animation.Speed() = remaining / seconds;
        }
    return 0.f;
}

void player_hud::update_script_layers()
{
    for (auto it = script_layers.begin(); it != script_layers.end();)
    {
        auto* layer = *it;
        layer->amount += (layer->active ? 1.f : -1.f) * Device.fTimeDelta / .4f;
        clamp(layer->amount, 0.f, 1.f);
        if (!layer->active && layer->amount == 0.f)
        {
            delete layer;
            it = script_layers.erase(it);
            continue;
        }
        if (layer->animation.IsPlaying())
        {
            layer->animation.Update(Device.fTimeDelta);
            layer->elapsed += Device.fTimeDelta * layer->animation.Speed();
            layer->transform = layer->animation.XFORM();
            if (!layer->looped && layer->elapsed >= layer->animation.GetLength()) layer->active = false;
            if (layer->looped && layer->animation.GetLength() > EPS)
                layer->elapsed = fmodf(layer->elapsed, layer->animation.GetLength());
        }
        const float t = layer->amount;
        const float eased = t * t * t * (10.f + t * (-15.f + 6.f * t));
        Fquaternion identity, rotation, blended;
        identity.identity();
        rotation.set(layer->transform);
        Fvector axis; float angle;
        if (rotation.get_axis_angle(axis, angle)) rotation.rotation(axis, angle * layer->power);
        blended.slerp(identity, rotation, eased);
        Fvector translation = layer->transform.c;
        translation.mul(layer->power * eased);
        Fmatrix transform;
        transform.mk_xform(blended, translation);
        // IX-Ray shares a skeleton but keeps separate left/right attachment transforms.
        if (layer->pivot.size() && m_model)
        {
            auto* k = m_model->dcast_PKinematics();
            const u16 bone = k->LL_BoneID(layer->pivot);
            if (bone != u16(-1))
            {
                const Fmatrix base = k->LL_GetTransform(bone);
                Fmatrix inverse; inverse.invert(base);
                transform.mulA_43(base);
                transform.mulB_43(inverse);
            }
        }
        if (layer->part == 0 || layer->part == 2) m_transform.mulB_43(transform);
        if (layer->part == 1 || layer->part == 2) m_transformL.mulB_43(transform);
        ++it;
    }
}

void player_hud::clear_script_layers()
{
    for (auto* layer : script_layers) delete layer;
    script_layers.clear();
}
