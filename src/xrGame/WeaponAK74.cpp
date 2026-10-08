#include "StdAfx.h"
#include "pch_script.h"
#include "WeaponAK74.h"

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
			.def("IsGrenadeLauncherAttached", &CWeapon::IsGrenadeLauncherAttached)
			.def("GrenadeLauncherAttachable", &CWeapon::GrenadeLauncherAttachable)
			.def("IsScopeAttached", &CWeapon::IsScopeAttached)
			.def("ScopeAttachable", &CWeapon::ScopeAttachable)
			.def("IsSilencerAttached", &CWeapon::IsSilencerAttached)
			.def("SilencerAttachable", &CWeapon::SilencerAttachable)
			.def("IsZoomEnabled", &CWeapon::IsZoomEnabled)
			.def("IsZoomed", &CWeapon::IsZoomed)
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
			.def("IsPending", &CWeapon::IsPending)
			.def("SetPending", &CWeapon::SetPending)
			.def("SetMisfire", &CWeapon::SetMisfireStatus)
			.def("GetFireMode", &CWeapon::GetCurrentFireMode)
	];
}
