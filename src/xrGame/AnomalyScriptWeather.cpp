#include "stdafx.h"
#include "pch_script.h"
#include "../xrEngine/Environment.h"
#include "../xrEngine/Rain.h"
#include "../xrEngine/IGame_Persistent.h"

namespace
{
CEnvironment* script_weather()
{
    return g_pGamePersistent ? g_pGamePersistent->pEnvironment : nullptr;
}

float rain_wetness()
{
    auto* env = script_weather();
    return env ? env->wetness_factor : 0.f;
}

float rain_volume()
{
    auto* env = script_weather();
    return env && env->eff_Rain ? env->eff_Rain->GetRainVolume() : 0.f;
}

float weather_number(LPCSTR name)
{
    auto* env = script_weather();
    if (!env || !env->CurrentEnv || !name) return 0.f;
    const auto& e = *env->CurrentEnv;
#define WEATHER_NUMBER(key, field) if (xr_strcmp(name, key) == 0) return e.field
    WEATHER_NUMBER("sky_rotation", sky_rotation);
    WEATHER_NUMBER("far_plane", far_plane);
    WEATHER_NUMBER("fog_density", fog_density);
    WEATHER_NUMBER("fog_distance", fog_distance);
    WEATHER_NUMBER("rain_density", rain_density);
    WEATHER_NUMBER("thunderbolt_period", bolt_period);
    WEATHER_NUMBER("thunderbolt_duration", bolt_duration);
    WEATHER_NUMBER("wind_velocity", wind_velocity);
    WEATHER_NUMBER("wind_direction", wind_direction);
    WEATHER_NUMBER("sun_shafts_intensity", m_fSunShaftsIntensity);
    WEATHER_NUMBER("water_intensity", m_fWaterIntensity);
    WEATHER_NUMBER("tree_amplitude_intensity", trees_amplitude);
#undef WEATHER_NUMBER
    return 0.f;
}

Fvector weather_vector(LPCSTR name)
{
    Fvector result = {};
    auto* env = script_weather();
    if (!env || !env->CurrentEnv || !name) return result;
    const auto& e = *env->CurrentEnv;
#define WEATHER_VECTOR(key, field) if (xr_strcmp(name, key) == 0) return e.field
    WEATHER_VECTOR("sky_color", sky_color);
    WEATHER_VECTOR("fog_color", fog_color);
    WEATHER_VECTOR("rain_color", rain_color);
    WEATHER_VECTOR("ambient_color", ambient);
    WEATHER_VECTOR("sun_color", sun_color);
    WEATHER_VECTOR("sun_dir", sun_dir);
#undef WEATHER_VECTOR
    if (xr_strcmp(name, "clouds_color") == 0)
        result.set(e.clouds_color.x, e.clouds_color.y, e.clouds_color.z);
    else if (xr_strcmp(name, "hemisphere_color") == 0)
        result.set(e.hemi_color.x, e.hemi_color.y, e.hemi_color.z);
    return result;
}

LPCSTR weather_string(LPCSTR name)
{
    auto* env = script_weather();
    if (!env || !env->CurrentEnv || !name) return "";
    const auto& e = *env->CurrentEnv;
    if (xr_strcmp(name, "sky_texture") == 0) return e.sky_texture_name.c_str();
    if (xr_strcmp(name, "clouds_texture") == 0) return e.clouds_texture_name.c_str();
    if (xr_strcmp(name, "ambient") == 0 && e.env_ambient) return e.env_ambient->name().c_str();
    return "";
}

void weather_pause(bool paused)
{
    if (auto* env = script_weather()) env->m_paused = paused;
}

bool weather_paused()
{
    auto* env = script_weather();
    return env && env->m_paused;
}
}

void RegisterAnomalyScriptWeather(lua_State* L)
{
    using namespace luabind;
    module(L, "level")
    [
        def("rain_wetness", &rain_wetness),
        def("get_rain_volume", &rain_volume)
    ];
    module(L, "weather")
    [
        def("get_value_numric", &weather_number),
        def("get_value_vector", &weather_vector),
        def("get_value_string", &weather_string),
        def("pause", &weather_pause),
        def("is_paused", &weather_paused)
    ];
}
