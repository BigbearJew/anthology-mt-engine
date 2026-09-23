# PiP motion ownership and history

`IRenderable` owns one transient, immutable identity for its lifetime. The fixed
pool has 13,312 concurrent IDs, encoded as exact negative normal FP16 values
strictly between -0.5 and zero. It allocates no heap memory. Constructors and
destructors lock the pool; draw calls only read the stored float.

Released slots form a bounded FIFO. Every rendered main view advances the serial
before `seqRender`; hidden PiP views do not. Successful texture publication sets
the oldest retained boundary to the preceding main serial N. A released ID with
release serial strictly below N cannot occur in either the main-N endpoint or
the new capture. It can be reused without invalidating history. Reuse at/after N
increments the atomic owner generation, which the renderer checks against the
capture and previous map. This avoids periodic identity collisions and avoids
invalidating every reconstruction after the first 13,312 allocations.

`pip_motion_history` is an optional float4 constant: unknown motion (0/2), owner,
reserved, reserved. Packets set it after binding their constant table, including
packets reusing that table. Static world uses owner0. Dynamic owners use their
negative ID; a missing owner or exhausted pool uses2. Untracked rigid main draws
have unknown2. Hardware skeleton draws override only unknown based on adjacent
main history; software skinning remains unknown. A child scope restores the
parent constant so a raw sibling cannot inherit a skeleton's valid history.
Hidden captures need current ownership without previous-pose validity. Outside
active temporal PiP both fields are zero.

The shader preserves motion XY and HUD Z. Native positive masks remain positive;
unknown2 takes precedence. Only an otherwise zero mask receives the owner ID.
The capture/map shaders normalize static world to+1, preserve negative IDs, and
reject unknown/nonfinite owners. Reconstruction is gated to non-MSAA rendering;
identities must never pass through an MSAA average or linear owner sampling.

## Active provider and compatibility audit

Selected MO2 profile: `Anthology 2.1 HARD Сложный`. Highest enabled provider is
`[GFX] ScreenSpaceShaders Update 23.5 — Обновление шейдеров/gamedata/shaders/r3/screenspace_mvectors.h`.
No enabled r4 provider exists. Original SHA256:
`b4770c1d4bcb70c31260c1a82a8b9cdf2b853921ac5031638547eff7726c464e`.
The original file has mixed CRLF/LF; unchanged bytes and existing function
semantics are preserved. Both authored renderer copies are identical.

The selected overlay contains259 shader sources. The raw-motion consumer audit
identified13 files. Native TAA saturates raw W, so negative IDs remain mask0.
Its other alpha read is the prepared velocity accumulator, whose preparation
reads raw XY only. AO tests W<-0.5, outside the ID range. Motion blur tests
W>=1.5, so IDs preserve blur behavior and unknown2 intentionally suppresses
unreliable motion. SSS, IL, SSR and combine consume only XY/Z; remaining declared
motion resources have no W use. Detailed source paths, hashes and expressions
are in `active_motion_consumers.json`. Proprietary vendor internals are outside
this local shader-source audit.

An older winning helper lacks these semantics while still exporting motion XY.
The engine must gate map reconstruction on the actual selected helper capability;
checking only whether `screenspace_mvectors.h` exists is insufficient.

## Validation

- `run_render_surface_owner_test.cmd`: MSVC C++17 /W4 /WX; 40,312 checks, including
  all FP16 IDs, concurrent live uniqueness, exhaustion, FIFO reuse, strict and
  monotonic boundaries, ring wrap, and nested packet/child constant restoration.
- `pip_motion_marker_check.py`: source integration ordering plus24 geometry
  shader variants on shader models4/5 (48 original/overlay compilations), using
  actual winning MO2 files over established fallback includes. All pass with no
  added warnings. Includes SKIN_NONE/0/1/2/3/4, bump/flat/alpha and MSAA variants.
- Artifacts: `_build/pip-motion-validation/render_surface_owner_test.exe` and
  `_build/pip-motion-validation/marker/{compile_summary.json,active_providers.json,active_motion_consumers.json}`.

These checks do not substitute for game runtime or GPU integration validation.
No authored files were installed into MO2 by this task.
