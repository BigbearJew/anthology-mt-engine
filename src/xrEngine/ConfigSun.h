#pragma once

// Anomaly stores its hourly sun angles separately from weather cycles.
class ConfigSunTable
{
    Fvector2 angles[24]{};
    bool ready = false;

public:
    bool load(const CInifile& config)
    {
        ready = false;
        for (unsigned hour = 0; hour < 24; ++hour)
        {
            char section[16];
            xr_sprintf(section, "%02u:00:00", hour);
            if (!config.line_exist(section, "sun_altitude") ||
                !config.line_exist(section, "sun_longitude"))
                return false;
            angles[hour].set(config.r_float(section, "sun_altitude"), config.r_float(section, "sun_longitude"));
            if (!_valid(angles[hour].x) || !_valid(angles[hour].y))
                return false;
        }
        ready = true;
        return true;
    }

    bool loaded() const { return ready; }

    bool sample(float seconds, Fvector2& result) const
    {
        if (!ready || !_valid(seconds))
            return false;
        float time = fmodf(seconds, 86400.f);
        if (time < 0.f)
            time += 86400.f;
        const float hour = time / 3600.f;
        const unsigned first = static_cast<unsigned>(hour) % 24;
        const unsigned next = (first + 1) % 24;
        const float weight = hour - floorf(hour);
        result.set(angles[first].x + (angles[next].x - angles[first].x) * weight,
                   angles[first].y + (angles[next].y - angles[first].y) * weight);
        return true;
    }
};
