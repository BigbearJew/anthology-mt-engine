# PiP capture epoch and main motion-vector accumulation

Design note only. No rendering implementation or performance claim. This describes
the future full PiP route, using actual engine vectors rather than optical flow.

## Coordinate and time contract

- `t0`: previous presented main pose time; `tc`: hidden PiP capture pose time;
  `t1`: current presented main pose time, with `t0 <= tc <= t1` at reset.
- `V0`, `V1`, `Vc` are world-to-view matrices; `P0`, `P1`, `Pc` are their
  unjittered projections. Use actual saved world matrices, never HUD matrices.
- `u` is normalized top-left-origin image UV. `j0`, `j1`, `jc` are raster jitter
  in those UV units: `(0.5 * jitter_ndc.x, -0.5 * jitter_ndc.y)`.
- Main motion at current jittered UV is `v = u1 - u0`, without jitter.
  Depth is positive linear view Z, sampled at jittered UV.
- Let `M_i(l)` map unjittered virtual-lens UV `l` to unjittered main UV at
  presented frame `i`. For the current same-view cameras this is the projection
  crop, including projection-center offsets, not the physical reticle screen UV.
  More generally, construct a lens ray, project it through main projection, and
  divide by W. A translated lens camera requires depth, not a pure 2D crop.

Define `N(u) = (2*u.x - 1, 1 - 2*u.y)`. To reconstruct a world surface point,
unproject `(N(u), clip_z, 1)` by inverse projection, divide by W, normalize the
result to view Z = 1, multiply by linear depth, then transform by inverse view:

```
ray_i(u) = normalize_to_z_one(unproject(P_i, N(u)))
X_i(u, z) = inverse(V_i) * float4(ray_i(u) * z, 1)
project_i(X) = N_inverse((P_i * V_i * X).xy / .w)
```

Choose a finite clip Z for unprojection; ordinary perspective projections also
permit direct reconstruction from their X/Y scale and center coefficients.

## Reset immediately after a new capture

For a current virtual-lens pixel `l1`:

```
u1 = M_1(l1)
q1 = u1 + j1
v  = main_motion_1(q1).xy
u0 = u1 - v
q0 = u0 + j0
X1 = X_1(u1, current_main_depth(q1))
X0 = X_0(u0, previous_main_depth(q0))
a  = (tc - t0) / (t1 - t0)
Xc = lerp(X0, X1, a)
c  = project_c(Xc) + jc
A1(l1) = c
```

Validate the seed against captured positive view Z:
`(Vc * Xc).z` must agree with capture depth at `c`, within justified depth and
raster precision tolerance. The capture-depth texture must describe the same
scene/color capture, dimensions, camera and epoch. Reject invalid seed pixels to
current main color rather than inventing a history coordinate.

This is exact for linear world-space point trajectories and correct visible
correspondences. It fixes the erroneous application of the entire `t0 -> t1`
vector to an image captured at `tc`. It is approximate for rotations, bones,
acceleration, vertex wind and particle deformation. A pose timestamp must refer
to the pose actually rendered; GPU completion time is irrelevant. Existing tree
wind snapshots advance only on main frames, so not every captured subsystem can
currently be assumed to represent the same `tc`.

Do not silently clamp out-of-range `a` to conceal a missed capture/frame epoch.
If `tc == t1`, direct current-world projection is enough and no previous point
is required. Otherwise a missing/mismatched previous pose invalidates the reset.

## Subsequent presented main frames

The map stores an address in the immutable captured RGB, not a previous RGB.
At every main frame, while the capture epoch is unchanged:

```
u1 = M_1(l1)
v  = main_motion_1(u1 + j1).xy
u0 = u1 - v
l0 = inverse(M_0)(u0)
A1(l1) = validated_sample(A0, l0)
color(l1) = captured_rgb(A1(l1))
```

Equivalent raster-coordinate relation:
`q0 = q1 - v + j0 - j1`. Do not add a separate camera warp afterward: the
actual motion already includes camera motion, and the crop transforms account
for the two lens projections. Do not multiply motion by capture age. Ping-pong
the map once per presented-main serial, not once per `Device.dwFrame`, which
also includes hidden capture ticks. A new capture seeds a new epoch; never
propagate addresses belonging to the old RGB into the new capture texture.

Keep validity/provenance with the map. A pixel filled from current main color
has no valid address into captured RGB and cannot later become valid merely by
being advected. It becomes valid after a suitable new capture or an explicitly
validated correspondence.

## Required data and rejection rules

Run before `phase_3DSSReticle`. That pass binds motion as an RTV and writes its
lens/TAA mask; direct simultaneous sampling is invalid. Store dedicated previous
pre-reticle main depth and world matrices/jitter/pose time. Existing
`rt_ssfx_prevPos` is copied after reticle/TAA and is not a reliable underlying
world-depth history for the scope region.

Reject invalid/nonfinite/out-of-bounds UVs, invalid depth, HUD-covered pixels,
unavailable motion, incompatible previous visibility, camera cuts, render-size
or projection changes that invalidate coverage, image-domain changes, and
capture/reset/weapon epochs that do not match the metadata. Initialize skeleton
history on first valid main render and reset it after visibility/pose-history
discontinuities. MSAA depth and motion must refer to the same chosen surface
sample, not independently averaged samples across a silhouette.

Do not use generic bilinear interpolation across map discontinuities. Gather
candidate taps with depth/surface/provenance checks; interpolate only compatible
taps or reject. A UV average between two unrelated surfaces addresses unrelated
captured content and can recreate the earlier texture deformation. Motion alone
is not a surface identity. Depth agreement reduces mistakes but cannot prove
identity for nearby/coplanar surfaces; an object/surface ID would strengthen it.

## Limits and validation targets

Sampling immutable RGB once avoids cumulative RGB blur. Resampling the address
map can still diffuse discontinuities and accumulate geometric error; it does
not guarantee a deformation-free image. Prefer adequate address precision:
FP16 normalized UV can quantize by visible fractions of a pixel at high capture
resolution, so assess RG32F or a local-coordinate encoding. The extra map/depth
storage and passes must be measured on the actual lens area and GPU.

Occluded prior points, newly exposed surfaces and subpixel animated details in
the coarse main buffers cannot be recovered uniquely. Fall back to current main
color with a local detail loss. Nonlinear bone motion can disagree with the next
true capture even when all depth tests pass, producing correction pops. The
current main color and captured RGB must share the same color-processing stage.

Before considering production: validate asynchronous capture fractions 0/0.25/
0.5/0.75/1, constant translation with simultaneous camera rotation, depth motion,
changing jitter/FOV, acceleration and articulated rotation, thin silhouettes,
coplanar surfaces, a newly exposed foreground, missing skeleton history, HUD
coverage and visibility gaps. A constant-translation test must have no extra
full-main-frame displacement on the first frame after capture. An occlusion
test must not borrow background addresses for the foreground. Periodic fresh
captures must replace predictions without cross-epoch coordinate contamination.
