# PiP actual motion-vector map contract

Production shaders: `svp_motion_depth.ps`, `svp_motion_map.ps`, and independent
sampling header `anthology_pip_motion_sample.h`. These shaders do not estimate
optical flow and the map pass never reads or resamples captured RGB.

## Resources and ordering

1. At successful ordinary PiP scene capture, write matching immutable captured
   RGB, R32F capture depth and R16F owner with `svp_motion_depth.ps`. Depth can remain at the
   actual capture render dimensions even when RGB is blitted to display size;
   their complete normalized UV domains and capture epochs must agree. Inputs are
   `Texture2D<float4> s_pip_capture_position`, the capture's G-buffer with view-Z
   in `.z`, and `Texture2D<float4> s_pip_capture_motion`, its same-sized raw motion
   buffer. MRT0 stores depth; MRT1 stores normalized owner. Invalid depth/unknown
   owner is stored as zero. UV spans the complete source.
2. Before each main reticle, write the next ping-pong RGBA32F map with
   `svp_motion_map.ps`, using existing `svp_temporal.vs` fullscreen UV geometry.
3. Lens sampling reads the completed map through the validated sampling header.
   Valid addresses sample immutable captured RGB. Invalid individual pixels can
   use current main color at the same scene/color-processing stage.

Map pass SRVs:

| Name | Type | Meaning |
| --- | --- | --- |
| `s_pip_main_position` | `Texture2D<float4>` | Current main `.z` positive view-Z |
| `s_pip_main_motion` | `Texture2D<float4>` | Current-minus-previous unjittered main UV in `.xy`; HUD mask `.z`; raw owner/reactivity in `.w` |
| `s_pip_previous_map` | `Texture2D<float4>` | Previous main map; `.xy` captured RGB address or `(-1,-1)` when invalid, `.z` previous main depth, `.w` normalized owner |
| `s_pip_capture_depth` | `Texture2D<float>` | Immutable R32F depth matching published captured RGB |
| `s_pip_capture_owner` | `Texture2D<float>` | Immutable R16F normalized owner matching capture depth/RGB |

Map output has the same RGBA32F packing. `.z` and known owner `.w` are retained
for current main surface samples even when `.xy=(-1,-1)` rejects the captured
address. Seed mode uses previous depth and matching owner without requiring a
valid previous address. Therefore no separate full-main depth/owner copy is needed.
Main motion and main position must have identical dimensions and raster epoch.

Normalized owner is `+1` for world/static/flora, or an exactly representable FP16
dynamic owner in `(-.5,-2^-14]`. Zero is unknown/invalid. Raw motion `.w` in this
negative interval is the owner itself. Other finite raw values in `[0,2)` or
below `-.5` map to world owner `+1`, preserving grass/reactivity and old flora
semantics. Nonfinite values, raw `.w>=2`, exactly `-.5`, and the reserved negative
gap near zero are unknown. Seed and advection require exact current/previous
owner agreement; capture owner must also agree. Four-tap interpolation never
mixes owners. This distinguishes a moving actor from a nearly coplanar wall
where depth-only tests accept the wrong surface. Distinct static triangles still
share world owner and depend on geometric/depth validation.

Owner allocation must not wrap into a still-live/captured identity. Reusing an
owner requires a generation change that resets old capture/map history before
the new identity can be accepted. Unknown/exhausted owners must fail closed;
they cannot silently become static world owner.

The map domain is the complete normalized virtual-lens image, with the same
aspect ratio as the captured scene. Initial validation uses actual capture
render dimensions. A 512/1024 cap may save work but affects thin silhouettes,
validity boundaries and map interpolation, even though RGB remains high quality;
benchmark and assess it separately. Capture depth has the published RGB's UV
domain and matching capture epoch. Maps stay outside the swapped PiP RT bank.

## Constants

All vectors are `float4`. Matrix declarations use normal HLSL column-major
storage and `mul(matrix, columnVector)`, matching existing engine shader binding.
`V` is world-to-view, `P` is unjittered world-camera projection. Suffix `0` means
previous presented main, `1` current main, `c` the actual captured camera. Jitter
is normalized top-left-origin UV: `j=(.5*jitterNdcX,-.5*jitterNdcY)`.

| Name | Value |
| --- | --- |
| `pip_current_main` | Current lens-to-unjittered-main scale `.xy`, offset `.zw`: `mainUv=(lensUv-.5)*scale+offset` |
| `pip_previous_main` | Same crop from the previous presented main |
| `pip_main_jitter` | `(j1.x,j1.y,j0.x,j0.y)` |
| `pip_current_ray` | `(1/P1._11,1/P1._22,-P1._31/P1._11,-P1._32/P1._22)` |
| `pip_previous_ray` | Same from `P0` |
| `pip_capture_projection` | `(.5*Pc._11,-.5*Pc._22,.5+.5*Pc._31+jc.x,.5-.5*Pc._32+jc.y)` |
| `pip_current_to_capture` | `float4x4`: `Vc * inverse(V1)` |
| `pip_previous_to_capture` | `float4x4`: `Vc * inverse(V0)` |
| `pip_motion_control` | `(mode,previousDepthMetadataValid,epochFraction,deltaMainSeconds)` |
| `pip_motion_limits` | `(absoluteDepthToleranceMeters,relativeDepthTolerance,maxWorldPointSpeedMps,maxMapStretchFactor)` |

