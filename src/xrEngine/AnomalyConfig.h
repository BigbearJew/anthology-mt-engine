#pragma once

// Explicit compatibility; native IX-Ray sections can opt out independently.
namespace AnomalyConfig
{
inline bool HasScopeTexture(LPCSTR value)
{
    return value && value[0] && xr_strcmp(value, "none") != 0;
}

inline bool Enabled(const CInifile& ini, LPCSTR section, bool default_value)
{
    return ini.line_exist(section, "anomaly_params") ? ini.r_bool(section, "anomaly_params") : default_value;
}

inline float ScopeZoom(const CInifile& ini, LPCSTR section, bool anomaly)
{
    if (anomaly && !ini.line_exist(section, "scope_zoom_factor"))
        return 0.f;
    return ini.r_float(section, "scope_zoom_factor");
}

inline float IronZoom(const CInifile& ini, LPCSTR section, bool anomaly)
{
    if (anomaly && ini.line_exist(section, "ironsight_zoom_factor"))
        return ini.r_float(section, "ironsight_zoom_factor");
    return ScopeZoom(ini, section, anomaly);
}

inline float ZoomFov(float value, float world_fov, float ironsight_factor)
{
    return value == 0.f ? rad2deg(2.f * atanf(tanf(deg2rad(world_fov) * .5f) / ironsight_factor)) : value * .75f;
}

inline float HudFovDegrees(float value, float world_fov)
{
    // Fractional Anomaly values and IX-Ray degrees are disjoint ranges.
    return value > 0.f && value <= 1.f ? value * world_fov : value;
}

inline float HudFov(const CInifile& ini, LPCSTR section, LPCSTR hud, bool anomaly)
{
    if (ini.line_exist(hud, "hud_fov"))
        return ini.r_float(hud, "hud_fov");
    return anomaly && ini.line_exist(section, "hud_fov") ? ini.r_float(section, "hud_fov") : 0.f;
}
}
