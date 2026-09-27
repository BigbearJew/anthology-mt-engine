# Anthology v165: PDA residency, screen resolve, Russian MCM

Base: v164 / 546f631a8e6a541e7a6e3f0ec616a5e41e27af1a.

PDA map DDS images (`map/`, `ui/ui_global_map`, `ui/ui_nomap2`) now enter
the existing resource-loading queue instead of waiting for the first visible draw.
Idle/pressure trimming and level-unload trimming retain maps while their resource
references exist. Ordinary destruction and device reset still release resources.
World texture pressure management and offscreen map draw culling are unchanged.
This trades higher persistent map memory for removing synchronous map reloads
during navigation. The tested Anthology profile has 84 map images / ~2386 MiB.

The PDA forward shader rasterizes without scene TAA jitter. Its exclusion used to
return current color sampled at the scene's unjittered UV, moving stationary UI.
The overlay now writes the reserved HUD value z=2 with its existing skip alpha
w=1. TAA returns native current UV for this marker. Other HUD/world/foliage masks
retain v164 behavior, including the accepted independent HUD/TAA mask contract.
Motion z remains positive for other HUD consumers and alpha remains 1 for MRT
blending. `anthology_v165_pda` isolates compiled shaders from old cache entries.

Russian calendar and PiP string tables contained valid Russian UTF-8, but this
engine/font path consumes Windows-1251. Convert bytes and declarations, preserving
all IDs and text; do not change MCM settings or script APIs.

Validation helpers:
- tools/tests/texture_residency_test.cpp: 30 policy cases.
- tools/tests/pda_map_draw_test.py: 11 production UI draw cases.
- tools/tests/pda_taa_gpu_test.cpp: production resolve on D3D11; 12348 stationary
  UI channel samples / 21 jitter positions. v164 fails displacement, v165 passes.
- Optional `-ui_stream_diagnostics` logs each completed map DDS load for auditing
  that loading finishes before actor startup and never repeats during PDA use.

No Lost in Place port, world streaming, texture compression, SSS shadow, or hand
motion-vector changes are included here.
