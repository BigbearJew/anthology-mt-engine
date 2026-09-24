# v156: live PiP, temporal inputs and seasonal calendar — test candidate

This is an experimental candidate. The user's rejected v155 image quality is not considered resolved merely because compilation or screenshots pass. Image quality in motion, shadow ghosting and intermittent GPU saturation still require acceptance in the affected scenes.

## Changes

- PiP mode 2 renders a fresh lens scene on every presentation. Simulation runs before the lens capture; the main view restores its camera/postprocessing and reuses the same simulation pose. It has separate four-sample TAA, depth/motion rejection, bounded neighborhood history and a reactive mask. It crops unused rays around the lens, preserving pixels per degree. This is not frame generation. Modes 0/1 and their repeat interval remain available.
- Lens sampling uses bounded cubic reconstruction instead of the extra bilinear resampling. A matching analytic GPU test checks detail reconstruction and silhouette bounds.
- Fixed a reproduced deadlock in `CHUDManager::RenderUI`: a main view with no new world tick must wait for the capture's UI frame, not a frame that will never be scheduled. The failed diagnostic `outdoor-sync-i` was dumped and terminated; it is not a passing test.
- DLSS bias-current-color and FSR reactive/composition masks use the opaque-to-forward difference. Invalid-frame masks are disabled. This targets transparent/effect history; it does not prove all lighting ghosting is gone.
- Screen-space shadow rays are clipped to the valid view range. History rejects raw shadow changes, uses matching depth/jitter and shorter history on HUD surfaces. Local-light history is invalidated for moved/switched lights.
- Inactive PiP targets/history can be released after five seconds when the local DXGI budget is at least 90% consumed. This is a bounded memory-pressure mitigation, not a demonstrated explanation of the user's 99–100% GPU issue.
- A shutdown heap-corruption trace led to using the engine allocator for the native-load queue and shared control blocks. Subsequent j/k/l runs exit normally; the earlier failed run remains recorded.
- Game-month calendar with manual override and open-ended weather-cycle registration; existing weather manager owns transitions/emissions/underground handling. See the weather authoring guides.
- SSS seasonal ice shaders/textures and immutable winter water-material alternatives preserve the pack's IDs, material pairs and damage factors. Water meshes with near-zero baked lighting use current weather sky light, avoiding a constant emissive floor. Switching out of winter restores original water behavior.
- Balanced snowfall gets nearby dynamic-model collision queries with a 256-query budget per effect call. No accumulation, footprints or HUD snow layer.

## Evidence, 24 September 2026

Workspace evidence is under `../work-v156/`. Tests use a separate materialized copy of the active MO2 profile and copied saves; they never ship with the player payload. DX11 and DX11-AVX candidate l builds pass; cache namespace is `anthology_pip156f`.

| Check | Result and scope |
| --- | --- |
| HLSL | 61 variants pass, including water with/without MSAA; six VS/PS interface pairs pass with a failing-old-interface negative control. |
| RTX 5070 GPU tests | Production reactive masks/history rejection, color/depth/HUD inputs pass. Bounded cubic test: analytic error 0.012320 → 0.001986 over 144 samples, no silhouette overshoot. |
| Calendar Lua 5.1 | All 12 months, manual override, unavailable-cycle fallback, emissions and external pool replacement pass. Touched scripts pass syntax checks. |
| `outdoor-sync-j` | AVX, native/DLSS/FSR, exit 0, no UI hang, 188 s. Warm capture age 0 ms instead of the prior 25–37 ms. |
| `far-compare-j` | DX11, fresh-old/live/repeat-4/live, exit 0, 179 s. Approximate scene-specific median 24–29 ms versus 23–25 ms; changing-camera preliminary comparison, not a universal speedup. |
| `field-modes-k` | AVX, trees, camera movement, rain, DLSS/FSR/native, exit 0, 249 s. Native median around 22.4 ms. Real foreground-window recording at `runtime/field-modes-k/visible-motion.mp4`. |
| `soak-modes-k` | 300 s measured gameplay, ADS transitions, torch, time-of-day and upscaler changes; total 418 s, exit 0. Local memory 4783–8248 MiB; max GPU 54%, temperature 65°C. Saturation symptom not reproduced. |
| `winter-light-l` | Same scene summer/winter, corrected ice lighting, no green tint in controlled scene; exit 0, 204 s including a cold shader cache. Earlier complex winter fixture's tint is not attributed to a proven root cause. |
| `winter-physics-l` | Force-transform wakes physics: actor falls from 3 m above the lake onto ice at y=-35.325 (surface -35.311), then to y=-36.561 after thaw. Exit 0, 111 s. Earlier sleeping-actor teleports did not establish this. |
| Winter save/load | Runtime calendar/weather and material restoration pass; `winter-standing-k` reload exits 0. A fixture-order error in `winter-standing-j` was fixed in the fixture, not claimed as a production fix. |
| Archive | Packing/unpacking reproduces all 1023 resource hashes. |

`PrintWindow` capture from `outdoor-sync-i` was black and is invalid evidence. Still images never establish freedom from swimming/lag. The real recording remains a test artifact, not a declaration of user acceptance.

## Remaining acceptance work

1. User-scene PiP motion, edge stability and shadow trails on weapon modules/interiors; DLSS/FSR detail at the chosen output resolution.
2. Intermittent 99–100% GPU degradation and recovery after minimizing: not reproduced by the bounded soak. Memory trimming was not triggered in that run.
3. Dynamic snow collision on varied models and freeze/thaw on other map water meshes. Walking physics was verified on one Cordon water surface.
4. Calendar is discrete by month. Daily texture blending and accumulating model snow are outside this implemented version.

The player archive is for testing. It must not be described as a complete fix for all reported visual/performance defects.