Suggested initial limits are `(.02,.005,64,4)`; these are rejection bounds, not
estimates of the real point velocity. Initial GPU fixtures exercise the actual
values and boundary failure behavior.

Mode `0` records depth and owner without a captured address. Mode `1` seeds a new capture epoch. Mode `2`
propagates addresses from the same capture epoch. Seed fraction is
`(tc-t0)/(t1-t0)` in `[0,1]`. A missing previous map/metadata, invalid fraction or
main delta outside `(0,.25]` rejects addresses while retaining current depth/owner.
CPU ownership must enforce matching epochs for mode 2: a texture cannot prove
the epoch of its RGB addresses by itself.

The first lens frame may have no previous main map. Warm up depth-only and keep
v143 lens sampling until a fresh capture can be seeded with coherent previous
main metadata. Do not publish a wholly invalid warmup map as a live-main lens.
Capture changes, size changes, camera cuts, device resets, owner changes and
image-domain changes reset that state. A main map is advanced exactly once per
presented-main serial; hidden SVP ticks do not consume the ping-pong history.

## Lens sampling header

`anthology_pip_motion_sample.h` has no engine/global includes. It declares:

```
Texture2D<float4> s_pip_motion_map;
float4 scope_lense_motion;
float4 pip_sample_motion_map(float2 uv);
```

`scope_lense_motion=(enabled,absoluteDepthTolerance,relativeDepthTolerance,
maxMapSpanTexels)`, initially `(1,.02,.005,4)`. The caller branches on `.x` and
uses the v143 path when disabled, warming up, or unsupported. The function
returns captured address `.xy`, current-main depth `.z` and normalized validity
`.w` (`0..1` capture coverage, not the stored raw owner).
Only compatible valid map taps of the same owner are interpolated. The nearest tap must itself be
valid; less than 75% compatible bilinear weight rejects the sample, avoiding
coordinate snaps at incomplete footprints. Do not subsequently interpolate
validity or map coordinates with a hardware bilinear sample across surfaces.

## Assumptions and validation

Production initially gates to non-MSAA, non-HDR ordinary SDR optics without
head NVG or thermal modes. Protected modes retain v143. Underlying skeleton
history must be initialized and checked on first appearance/visibility gaps.
The pass must run before the reticle overwrites motion/depth and must not sample
an RTV simultaneously. Main crop constants must exclude raster jitter and use
world projection, not `CHudInitializer` projection.

Current motion/depth sampling and previous map gathering reject depth or vector
discontinuities rather than borrowing neighboring foreground vectors. Seed
interpolates corresponding world points into the capture pose epoch, then checks
capture depth and owner. Propagation carries the immutable capture address and
validates owner/local surface continuity. New visibility, missing coverage or
invalid history fails locally to current main color. Distinct static surfaces
sharing the world owner and nonlinear animation still require visual validation.

Required GPU cases: seed fractions `.25/.5/.75`, simultaneous camera and rigid
object motion, changing depth, jitter, capture center/FOV, per-main advection,
invalid previous addresses with valid seed depth, missing previous coverage,
HUD/sky, foreground disocclusion (including coplanar actor/wall), adjacent exact
FP16 owner IDs, invalid epochs, interrupted motion history, static texture
preservation and map sampling across validity/depth/owner/UV seams.
Benchmark actual production PS at the agreed map dimensions before choosing a
smaller map or reduced format. No FPS claim follows from shader compilation.

## v149 edge reconstruction and spatial AA

Ordinary scene capture runs SMAA before publishing RGB. Its early return in
phase_combine bypasses the later display SMAA pass. With main SMAA off, the
compiled spatial-AA preset defaults to High for this always-active PiP pass.
Main SMAA activation, main TAA and the capture cadence are unchanged.

Sampling coverage is smoothstep(.75, 1, compatibleWeight). Callers blend
validated captured colour with current main colour for partial coverage. They
never blend raw addresses/owners across rejected surfaces. Interior coverage
is one; sharpness 1 remains a neutral sample of the resolved capture.

World/foliage uses depth-validated camera reprojection of its captured pose.
Coarse main-view wind vectors must not deform individual magnified leaves.
Their wind animation advances on capture frames. Dynamic owners retain the
per-present skeleton/object advection path.

Rejected world samples can recover finer captured geometry supported by the
current bilinear depth footprint. Empty sky cannot occlude subpixel captured
world branches; current actors, HUD, unknown history and nearer solid occluders
remain protected. These recovered map depths are ray-derived current-view Z,
not a copy of the nearest coarse main G-buffer depth.

## v152 dynamic silhouettes against sky

Sky reconstruction may accept a known dynamic neighbour only when the nearest current texel is sky and all footprint owners are known. Every foreground layer is attempted first. Captured sky retains depth sentinel 9999. When a moving owner uncovers sky absent from the capture, depth sentinel 10001 encodes a proven current main sky texel centre. Consumers sample current colour once without interpolating its address across foreground. Such entries are excluded from surface-depth history and normal captured-colour gathers. HUD, unknown owners and current solid centres remain rejected.
