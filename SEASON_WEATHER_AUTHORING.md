# Calendar and seasonal weather

The calendar reads the save's game year/month. It never reads the computer's date. MCM → runtime seasons → calendar enables automatic selection; disable it to retain manual selection. The six existing visual presets are preserved. Transitions select a preset on a month boundary; daily texture crossfades are not implemented.

`gamedata/configs/anthology_seasons.ltx` maps months 1–12 to styles 1–6. Defaults: January/February/December winter (4), March/November thaw (5), April spring (6), May–August summer (1), September autumn (2), October bare vegetation (3).

Create a normal XRay weather cycle at `gamedata/configs/environment/weathers/my_winter_clear.ltx`, including its time-of-day descriptors and resources. Cycle files load at game startup; restart after adding them. Then add your uniquely named registration file under `gamedata/configs/anthology_seasons/`, for example:

```ini
[weather_myaddon_winter_clear_01]
style = 4
category = clear
cycle = my_winter_clear
```

Supported categories: `clear`, `partly`, `cloudy`, `rain`, `storm`, `foggy`. Add any number of uniquely named registration sections/files. Several cycles for a style/category form its selection pool. There is no explicit count limit; normal memory/resource limits still apply. Unknown cycles are rejected and logged. Categories without a seasonal pool keep the modpack's original pool. Weather transitions, emissions and underground behavior remain owned by `level_weathers.WeatherManager`.

Scripts may call `anthology_runtime_seasons.register_weather(style, category, cycle)` after the level's weather cycles are loaded. It returns false on invalid style/category or an unavailable cycle. Re-registering an existing cycle does not duplicate its pool entry. Global runtime registrations are reconstructed by addon startup; engine objects are never stored in save data.

Winter style 4 selects the adapted SSS ice shader and alternate physical properties for `materials\water` and `materials\water_radiation`. Other material IDs/pairs and radiation damage are preserved. Switching away restores the modpack's original materials. Ice geometry is the existing level water surface; this does not create missing collision meshes or new water bodies.

The snowfall extension adds bounded collision queries against nearby dynamic models to the existing balanced snow effect. It does not implement snow accumulation, footprints or a snow layer on HUD weapons.
