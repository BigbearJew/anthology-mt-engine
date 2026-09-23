# PiP validation

## Current v144 motion reconstruction

v144 retains the separate detailed PiP capture and v143 capture interval. Between
captures, main-view geometry motion advances a map of addresses into that
immutable capture. RGB is sampled from the capture only once at display time;
there is no optical-flow search or repeated filtering of captured RGB. Depth,
surface identity and address continuity reject uncertain samples. Only rejected
pixels use the current pre-LUT main image. Cold start and unsupported render
modes retain the v143 path.

Run from the engine source root on Windows with Visual Studio 2022 BuildTools
and the Windows SDK:

```bat
tools\tests\run_pip_motion_map_gpu_test.cmd
tools\tests\run_pip_motion_map_gpu_test.cmd --warp
tools\tests\run_pip_motion_map_gpu_test.cmd --lens-only
tools\tests\run_pip_motion_map_gpu_test.cmd --lens-only --warp
py -3 _build/v144-validation/shaders/compile_shaders.py all
```

The final 2026-09-19 run is recorded in
`_build/v144-validation/final_gpu_validation.json`, with input/output SHA-256
hashes and unchanged source hashes after all four successful runs. Detailed
results are in `final_map_hardware.log`, `final_map_warp.log`,
`final_lens_hardware.log` and `final_lens_warp.log` in the same directory.
Both D3D11 hardware (NVIDIA GeForce RTX 5070) and WARP passed:

- 336 cases using the actual r3/r4 `screenspace_mvectors.h` producer, including
  FP16 velocity, HUD/reactive/unknown masks and adjacent exact surface IDs.
- Production capture-depth export and motion-map reconstruction against an
  analytical world fixture. Five capture-time fractions cover camera rotation,
  translation and moving geometry; maximum hardware seed error is 0.006573
  capture pixels, or 0.007392 with projection jitter and FOV changes.
- Eight successive main-frame updates retain 25,054 of 25,454 tested interior
  addresses with maximum hardware error 0.040978 capture pixels. Invalid pixels
  are rejected rather than borrowing a different surface. Coplanar disocclusion
  at 0/1/2 cm separations exposes 1,920 pixels per case with zero wrong-surface
  addresses; depth, owner and address seams produce zero invalid blends.
- A 32-update stress case samples immutable RGB with maximum error 0.000513 and
  gradient-energy ratio 1.002830. Valid coverage falls to 17,953/22,736 interior
  pixels by update 32; this is a stress result, beyond the MCM maximum interval
  of 8, and does not imply every pixel preserves captured detail indefinitely.
- The actual lens helper matches the independent expected samples for world,
  dynamic and rejected pixels at sharpness 0/1/2 and resolution 100/50/25%.
  All nine combinations have zero reported RGB error, correct main crop/jitter
  and exact v143 parity when the map is disabled. Sharpness 1 remains neutral.

The final shader summaries under `_build/v144-validation/shaders` contain 16
successful compilations: eight r3/r4 standard/precise lens variants with and
without MSAA, two capture shaders, two vertex shaders and four motion/depth
shaders. The lens variants retain exactly their existing 18/22 warning
diagnostics; no new warnings were introduced. The other eight compile with
`/WX` and zero warnings.

Hardware timestamp queries measure isolated passes, not game FPS. With
1920x1080 main/capture inputs, the full 1920x1080 map propagation median is
0.184736 ms (p95 0.185856 ms). For a 512x512 lens draw with 1920x1080 source/map,
neutral sharpness costs 0.012416 ms with all map samples valid, versus
0.003424 ms for the preserved v143 helper; one-third fallback costs 0.010272 ms.
WARP validates correctness only and reports no GPU performance claim.

Integration review places the map update before `phase_3DSSReticle`, which
overwrites depth/motion MRTs. The existing `s_prev_frame` binding reads
`$user$generic_temp`, copied from the current main scene immediately before
reticle drawing and before LUT. Capture depth/owner and map targets remain
outside the swapped PiP target bank; ping-pong aliases are detached before
writes. Draw metadata uses scoped restoration. The release plan puts both
owner-producer headers in Runtime v135 at MO2 index 3 and checks prospective
winners; actual installed winners require the release installer verification.

