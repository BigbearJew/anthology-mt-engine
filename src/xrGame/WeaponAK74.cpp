#include "StdAfx.h"
#include "pch_script.h"
#include "WeaponAK74.h"
#include "game_cl_single.h"

CWeaponAK74::CWeaponAK74(ESoundTypes eSoundType) : CWeaponMagazinedWGrenade(eSoundType)
{}

CWeaponAK74::~CWeaponAK74()
{}

namespace
{
void anomaly_ammo_types(CWeapon* weapon, const luabind::functor<bool>& callback)
{
    // Callbacks may change upgrades/ammunition; iterate a stable snapshot.
    const auto types = weapon->m_ammoTypes;
    for (u32 i = 0; i < types.size(); ++i)
        if (callback(i, types[i].c_str())) break;
}
LPCSTR anomaly_scope_name(CWeapon* w) { return w->GetScopeName().c_str(); }
LPCSTR anomaly_silencer_name(CWeapon* w) { return w->GetSilencerName().c_str(); }
LPCSTR anomaly_launcher_name(CWeapon* w) { return w->GetGrenadeLauncherName().c_str(); }
u32 anomaly_ammo_type(CWeapon* w) { return w->GetAmmoType(); }
int anomaly_ammo_total(CWeapon* w) { return w->GetSuitableAmmoTotal(); }
float anomaly_magazine_weight(CWeapon* w) { return w->GetMagazineWeight(w->m_magazine); }
int anomaly_ammo_count(CWeapon* w, LPCSTR section) { return w->GetAmmoCount_forType(section); }
float anomaly_fire_dispersion(CWeapon* w) { return w->getFireDispersionBase(); }
float anomaly_hit_power(CWeapon* w) { return w->getHitPower()[g_SingleGameDifficulty]; }
float anomaly_shot_interval(CWeapon* w) { return w->getRPM(); }
float anomaly_real_rpm(CWeapon* w) { return w->getRPM() > 0.f ? 60.f / w->getRPM() : 0.f; }
void anomaly_set_shot_interval(CWeapon* w, float seconds) { if (_valid(seconds) && seconds > 0.f) w->setRPM(seconds); }
void anomaly_set_real_rpm(CWeapon* w, float rpm) { if (_valid(rpm) && rpm > 0.f) w->setRPM(60.f / rpm); }

}

using namespace luabind;

#pragma optimize("s",on)
void CWeaponAK74::script_register	(lua_State *L)
{
	module(L)
	[
		class_<CWeaponAK74,CGameObject>("CWeaponAK74")
			.def(constructor<>()),
		class_<CWeapon, CGameObject>("CWeapon")
			.def("AmmoTypeForEach", &anomaly_ammo_types)
			.def("GetScopeName", &anomaly_scope_name)
			.def("GetSilencerName", &anomaly_silencer_name)
			.def("GetGrenadeLauncherName", &anomaly_launcher_name)
			.def("GetAmmoType", &anomaly_ammo_type)
			.def("GetSuitableAmmoTotal", &anomaly_ammo_total)
			.def("GetMagazineWeight", &anomaly_magazine_weight)
			.def("GetAmmoCount_forType", &anomaly_ammo_count)
			.def("GetFireDispersion", &anomaly_fire_dispersion)
            .def("Get_PDM_Base", &CWeapon::Get_PDM_Base)
            .def("GetHitPower", &anomaly_hit_power)
            .def("RPM", &anomaly_shot_interval)
            .def("RealRPM", &anomaly_real_rpm)
            .def("SetRPM", &anomaly_set_shot_interval)
            .def("SetRealRPM", &anomaly_set_real_rpm)
			.def("IsGrenadeLauncherAttached", &CWeapon::IsGrenadeLauncherAttached)
			.def("GrenadeLauncherAttachable", &CWeapon::GrenadeLauncherAttachable)
			.def("IsScopeAttached", &CWeapon::IsScopeAttached)
			.def("ScopeAttachable", &CWeapon::ScopeAttachable)
			.def("IsSilencerAttached", &CWeapon::IsSilencerAttached)
			.def("SilencerAttachable", &CWeapon::SilencerAttachable)
			.def("IsZoomEnabled", &CWeapon::IsZoomEnabled)
			.def("IsZoomed", &CWeapon::IsZoomed)
            .def("CanBeLowered", &CWeapon::CanBeLowered)
            .def("SetLowered", &CWeapon::SetLowered)
			.def("GetZoomFactor", &CWeapon::GetZoomFactor)
			.def("SetZoomFactor", &CWeapon::SetZoomFactor)
			.def("IsSingleHanded", &CWeapon::IsSingleHanded)
			.def("GetBaseDispersion", &CWeapon::GetBaseDispersion)
			.def("GetMisfireStartCondition", &CWeapon::GetMisfireStartCondition)
			.def("GetMisfireEndCondition", &CWeapon::GetMisfireEndCondition)
			.def("GetAmmoElapsed", &CWeapon::GetAmmoElapsed)
			.def("GetAmmoMagSize", &CWeapon::GetAmmoMagSize)
			.def("SetAmmoElapsed", &CWeapon::SetAmmoElapsed)
			.def("SwitchAmmoType", &CWeapon::SwitchAmmoType)
			.def("SetAmmoType", &CWeapon::SetAmmoType)
			.def("Cost", &CWeapon::Cost)
			.def("Weight", &CWeapon::Weight)
			.def("IsMisfire", &CWeapon::IsMisfire)
            // Bind the inherited methods to the class exposed to Lua.
			.def("IsPending", static_cast<bool (CWeapon::*)() const>(&CWeapon::IsPending))
			.def("SetPending", static_cast<void (CWeapon::*)(bool)>(&CWeapon::SetPending))
			.def("SetMisfire", &CWeapon::SetMisfireStatus)
			.def("GetFireMode", &CWeapon::GetCurrentFireMode)
	];
}
