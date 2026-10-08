////////////////////////////////////////////////////////////////////////////
//	Module 		: script_game_object_script3.cpp
//	Created 	: 25.09.2003
//  Modified 	: 29.06.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script game object script export
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "pch_script.h"
#include "script_game_object.h"
#include "alife_space.h"
#include "script_entity_space.h"
#include "movement_manager_space.h"
#include "pda_space.h"
#include "memory_space.h"
#include "cover_point.h"
#include "script_hit.h"
#include "script_binder_object.h"
#include "script_sound_info.h"
#include "script_monster_hit_info.h"
#include "script_entity_action.h"
#include "action_planner.h"
#include "physics_shell_scripted.h"
#include "helicopter.h"
#include "HangingLamp.h"
#include "CustomZone.h"
#include "holder_custom.h"
#include "script_zone.h"
#include "relation_registry.h"
#include "GameTask.h"
#include "Car.h"
#include "ZoneCampfire.h"
#include "PhysicObject.h"
#include "Artefact.h"
#include "sight_manager_space.h"
#include "../xrScripts/exports/script_ini_file.h"

#include "InventoryOwner.h"
#include "Inventory.h"
#include "trade_parameters.h"
#include "Level.h"
#include "WeaponMagazinedWGrenade.h"
#include "HudItem.h"
#include "ActorHelmet.h"
#include "xrMessages.h"
#include "player_hud.h"
#include "ai_space.h"
#include "../xrScripts/script_engine.h"

namespace
{
void move_anomaly_inventory_item(CScriptGameObject* self, CScriptGameObject* object, u16 event, u16 slot = 0)
{
    auto* owner = self ? self->object().cast_inventory_owner() : nullptr;
    auto* item = object ? object->object().cast_inventory_item() : nullptr;
    if (!owner || !item || item->object().H_Parent() != &self->object()) return;
    auto& inventory = owner->inventory();
    if (event == GEG_PLAYER_ITEM2RUCK && !inventory.CanPutInRuck(item)) return;
    if (event == GEG_PLAYER_ITEM2BELT && !inventory.CanPutInBelt(item)) return;
    if (event == GEG_PLAYER_ITEM2SLOT)
    {
        if (slot < inventory.FirstSlot() || slot > inventory.LastSlot()) return;
        if (!owner->CanPutInSlot(item, slot)) return;
        auto* occupied = inventory.ItemFromSlot(slot);
        if (occupied == item) return;
        if (occupied)
        {
            if (!inventory.CanPutInRuck(occupied)) return;
            NET_Packet packet;
            CGameObject::u_EventGen(packet, GEG_PLAYER_ITEM2RUCK, owner->object_id());
            packet.w_u16(occupied->object().ID());
            CGameObject::u_EventSend(packet);
        }
    }
    NET_Packet packet;
    CGameObject::u_EventGen(packet, event, owner->object_id());
    packet.w_u16(item->object().ID());
    if (event == GEG_PLAYER_ITEM2SLOT) packet.w_u16(slot);
    CGameObject::u_EventSend(packet);
}

void set_anomaly_position(CScriptGameObject* self, const Fvector& position)
{
    auto* zone = self ? smart_cast<CCustomZone*>(&self->object()) : nullptr;
    if (zone) zone->MoveScript(position);
}

template <auto Cast>
bool is_anomaly_object_type(CScriptGameObject* self)
{
    return self && (self->object().*Cast)() != nullptr;
}

CWeapon* cast_anomaly_weapon(CScriptGameObject* self)
{
    return self ? self->object().cast_weapon() : nullptr;
}

// Anomaly's fourth argument selects the first-person model, not recursion.
IKinematics* anomaly_bone_model(CScriptGameObject* self, bool hud)
{
    if (!self) return nullptr;
    if (!hud)
        return self->object().Visual() ? PKinematics(self->object().Visual()) : nullptr;
    if (auto* item = self->object().cast_hud_item())
    {
        auto* data = item->HudItemData();
        return data ? data->m_model : nullptr;
    }
    if (self->object().cast_actor() && g_player_hud && g_player_hud->GetModel())
        return g_player_hud->GetModel()->dcast_PKinematics();
    return nullptr;
}
Fvector anomaly_bone_position_index(CScriptGameObject* self, u16 bone)
{
    auto* model = anomaly_bone_model(self, false);
    if (!model) return Fvector().set(0.f, 0.f, 0.f);
    // Anomaly uses BI_NONE to request the root bone.
    if (bone == BI_NONE) bone = model->LL_GetBoneRoot();
    if (bone >= model->LL_BoneCount()) return self->object().Position();
    Fmatrix transform;
    transform.mul_43(self->object().XFORM(), model->LL_GetBoneInstance(bone).mTransform);
    return transform.c;
}
void set_anomaly_bone_visible(CScriptGameObject* self, LPCSTR name,
    bool visible, bool recursive, bool hud)
{
    auto* model = anomaly_bone_model(self, hud);
    if (!model || !name || !*name) return;
    const u16 bone = model->LL_BoneID(name);
    if (bone != BI_NONE && !!model->LL_GetBoneVisible(bone) != visible)
        model->LL_SetBoneVisible(bone, visible, recursive);
}

u32 play_anomaly_item_motion(CScriptGameObject* self, LPCSTR motion,
    bool mix, u32 state, float speed, float end)
{
    auto* item = self ? self->object().cast_hud_item() : nullptr;
    if (!item || !g_player_hud || !motion || !*motion || !item->HudAnimationExist(motion)) return 0;
    return item->PlayHUDMotion(motion, mix ? EHudMixType::eMixAll : EHudMixType::eNoMix, state, speed, end);
}
u32 play_anomaly_item_motion_speed(CScriptGameObject* self, LPCSTR motion,
    bool mix, u32 state, float speed)
{
    return play_anomaly_item_motion(self, motion, mix, state, speed, 0.f);
}

luabind::object anomaly_character_dialogs(CScriptGameObject* self)
{
    auto table = luabind::newtable(ai().script_engine().lua());
    auto* owner = self ? self->object().cast_inventory_owner() : nullptr;
    if (!owner || !owner->CharacterInfo().GetSpecificCharacterId().size()) return table;
    int index = 1;
    for (const auto& dialog : owner->CharacterInfo().ActorDialogs())
        table[index++] = dialog.c_str();
    return table;
}

void set_anomaly_buy_exponent(CScriptGameObject* self, float factor)
{
    auto* owner = self ? self->object().cast_inventory_owner() : nullptr;
    if (owner && _valid(factor)) owner->trade_parameters().buy_item_exponent = factor;
}
void set_anomaly_sell_exponent(CScriptGameObject* self, float factor)
{
    auto* owner = self ? self->object().cast_inventory_owner() : nullptr;
    if (owner && _valid(factor)) owner->trade_parameters().sell_item_exponent = factor;
}

void iterate_anomaly_inventory(CScriptGameObject* self, luabind::functor<bool> callback,
    const luabind::object& context, bool belt)
{
    auto* owner = self ? self->object().cast_inventory_owner() : nullptr;
    if (!owner) return;
    const auto& items = belt ? owner->inventory().m_belt : owner->inventory().m_ruck;
    xr_vector<u16> ids;
    ids.reserve(items.size());
    for (auto* item : items) ids.push_back(item->object().ID());
    // The callback may move or release inventory entries.
    for (u16 id : ids)
    {
        auto* object = Level().Objects.net_Find(id);
        auto* game_object = object ? object->cast_game_object() : nullptr;
        if (game_object && game_object->H_Parent() == &self->object()
            && callback(context, game_object->lua_game_object())) return;
    }
}
void iterate_anomaly_belt(CScriptGameObject* self, luabind::functor<bool> callback, const luabind::object& context)
{
    iterate_anomaly_inventory(self, callback, context, true);
}
void iterate_anomaly_ruck(CScriptGameObject* self, luabind::functor<bool> callback, const luabind::object& context)
{
    iterate_anomaly_inventory(self, callback, context, false);
}
luabind::object empty_inventory_context(const luabind::functor<bool>& callback)
{
    return luabind::object(callback.lua_state(), static_cast<CScriptGameObject*>(nullptr));
}
void iterate_anomaly_belt_plain(CScriptGameObject* self, luabind::functor<bool> callback)
{
    iterate_anomaly_belt(self, callback, empty_inventory_context(callback));
}
void iterate_anomaly_ruck_plain(CScriptGameObject* self, luabind::functor<bool> callback)
{
    iterate_anomaly_ruck(self, callback, empty_inventory_context(callback));
}
void iterate_inventory_plain(CScriptGameObject* self, luabind::functor<bool> callback)
{
    self->IterateInventory(callback, empty_inventory_context(callback));
}
void iterate_inventory_box_plain(CScriptGameObject* self, luabind::functor<bool> callback)
{
    self->IterateInventoryBox(callback, empty_inventory_context(callback));
}
}

