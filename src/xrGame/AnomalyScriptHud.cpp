#include "stdafx.h"
#include "pch_script.h"
#include "player_hud.h"
#include "Actor.h"
#include "Weapon.h"
#include "Inventory.h"
#include "CharacterPhysicsSupport.h"
#include "../xrPhysics/IElevatorState.h"

namespace
{
u32 PlayHudMotion(u8 hand, LPCSTR section, LPCSTR motion, bool mix, float speed)
{
    return g_player_hud ? g_player_hud->script_anim_play(hand, section, motion, mix, speed) : 0;
}
void StopHudMotion() { if (g_player_hud) g_player_hud->stop_script_anim(); }
bool AllowHudMotion() { return g_player_hud && g_player_hud->allow_script_anim(); }
u32 HudMotionLength(LPCSTR section, LPCSTR motion, float speed)
{
    return g_player_hud ? g_player_hud->script_motion_length(section, motion, speed) : 0;
}
}

bool AnomalyOnlyMovementKeys() { return g_player_hud && g_player_hud->only_movement_keys; }
static void OnlyMovementKeys(bool value) { if (g_player_hud) g_player_hud->only_movement_keys = value; }
static u32 ActorMovingState()
{
    auto* actor = Actor();
    return actor ? actor->GetMovementState(ACTOR_DEFS::eReal) : 0;
}
static void AllowActorLadder(bool allow)
{
    auto* actor = Actor();
    if (!actor || !actor->character_physics_support()) return;
    auto* movement = actor->character_physics_support()->movement();
    if (!movement) return;
    if (auto* ladder = movement->ElevatorState()) ladder->AllowClimbing(allow);
}
static void PlayLayer(LPCSTR name, u8 part, float speed, float power, bool looped, bool no_restart, LPCSTR pivot)
{
    if (g_player_hud) g_player_hud->play_script_layer(name, part, speed, power, looped, no_restart, pivot);
}
static void PlayLayer6(LPCSTR name, u8 part, float speed, float power, bool looped, bool no_restart)
{ PlayLayer(name, part, speed, power, looped, no_restart, nullptr); }
static void PlayLayer5(LPCSTR name, u8 part, float speed, float power, bool looped)
{ PlayLayer6(name, part, speed, power, looped, false); }
static void StopLayer(LPCSTR name, bool force) { if (g_player_hud) g_player_hud->stop_script_layer(name, force); }
static void StopLayerDefault(LPCSTR name) { StopLayer(name, false); }
static void StopLayers(bool force) { StopLayer(nullptr, force); }
static void StopLayersDefault() { StopLayers(false); }
static float SetLayerTime(LPCSTR name, float seconds)
{ return g_player_hud ? g_player_hud->set_script_layer_time(name, seconds) : 0.f; }

static bool ActorWeaponLowered() { return Actor() && Actor()->is_safemode(); }
static void ActorLowerWeapon(bool lower)
{
    auto* actor = Actor();
    if (!actor) return;
    auto* item = actor->inventory().ActiveItem();
    if (auto* weapon = item ? item->cast_weapon() : nullptr) weapon->SetLowered(lower);
    else if (!lower) actor->set_safemode(false);
}
static void HudAdjustEnabled(bool enabled) { if (g_player_hud) g_player_hud->script_adjust_enabled = enabled; }
static void HudAdjustVector(int component, int index, float x, float y, float z)
{
    if (!g_player_hud || component < 0 || component >= 2 || index < 0 || index >= 21 || !_valid(x) || !_valid(y) || !_valid(z)) return;
    g_player_hud->script_adjust_offsets[component][index].set(x, y, z);
    g_player_hud->script_adjust_valid[component][index] = true;
}
static void HudAdjustValue(LPCSTR name, float value)
{
    if (!g_player_hud || !name || !_valid(value)) return;
    if (!xr_strcmp(name, "scope_zoom_factor")) g_player_hud->script_adjust_zoom[0] = value;
    else if (!xr_strcmp(name, "gl_zoom_factor")) g_player_hud->script_adjust_zoom[1] = value;
    else if (!xr_strcmp(name, "scope_zoom_factor_alt")) g_player_hud->script_adjust_zoom[2] = value;
}

void RegisterAnomalyScriptHud(lua_State* L)
{
    using namespace luabind;
    module(L, "hud_adjust")[def("enabled", &HudAdjustEnabled), def("set_vector", &HudAdjustVector), def("set_value", &HudAdjustValue)];
    module(L, "level")[def("actor_moving_state", &ActorMovingState)];
    module(L, "game")
    [
        def("actor_weapon_lowered", &ActorWeaponLowered), def("actor_lower_weapon", &ActorLowerWeapon),
        def("play_hud_motion", &PlayHudMotion),
        def("stop_hud_motion", &StopHudMotion),
        def("hud_motion_allowed", &AllowHudMotion),
        def("get_motion_length", &HudMotionLength),
        def("play_hud_anm", &PlayLayer), def("play_hud_anm", &PlayLayer6), def("play_hud_anm", &PlayLayer5),
        def("stop_hud_anm", &StopLayer), def("stop_hud_anm", &StopLayerDefault),
        def("stop_all_hud_anms", &StopLayers), def("stop_all_hud_anms", &StopLayersDefault),
        def("set_hud_anm_time", &SetLayerTime),
        def("set_actor_allow_ladder", &AllowActorLadder),
        def("only_allow_movekeys", &OnlyMovementKeys), def("only_movekeys_allowed", &AnomalyOnlyMovementKeys)
    ];
}
