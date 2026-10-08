#include "stdafx.h"
#include "pch_script.h"
#include "player_hud.h"
#include "Actor.h"
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

void RegisterAnomalyScriptHud(lua_State* L)
{
    using namespace luabind;
    module(L, "level")[def("actor_moving_state", &ActorMovingState)];
    module(L, "game")
    [
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