using namespace luabind;

class_<CScriptGameObject> script_register_game_object2(class_<CScriptGameObject> &&instance)
{
	return std::move(instance)
		.def("add_sound",					(u32 (CScriptGameObject::*)(LPCSTR,u32,ESoundTypes,u32,u32,u32))(&CScriptGameObject::add_sound))
		.def("add_sound",					(u32 (CScriptGameObject::*)(LPCSTR,u32,ESoundTypes,u32,u32,u32,LPCSTR))(&CScriptGameObject::add_sound))
		.def("add_combat_sound",			(u32 (CScriptGameObject::*)(LPCSTR,u32,ESoundTypes,u32,u32,u32,LPCSTR))(&CScriptGameObject::add_combat_sound))
		.def("remove_sound",				&CScriptGameObject::remove_sound)
		.def("set_sound_mask",				&CScriptGameObject::set_sound_mask)
		.def("play_sound",					(void (CScriptGameObject::*)(u32))(&CScriptGameObject::play_sound))
		.def("play_sound",					(void (CScriptGameObject::*)(u32,u32))(&CScriptGameObject::play_sound))
		.def("play_sound",					(void (CScriptGameObject::*)(u32,u32,u32))(&CScriptGameObject::play_sound))
		.def("play_sound",					(void (CScriptGameObject::*)(u32,u32,u32,u32))(&CScriptGameObject::play_sound))
		.def("play_sound",					(void (CScriptGameObject::*)(u32,u32,u32,u32,u32))(&CScriptGameObject::play_sound))
		.def("play_sound",					(void (CScriptGameObject::*)(u32,u32,u32,u32,u32,u32))(&CScriptGameObject::play_sound))
		.def("binded_object",				&CScriptGameObject::binded_object)
		.def("set_previous_point",			&CScriptGameObject::set_previous_point)
		.def("set_start_point",				&CScriptGameObject::set_start_point)
		.def("get_current_point_index",		&CScriptGameObject::get_current_patrol_point_index)
		.def("path_completed",				&CScriptGameObject::path_completed)
		.def("patrol_path_make_inactual",	&CScriptGameObject::patrol_path_make_inactual)
		.def("enable_memory_object",		&CScriptGameObject::enable_memory_object)
		.def("active_sound_count",			(int (CScriptGameObject::*)())(&CScriptGameObject::active_sound_count))
		.def("active_sound_count",			(int (CScriptGameObject::*)(bool))(&CScriptGameObject::active_sound_count))
		.def("best_cover",					&CScriptGameObject::best_cover)
		.def("safe_cover",					&CScriptGameObject::safe_cover)
		.def("spawn_ini",					&CScriptGameObject::spawn_ini)
		.def("memory_visible_objects",		&CScriptGameObject::memory_visible_objects, return_stl_iterator)
		.def("memory_sound_objects",		&CScriptGameObject::memory_sound_objects, return_stl_iterator)
		.def("memory_hit_objects",			&CScriptGameObject::memory_hit_objects, return_stl_iterator)
		.def("not_yet_visible_objects",		&CScriptGameObject::not_yet_visible_objects, return_stl_iterator)
		.def("visibility_threshold",		&CScriptGameObject::visibility_threshold)
		.def("enable_vision",				&CScriptGameObject::enable_vision)
		.def("vision_enabled",				&CScriptGameObject::vision_enabled)
		.def("set_sound_threshold",			&CScriptGameObject::set_sound_threshold)
		.def("restore_sound_threshold",		&CScriptGameObject::restore_sound_threshold)

		// sight manager
		.def("set_sight",					(void (CScriptGameObject::*)(SightManager::ESightType sight_type, Fvector *vector3d, u32 dwLookOverDelay))(&CScriptGameObject::set_sight))
		.def("set_sight",					(void (CScriptGameObject::*)(SightManager::ESightType sight_type, bool torso_look, bool path))(&CScriptGameObject::set_sight))
		.def("set_sight",					(void (CScriptGameObject::*)(SightManager::ESightType sight_type, Fvector &vector3d, bool torso_look))(&CScriptGameObject::set_sight))
		.def("set_sight",					(void (CScriptGameObject::*)(SightManager::ESightType sight_type, Fvector *vector3d))(&CScriptGameObject::set_sight))
		.def("set_sight",					(void (CScriptGameObject::*)(CScriptGameObject *object_to_look))(&CScriptGameObject::set_sight))
		.def("set_sight",					(void (CScriptGameObject::*)(CScriptGameObject *object_to_look, bool torso_look))(&CScriptGameObject::set_sight))
		.def("set_sight",					(void (CScriptGameObject::*)(CScriptGameObject *object_to_look, bool torso_look, bool fire_object))(&CScriptGameObject::set_sight))
		.def("set_sight",					(void (CScriptGameObject::*)(CScriptGameObject *object_to_look, bool torso_look, bool fire_object, bool no_pitch))(&CScriptGameObject::set_sight))
//		.def("set_sight",					(void (CScriptGameObject::*)(const MemorySpace::CMemoryInfo *memory_object, bool	torso_look))(&CScriptGameObject::set_sight))

		// object handler
		.def("set_item",					(void (CScriptGameObject::*)(MonsterSpace::EObjectAction ))(&CScriptGameObject::set_item))
		.def("set_item",					(void (CScriptGameObject::*)(MonsterSpace::EObjectAction, CScriptGameObject *))(&CScriptGameObject::set_item))
		.def("set_item",					(void (CScriptGameObject::*)(MonsterSpace::EObjectAction, CScriptGameObject *, u32))(&CScriptGameObject::set_item))
		.def("set_item",					(void (CScriptGameObject::*)(MonsterSpace::EObjectAction, CScriptGameObject *, u32, u32))(&CScriptGameObject::set_item))

		.def("bone_position",				&CScriptGameObject::bone_position)
		.def("bone_position", &anomaly_bone_position_index)

		.def("is_body_turning",				&CScriptGameObject::is_body_turning)

		//////////////////////////////////////////////////////////////////////////
		// Space restrictions
		//////////////////////////////////////////////////////////////////////////
		.def("add_restrictions",			&CScriptGameObject::add_restrictions)
		.def("remove_restrictions",			&CScriptGameObject::remove_restrictions)
		.def("remove_all_restrictions",		&CScriptGameObject::remove_all_restrictions)
		.def("in_restrictions",				&CScriptGameObject::in_restrictions)
		.def("out_restrictions",			&CScriptGameObject::out_restrictions)
		.def("base_in_restrictions",		&CScriptGameObject::base_in_restrictions)
		.def("base_out_restrictions",		&CScriptGameObject::base_out_restrictions)
		.def("accessible",					&CScriptGameObject::accessible_position)
		.def("accessible",					&CScriptGameObject::accessible_vertex_id)
		.def("accessible_nearest",			&CScriptGameObject::accessible_nearest, out_value<3>())

		//////////////////////////////////////////////////////////////////////////
		.def("enable_attachable_item",		&CScriptGameObject::enable_attachable_item)
		.def("attachable_item_enabled",		&CScriptGameObject::attachable_item_enabled)
		.def("night_vision_allowed",		&CScriptGameObject::night_vision_allowed)
		.def("enable_night_vision",			&CScriptGameObject::enable_night_vision)
		.def("night_vision_enabled",		&CScriptGameObject::night_vision_enabled)
		.def("enable_torch",				&CScriptGameObject::enable_torch)
		.def("torch_enabled",				&CScriptGameObject::torch_enabled)
		.def("attachable_item_load_attach", &CScriptGameObject::attachable_item_load_attach)
		.def("weapon_strapped",				&CScriptGameObject::weapon_strapped)
		.def("weapon_unstrapped",			&CScriptGameObject::weapon_unstrapped)

		//////////////////////////////////////////////////////////////////////////
		//inventory owner
		//////////////////////////////////////////////////////////////////////////

		.enum_("EPdaMsg")
		[
			value("dialog_pda_msg",			int(ePdaMsgDialog)),
			value("info_pda_msg",			int(ePdaMsgInfo)),
			value("no_pda_msg",				int(ePdaMsgMax))
		]

		.def("give_info_portion",			&CScriptGameObject::GiveInfoPortion)
		.def("disable_info_portion",		&CScriptGameObject::DisableInfoPortion)
		.def("give_game_news",				(void (CScriptGameObject::*)(LPCSTR,LPCSTR,LPCSTR,int,int))(&CScriptGameObject::GiveGameNews))
		.def("give_game_news",				(void (CScriptGameObject::*)(LPCSTR,LPCSTR,LPCSTR,int,int,int))(&CScriptGameObject::GiveGameNews))

		.def("give_talk_message",			(void (CScriptGameObject::*)(LPCSTR,LPCSTR,LPCSTR))(&CScriptGameObject::AddIconedTalkMessage_old))//old version, must remove!
		.def("give_talk_message2",			(void (CScriptGameObject::*)(LPCSTR,LPCSTR,LPCSTR,LPCSTR))(&CScriptGameObject::AddIconedTalkMessage))

		.def("has_info",					&CScriptGameObject::HasInfo)
		.def("dont_has_info",				&CScriptGameObject::DontHasInfo)

		.def("get_task_state",				&CScriptGameObject::GetGameTaskState)
		.def("set_task_state",				&CScriptGameObject::SetGameTaskState)
		.def("give_task",					&CScriptGameObject::GiveTaskToActor,		adopt<2>())
		.def("set_active_task",				&CScriptGameObject::SetActiveTask)
		.def("is_active_task",				&CScriptGameObject::IsActiveTask)
		.def("get_task",					&CScriptGameObject::GetTask)

		.def("is_talking",					&CScriptGameObject::IsTalking)
		.def("stop_talk",					&CScriptGameObject::StopTalk)
		.def("enable_talk",					&CScriptGameObject::EnableTalk)
		.def("disable_talk",				&CScriptGameObject::DisableTalk)
		.def("is_talk_enabled",				&CScriptGameObject::IsTalkEnabled)

		.def("enable_trade",				&CScriptGameObject::EnableTrade)
		.def("disable_trade",				&CScriptGameObject::DisableTrade)
		.def("is_trade_enabled",			&CScriptGameObject::IsTradeEnabled)
		.def("enable_inv_upgrade",			&CScriptGameObject::EnableInvUpgrade)
		.def("disable_inv_upgrade",			&CScriptGameObject::DisableInvUpgrade)
		.def("is_inv_upgrade_enabled",		&CScriptGameObject::IsInvUpgradeEnabled)

		.def("disable_show_hide_sounds",	&CScriptGameObject::SetPlayShHdRldSounds)
		.def("inventory_for_each",			&CScriptGameObject::ForEachInventoryItems)
		.def("drop_item",					&CScriptGameObject::DropItem)
		.def("drop_item_and_teleport",		&CScriptGameObject::DropItemAndTeleport)
		.def("transfer_item",				&CScriptGameObject::TransferItem)
		.def("move_to_ruck", +[](CScriptGameObject* self, CScriptGameObject* item) { move_anomaly_inventory_item(self, item, GEG_PLAYER_ITEM2RUCK); })
		.def("move_to_belt", +[](CScriptGameObject* self, CScriptGameObject* item) { move_anomaly_inventory_item(self, item, GEG_PLAYER_ITEM2BELT); })
		.def("move_to_slot", +[](CScriptGameObject* self, CScriptGameObject* item, u16 slot) { move_anomaly_inventory_item(self, item, GEG_PLAYER_ITEM2SLOT, slot); })
		.def("force_unload_magazine", +[](CScriptGameObject* self, bool keepAmmo) {
            auto* weapon = self ? smart_cast<CWeaponMagazined*>(&self->object()) : nullptr;
            if (weapon) weapon->UnloadMagazine(keepAmmo);
        })
		.def("transfer_money",				&CScriptGameObject::TransferMoney)
		.def("give_money",					&CScriptGameObject::GiveMoney)
		.def("money",						&CScriptGameObject::Money)
		.def("make_item_active",			&CScriptGameObject::MakeItemActive)

		.def("switch_to_trade",				&CScriptGameObject::SwitchToTrade)
		.def("switch_to_upgrade",			&CScriptGameObject::SwitchToUpgrade)
		.def("switch_to_talk",				&CScriptGameObject::SwitchToTalk)
		.def("run_talk_dialog",				&CScriptGameObject::RunTalkDialog)
		.def("allow_break_talk_dialog",		&CScriptGameObject::AllowBreakTalkDialog)

		.def("set_pda_disabled",			&CScriptGameObject::SetPdaDisabled)
		.def("is_pda_disabled",				&CScriptGameObject::IsPdaDisabled)
		.def("set_inventory_disabled",		&CScriptGameObject::SetInventoryDisabled)
		.def("is_inventory_disabled",		&CScriptGameObject::IsInventoryDisabled)
		
		.def("hide_weapon",					&CScriptGameObject::HideWeapon)
		.def("hide_detector",				&CScriptGameObject::HideDetector)
		.def("switch_detector",				&CScriptGameObject::SwitchDetector)
		.def("restore_weapon",				&CScriptGameObject::RestoreWeapon)
		
		.def("weapon_is_grenadelauncher",	&CScriptGameObject::Weapon_IsGrenadeLauncherAttached)
		.def("weapon_is_scope",				&CScriptGameObject::Weapon_IsScopeAttached)
		.def("weapon_is_silencer",			&CScriptGameObject::Weapon_IsSilencerAttached)

		.def("weapon_grenadelauncher_status",	&CScriptGameObject::Weapon_GrenadeLauncher_Status)
		.def("weapon_scope_status",				&CScriptGameObject::Weapon_Scope_Status)
		.def("weapon_silencer_status",			&CScriptGameObject::Weapon_Silencer_Status)

		.def("allow_sprint",				&CScriptGameObject::AllowSprint)

		.def("set_start_dialog",			&CScriptGameObject::SetStartDialog)
		.def("get_start_dialog",			&CScriptGameObject::GetStartDialog)
		.def("restore_default_start_dialog",&CScriptGameObject::RestoreDefaultStartDialog)

		.def("goodwill",					&CScriptGameObject::GetGoodwill)
		.def("set_goodwill",				&CScriptGameObject::SetGoodwill)
		.def("force_set_goodwill",			&CScriptGameObject::ForceSetGoodwill)
		.def("change_goodwill",				&CScriptGameObject::ChangeGoodwill)

		.def("general_goodwill",			&CScriptGameObject::GetAttitude)
		.def("set_relation",				&CScriptGameObject::SetRelation)
		
		.def("community_goodwill",			&CScriptGameObject::GetCommunityGoodwill_obj)
		.def("set_community_goodwill",		&CScriptGameObject::SetCommunityGoodwill_obj)

		.def("sympathy",					&CScriptGameObject::GetSympathy)
		.def("set_sympathy",				&CScriptGameObject::SetSympathy)

		//////////////////////////////////////////////////////////////////////////
		.def("profile_name",				&CScriptGameObject::ProfileName)
		.def("character_name",				&CScriptGameObject::CharacterName)
        .def("character_dialogs", &anomaly_character_dialogs)
		.def("character_icon",				&CScriptGameObject::CharacterIcon)
		.def("character_rank",				&CScriptGameObject::CharacterRank)
		.def("set_character_rank",			&CScriptGameObject::SetCharacterRank)
		.def("change_character_rank",		&CScriptGameObject::ChangeCharacterRank)
		.def("character_reputation",		&CScriptGameObject::CharacterReputation)
		.def("set_character_reputation",	&CScriptGameObject::SetCharacterReputation)
		.def("change_character_reputation",	&CScriptGameObject::ChangeCharacterReputation)
		.def("character_community",			&CScriptGameObject::CharacterCommunity)
		.def("set_character_community",		&CScriptGameObject::SetCharacterCommunity)

		.def("get_actor_relation_flags",	&CScriptGameObject::get_actor_relation_flags)
		.def("set_actor_relation_flags",	&CScriptGameObject::set_actor_relation_flags)
		.def("sound_voice_prefix",	&CScriptGameObject::sound_voice_prefix)

		.enum_("ACTOR_RELATIONS")
		[
			value("relation_attack",						int(RELATION_REGISTRY::ATTACK)),
			value("relation_fight_help_monster",			int(RELATION_REGISTRY::FIGHT_HELP_MONSTER)),
			value("relation_fight_help_human",				int(RELATION_REGISTRY::FIGHT_HELP_HUMAN)),
			value("relation_kill",							int(RELATION_REGISTRY::KILL))
		]

		.enum_("CLSIDS")
		[
			value("no_pda_msg",				int(ePdaMsgMax))
		]

		//Boosters
		.def("is_booster_influence", &CScriptGameObject::IsBoosterInfluence)
		.def("get_booster_influence_time", &CScriptGameObject::GetBoosterInfluenceTime)
		.def("apply_booster", &CScriptGameObject::ApplyBooster)
		.def("set_booster_time", &CScriptGameObject::SetBoosterTime)

		//Actor states
		.def("get_movement_state", &CScriptGameObject::GetActorMovementState)
		.def("set_movement_state", &CScriptGameObject::SetActorMovementState)

		//CustomZone
		.def("set_restrictor_type",			&CScriptGameObject::SetRestrictionType) 
		.def("get_restrictor_type",			&CScriptGameObject::GetRestrictionType)
		.def("set_anomaly_position", &set_anomaly_position)
		.def("enable_anomaly",              &CScriptGameObject::EnableAnomaly)
		.def("disable_anomaly",             &CScriptGameObject::DisableAnomaly)
		.def("get_anomaly_power",			&CScriptGameObject::GetAnomalyPower)
		.def("set_anomaly_power",			&CScriptGameObject::SetAnomalyPower)

        .def("get_artefact_health",			&CScriptGameObject::GetArtefactHealthRestoreSpeed)
        .def("get_artefact_radiation",			&CScriptGameObject::GetArtefactRadiationRestoreSpeed)
        .def("get_artefact_satiety",			&CScriptGameObject::GetArtefactSatietyRestoreSpeed)
        .def("get_artefact_thirst",			&CScriptGameObject::GetArtefactThirstRestoreSpeed)
        .def("get_artefact_power",			&CScriptGameObject::GetArtefactPowerRestoreSpeed)
        .def("get_artefact_bleeding",			&CScriptGameObject::GetArtefactBleedingRestoreSpeed)        

        .def("set_artefact_health",			&CScriptGameObject::SetArtefactHealthRestoreSpeed)
        .def("set_artefact_radiation",			&CScriptGameObject::SetArtefactRadiationRestoreSpeed)
        .def("set_artefact_satiety",			&CScriptGameObject::SetArtefactSatietyRestoreSpeed)
        .def("set_artefact_thirst",			&CScriptGameObject::SetArtefactThirstRestoreSpeed)
        .def("set_artefact_power",			&CScriptGameObject::SetArtefactPowerRestoreSpeed)
        .def("set_artefact_bleeding",			&CScriptGameObject::SetArtefactBleedingRestoreSpeed)
		//HELICOPTER
		.def("get_helicopter",              &CScriptGameObject::get_helicopter)
		.def("get_car",						&CScriptGameObject::get_car)
		.def("get_hanging_lamp",            &CScriptGameObject::get_hanging_lamp)
		.def("get_bone_id",					&CScriptGameObject::get_bone_id)
		.def("get_physics_shell",			&CScriptGameObject::get_physics_shell)
		.def("get_holder_class",			&CScriptGameObject::get_custom_holder)
		.def("get_current_holder",			&CScriptGameObject::get_current_holder)
		//usable object
		.def("set_tip_text",				&CScriptGameObject::SetTipText)
		.def("set_tip_text_default",		&CScriptGameObject::SetTipTextDefault)
		.def("set_nonscript_usable",		&CScriptGameObject::SetNonscriptUsable)

		// Script Zone
		.def("active_zone_contact",			&CScriptGameObject::active_zone_contact)
		.def("inside",						(bool (CScriptGameObject::*)(const Fvector &, float) const)(&CScriptGameObject::inside))
		.def("inside",						(bool (CScriptGameObject::*)(const Fvector &) const)(&CScriptGameObject::inside))
		.def("set_fastcall",				&CScriptGameObject::set_fastcall)
		.def("set_const_force",				&CScriptGameObject::set_const_force)
		.def("info_add",					&CScriptGameObject::info_add)
		.def("info_clear",					&CScriptGameObject::info_clear)

		// inv box
		.def("is_inv_box_empty",			&CScriptGameObject::IsInvBoxEmpty)
		.def("inv_box_closed",				&CScriptGameObject::inv_box_closed)
		.def("inv_box_closed_status",		&CScriptGameObject::inv_box_closed_status)
		.def("inv_box_can_take",			&CScriptGameObject::inv_box_can_take)
		.def("inv_box_can_take_status",		&CScriptGameObject::inv_box_can_take_status)

		// monster jumper
		.def("jump",						&CScriptGameObject::jump)

		.def("make_object_visible_somewhen",&CScriptGameObject::make_object_visible_somewhen)

		.def("buy_condition",				(void (CScriptGameObject::*)(CScriptIniFile*,LPCSTR))(&CScriptGameObject::buy_condition))
		.def("buy_condition",				(void (CScriptGameObject::*)(float,float))(&CScriptGameObject::buy_condition))
		.def("show_condition",				&CScriptGameObject::show_condition)
		.def("sell_condition",				(void (CScriptGameObject::*)(CScriptIniFile*,LPCSTR))(&CScriptGameObject::sell_condition))
		.def("sell_condition",				(void (CScriptGameObject::*)(float,float))(&CScriptGameObject::sell_condition))
		.def("buy_supplies",				&CScriptGameObject::buy_supplies)
		.def("buy_item_condition_factor",	&CScriptGameObject::buy_item_condition_factor)
        .def("buy_item_exponent", &set_anomaly_buy_exponent)
        .def("sell_item_exponent", &set_anomaly_sell_exponent)

		.def("sound_prefix",				(LPCSTR (CScriptGameObject::*)() const)(&CScriptGameObject::sound_prefix))
		.def("sound_prefix",				(void (CScriptGameObject::*)(LPCSTR))(&CScriptGameObject::sound_prefix))

		.def("location_on_path",			&CScriptGameObject::location_on_path)
		.def("is_there_items_to_pickup",	&CScriptGameObject::is_there_items_to_pickup)
		.def("is_ladder",					&CScriptGameObject::IsActorLadder)

		.def("wounded",						(bool (CScriptGameObject::*)() const)(&CScriptGameObject::wounded))
		.def("wounded",						(void (CScriptGameObject::*)(bool))(&CScriptGameObject::wounded))

		.def("iterate_inventory",			&CScriptGameObject::IterateInventory)
		.def("iterate_inventory", &iterate_inventory_plain)
		.def("iterate_belt", &iterate_anomaly_belt)
		.def("iterate_belt", &iterate_anomaly_belt_plain)
		.def("iterate_ruck", &iterate_anomaly_ruck)
		.def("iterate_ruck", &iterate_anomaly_ruck_plain)
		.def("iterate_inventory_box",		&CScriptGameObject::IterateInventoryBox)
		.def("iterate_inventory_box", &iterate_inventory_box_plain)
		.def("mark_item_dropped",			&CScriptGameObject::MarkItemDropped)
		.def("marked_dropped",				&CScriptGameObject::MarkedDropped)
		.def("unload_magazine",				&CScriptGameObject::UnloadMagazine)

		.def("sight_params",				&CScriptGameObject::sight_params)

		.def("movement_enabled",			&CScriptGameObject::enable_movement)
		.def("movement_enabled",			&CScriptGameObject::movement_enabled)

		.def("critically_wounded",			&CScriptGameObject::critically_wounded)
		.def("get_campfire",				&CScriptGameObject::get_campfire)
		.def("get_artefact",				&CScriptGameObject::get_artefact)
		.def("get_physics_object",			&CScriptGameObject::get_physics_object)
		.def("aim_time",					(void (CScriptGameObject::*) (CScriptGameObject*, u32))&CScriptGameObject::aim_time)
		.def("aim_time",					(u32 (CScriptGameObject::*) (CScriptGameObject*))&CScriptGameObject::aim_time)

		.def("special_danger_move",			(void (CScriptGameObject::*) (bool))&CScriptGameObject::special_danger_move)
		.def("special_danger_move",			(bool (CScriptGameObject::*) ())&CScriptGameObject::special_danger_move)

		.def("sniper_update_rate",			(void (CScriptGameObject::*) (bool))&CScriptGameObject::sniper_update_rate)
		.def("sniper_update_rate",			(bool (CScriptGameObject::*) () const)&CScriptGameObject::sniper_update_rate)

		.def("sniper_fire_mode",			(void (CScriptGameObject::*) (bool))&CScriptGameObject::sniper_fire_mode)
		.def("sniper_fire_mode",			(bool (CScriptGameObject::*) () const)&CScriptGameObject::sniper_fire_mode)

		.def("aim_bone_id",					(void (CScriptGameObject::*) (LPCSTR))&CScriptGameObject::aim_bone_id)
		.def("aim_bone_id",					(LPCSTR (CScriptGameObject::*) () const)&CScriptGameObject::aim_bone_id)

		.def("actor_look_at_point",			&CScriptGameObject::ActorLookAtPoint)
		.def("enable_level_changer",		&CScriptGameObject::enable_level_changer)
		.def("is_level_changer_enabled",	&CScriptGameObject::is_level_changer_enabled)

		.def("is_actor_outdoors",			&CScriptGameObject::IsActorOutdoors)

		.def("set_level_changer_invitation",&CScriptGameObject::set_level_changer_invitation)
		.def("start_particles",				&CScriptGameObject::start_particles)
		.def("stop_particles",				&CScriptGameObject::stop_particles)
					//For Car
		.def("attach_vehicle",				&CScriptGameObject::AttachVehicle)
		.def("detach_vehicle",				&CScriptGameObject::DetachVehicle)
		.def("get_attached_vehicle",		&CScriptGameObject::GetAttachedVehicle)
		.def("ray",							&CScriptGameObject::RayPick)
		.def("is_jump",						&CScriptGameObject::ActorIsJump)

		//
		.def("iterate_feel_touch",			&CScriptGameObject::IterateFeelTouch)
		.def("get_weapon_substate",			&CScriptGameObject::GetWeaponSubstate)
		.def("get_ammo_count_for_type",     &CScriptGameObject::GetAmmoCount)
		.def("get_main_weapon_type",		&CScriptGameObject::GetMainWeaponType)
		.def("get_luminocity", 				&CScriptGameObject::GetLuminocity)
		.def("bone_visible", 				&CScriptGameObject::IsBoneVisible)
		.def("set_bone_visible", 			&CScriptGameObject::SetBoneVisible)
        .def("set_bone_visible", &set_anomaly_bone_visible)
		.def("force_set_position", 			&CScriptGameObject::ForceSetPosition)
		.def("set_spatial_type", 			&CScriptGameObject::SetSpatialType)
		.def("get_spatial_type", 			&CScriptGameObject::GetSpatialType)
		.def("remove_danger", 				&CScriptGameObject::RemoveDanger)
		.def("remove_memory_sound_object", 	&CScriptGameObject::RemoveMemorySoundObject)
		.def("remove_memory_visible_object", &CScriptGameObject::RemoveMemoryVisibleObject)
		.def("remove_memory_hit_object", 	&CScriptGameObject::RemoveMemoryHitObject)
		.def("get_weapon_type",				&CScriptGameObject::GetWeaponType)
			
		///////////////////////////////////////////////////////////////////////////////
		// CoC
		.def("weapon_in_grenade_mode", &CScriptGameObject::WeaponInGrenadeMode)
		.def("weapon_set_scope", &CScriptGameObject::Weapon_SetCurrentScope)
		.def("weapon_get_scope", &CScriptGameObject::Weapon_GetCurrentScope)
		.def("phantom_set_enemy", &CScriptGameObject::PhantomSetEnemy)
		.def("cast_GameObject", &CScriptGameObject::cast_GameObject)
		.def("cast_Car", &CScriptGameObject::cast_Car)
		.def("cast_Heli", &CScriptGameObject::cast_Heli)
		.def("cast_HolderCustom", &CScriptGameObject::cast_HolderCustom)
		.def("cast_EntityAlive", &CScriptGameObject::cast_EntityAlive)
		.def("cast_InventoryItem", &CScriptGameObject::cast_InventoryItem)
		.def("cast_InventoryOwner", &CScriptGameObject::cast_InventoryOwner)
		.def("cast_Actor", &CScriptGameObject::cast_Actor)
		.def("cast_Weapon", &cast_anomaly_weapon)
		.def("cast_Medkit", &CScriptGameObject::cast_Medkit)
		.def("cast_EatableItem", &CScriptGameObject::cast_EatableItem)
		.def("cast_Antirad", &CScriptGameObject::cast_Antirad)
		.def("cast_CustomOutfit", &CScriptGameObject::cast_CustomOutfit)
		.def("cast_Helmet", +[](CScriptGameObject* self) -> CHelmet* { return self ? self->object().cast_helmet() : nullptr; })
		.def("cast_Scope", &CScriptGameObject::cast_Scope)
		.def("cast_Silencer", &CScriptGameObject::cast_Silencer)
		.def("cast_GrenadeLauncher", &CScriptGameObject::cast_GrenadeLauncher)
		.def("cast_SpaceRestrictor", &CScriptGameObject::cast_SpaceRestrictor)
		.def("cast_Stalker", &CScriptGameObject::cast_Stalker)
		.def("cast_CustomZone", &CScriptGameObject::cast_CustomZone)
		.def("cast_Monster", &CScriptGameObject::cast_Monster)
		.def("cast_Explosive", &CScriptGameObject::cast_Explosive)
		.def("cast_ScriptZone", &CScriptGameObject::cast_ScriptZone)
		//.def("cast_Projector", &CScriptGameObject::cast_Projector)
		.def("cast_Trader", &CScriptGameObject::cast_Trader)
		.def("cast_HudItem", &CScriptGameObject::cast_HudItem)
		.def("cast_FoodItem", &CScriptGameObject::cast_FoodItem)
		.def("cast_Artefact", &CScriptGameObject::cast_Artefact)
		.def("cast_Ammo", &CScriptGameObject::cast_Ammo)
		//.def("cast_Missile", &CScriptGameObject::cast_Missile)
		.def("cast_PhysicsShellHolder", &CScriptGameObject::cast_PhysicsShellHolder)
		//.def("cast_Grenade", &CScriptGameObject::cast_Grenade)
		.def("cast_BottleItem", &CScriptGameObject::cast_BottleItem)
		.def("cast_Torch", &CScriptGameObject::cast_Torch)
		.def("cast_InventoryBox", &CScriptGameObject::cast_InventoryBox)
		.def("bones_protection_sect", &CScriptGameObject::bones_protection_sect)															  
		.def("is_on_belt",					&CScriptGameObject::IsOnBelt)
		.def("item_on_belt",				&CScriptGameObject::ItemOnBelt) 
		.def("belt_count",					&CScriptGameObject::BeltSize)  													   
		.def("get_actor_max_weight",		&CScriptGameObject::GetActorMaxWeight)
		.def("set_actor_max_weight",		&CScriptGameObject::SetActorMaxWeight)
		.def("get_actor_max_walk_weight",	&CScriptGameObject::GetActorMaxWalkWeight)
		.def("set_actor_max_walk_weight",	&CScriptGameObject::SetActorMaxWalkWeight)
		.def("get_additional_max_weight",		&CScriptGameObject::GetAdditionalMaxWeight)
		.def("set_additional_max_weight",		&CScriptGameObject::SetAdditionalMaxWeight)
		.def("get_additional_max_walk_weight",	&CScriptGameObject::GetAdditionalMaxWalkWeight)
		.def("set_additional_max_walk_weight",	&CScriptGameObject::SetAdditionalMaxWalkWeight)
		.def("get_total_weight",			&CScriptGameObject::GetTotalWeight)
		.def("weight",						&CScriptGameObject::Weight)        

		.def("get_actor_jump_speed",		&CScriptGameObject::GetActorJumpSpeed)
		.def("set_actor_jump_speed",		&CScriptGameObject::SetActorJumpSpeed)
		.def("get_actor_sprint_koef",		&CScriptGameObject::GetActorSprintKoef)
		.def("set_actor_sprint_koef",		&CScriptGameObject::SetActorSprintKoef) 
		.def("get_actor_run_coef",		&CScriptGameObject::GetActorRunCoef)
		.def("set_actor_run_coef",		&CScriptGameObject::SetActorRunCoef) 
		.def("get_actor_runback_coef",		&CScriptGameObject::GetActorRunBackCoef)
		.def("set_actor_runback_coef",		&CScriptGameObject::SetActorRunBackCoef)   
		.def("get_actor_power_boost_time", &CScriptGameObject::GetActorPowerBoostTime)

		//For Weapons
		.def("weapon_get_ammo_section",		&CScriptGameObject::Weapon_GetAmmoSection)
		.def("weapon_addon_attach",			&CScriptGameObject::Weapon_AddonAttach)
		.def("weapon_addon_detach",			&CScriptGameObject::Weapon_AddonDetach)
		.def("get_ammo_count_for_type",     &CScriptGameObject::GetAmmoCount)
		.def("get_main_weapon_type",		&CScriptGameObject::GetMainWeaponType)
		.def("get_weapon_type",				&CScriptGameObject::GetWeaponType)
		.def("get_weapon_substate",			&CScriptGameObject::GetWeaponSubstate)

		// For CHudItem
		.def("play_hud_motion",				&CScriptGameObject::PlayHudMotion)
        .def("play_hud_motion", &play_anomaly_item_motion_speed)
        .def("play_hud_motion", &play_anomaly_item_motion)
		.def("switch_state",				&CScriptGameObject::SwitchState)
		.def("get_state",					&CScriptGameObject::GetState)
			
		// For EatableItem
		.def("set_remaining_uses",			&CScriptGameObject::SetRemainingUses)
		.def("get_remaining_uses",			&CScriptGameObject::GetRemainingUses)
		.def("get_max_uses",				&CScriptGameObject::GetMaxUses)

		//For Ammo
		.def("ammo_get_count",				&CScriptGameObject::AmmoGetCount)
		.def("ammo_set_count",				&CScriptGameObject::AmmoSetCount)
		.def("ammo_box_size",				&CScriptGameObject::AmmoBoxSize)
        .def("is_entity_alive", &is_anomaly_object_type<&CGameObject::cast_entity_alive>)
        .def("is_inventory_item", &is_anomaly_object_type<&CGameObject::cast_inventory_item>)
        .def("is_inventory_owner", &is_anomaly_object_type<&CGameObject::cast_inventory_owner>)
        .def("is_actor", &is_anomaly_object_type<&CGameObject::cast_actor>)
        .def("is_custom_monster", &is_anomaly_object_type<&CGameObject::cast_custom_monster>)
        .def("is_weapon", &is_anomaly_object_type<&CGameObject::cast_weapon>)
        .def("is_outfit", &is_anomaly_object_type<&CGameObject::cast_outfit>)
        .def("is_helmet", &is_anomaly_object_type<&CGameObject::cast_helmet>)
        .def("is_scope", &is_anomaly_object_type<&CGameObject::cast_addon_scope>)
        .def("is_silencer", &is_anomaly_object_type<&CGameObject::cast_addon_silencer>)
        .def("is_grenade_launcher", &is_anomaly_object_type<&CGameObject::cast_addon_grenade_launcher>)
        .def("is_weapon_magazined", &is_anomaly_object_type<&CGameObject::cast_weapon_magazined>)
        .def("is_space_restrictor", &is_anomaly_object_type<&CGameObject::cast_restrictor>)
        .def("is_stalker", &is_anomaly_object_type<&CGameObject::cast_stalker>)
        .def("is_anomaly", &is_anomaly_object_type<&CGameObject::cast_custom_zone>)
        .def("is_monster", &is_anomaly_object_type<&CGameObject::cast_base_monster>)
        .def("is_trader", &is_anomaly_object_type<&CGameObject::cast_trader>)
        .def("is_hud_item", &is_anomaly_object_type<&CGameObject::cast_hud_item>)
        .def("is_artefact", &is_anomaly_object_type<&CGameObject::cast_artefact>)
        .def("is_weapon_gl", &is_anomaly_object_type<&CGameObject::cast_weapon_magazined_w_grenade>)
        .def("is_inventory_box", &is_anomaly_object_type<&CGameObject::cast_inventory_box>)
		.def("is_ammo",						&CScriptGameObject::IsAmmo)
		// Actor
		.def("set_character_icon", &CScriptGameObject::SetCharacterIcon)

		//For Weapon & Outfit
		.def("install_upgrade",				&CScriptGameObject::InstallUpgrade)
		.def("has_upgrade",					&CScriptGameObject::HasUpgrade)
		.def("iterate_installed_upgrades",	&CScriptGameObject::IterateInstalledUpgrades)
        .def("set_health_ex",				&CScriptGameObject::SetHealthEx)

		// 2055
		.def("get_cutscene_visual",			&CScriptGameObject::GetCutsceneVisual)
		.def("set_invulnerable",			&CScriptGameObject::SetInvulnerable)
		.def("set_best_enemy",				&CScriptGameObject::SetBestEnemy)
		.def("set_fire",					&CScriptGameObject::SetFire)
		.def("get_gasmask_status",			&CScriptGameObject::GetGasmaskStatus)
		.def("get_gasmask_condition",		&CScriptGameObject::GetGasmaskCondition)
		.def("set_head_rotate",				&CScriptGameObject::SetHeadRotate)
		.def("set_default_visual",			&CScriptGameObject::SetActorDefaultVisual)
		.def("IsInCar", &CScriptGameObject::IsInCar)
	;
}