These checks do not establish in-game FPS or complete perceived smoothness.
Hidden captures still delay presentation, same-owner self-occlusion and
nonlinear animation remain approximations, and rejected regions temporarily
use lower-detail main-view samples. Gameplay boot, save/load, long-session
stability and user-scene performance remain runtime QA.

## Historical v143 stable reuse

The v141/v142 optical-flow reconstruction is retired after the user's real
scene showed wavy textures and severe FPS loss. v143 production uses one
camera transform and captured color; independently moving objects update only
on a new capture. The frame interval is authoritative during ordinary movement.

```text
tools/tests/run_pip_temporal_cost_gpu_test.cmd gamedata/shaders/r3
py -3 tools/tests/svp_capture_refresh_test.py
py -3 tools/tests/v143_svp_pipeline_check.py
py -3 _build/v143-validation/shaders/compile_shaders.py lens
py -3 _build/v143-validation/shaders/compile_shaders.py capture
```

The scheduler executable is built by `_build/v143-validation/run_schedule.cmd`.
New coverage executes the real scheduler and extracted camera-refresh C++,
checks the capture/postprocessing branch, and measures production lens sampling
with hardware timestamp queries. It checks static-frame invariance despite
changing false motion, analytic yaw/pitch projection, and sharpness 0/1/2 at
100/50/25% render resolution. Timings cover isolated passes, not game FPS.

## Historical v142 optical-flow regressions

The following tests validate the retired prototype. Pass the preserved v142
release shader directory as the optional argument when running them; their
motion/depth expectations do not apply to the v143 production helper.

Run from the engine source root on Windows with the configured Visual Studio
2022 BuildTools and Windows SDK:

```bat
tools\tests\run_pip_flow_gpu_test.cmd
tools\tests\run_pip_block_artifact_gpu_test.cmd
```

Both compile C++ with `/W4 /WX` and execute the production HLSL using D3D11 WARP.
Outputs live in `_build/v142-validation`. An optional argument selects another
directory containing `anthology_pip_temporal.h`, for comparisons with a saved
helper. The original depth, flow, sharpness and ideal-field cases remain in the
first executable. The second reuses its D3D11 fixture and exercises:

- Pre-LUT capture identity and complete reduced-resolution resampling.
- Textured stationary walls, quarter-resolution confidence/sentinel boundaries,
  rejected nonuniform vectors, small depth noise and different source radiometry.
- Textured moving surfaces, static detail and foreground/background silhouettes.
- Actual production pyramid/flow output with nonuniform vectors and confidence;
  per-pixel outliers and grid-edge percentiles supplement aggregate image error.
- A physical 83-degree world/20-degree lens crop, shifted center and jitter.
- NVG, thermal and HDR exclusion of incompatible current-main color.

On failure, coordinate-encoded textures reveal the actual production helper's
selected source tap without reproducing its candidate algorithm in test code.
The motion/depth selection is independent of the encoded RGB values.

The v141 helper changed half of a stationary texture when the confidence field
alternated between valid and unmatched patches. Its maximum RGB seam error was
0.091536. The corrected helper preserves those pixels. A 0.0002 tolerance is used
only when floating-point perspective division or arbitrary linear filtering
exercises D3D11's finite subtexel precision; exact identity cases retain stricter
tolerances. These bounds are below one 8-bit color step.

The deliberately ambiguous diagonal-motion case has a revised contract. The
previous global 30% image-error improvement relied on unmatched pixels importing
current-main RGB, which caused the reported blocks. The new checks require a
30% improvement where source and destination flow independently agree with the
known motion, globally non-increasing error, and exact captured color wherever
all four flow taps are rejected. All other existing motion requirements remain.
For the final v142 helper, diagonal global RMSE improves from 0.109652 to 0.093699
(14.55%), valid-coverage RMSE from 0.102976 to 0.018075 (82.45%), and all 3,140
fully rejected pixels retain their captured values.

These offscreen tests do not measure game FPS, perceived motion smoothness,
driver performance or complete game postprocessing. Engine stage selection and
world-versus-HUD projection bindings have separate integration checks.
