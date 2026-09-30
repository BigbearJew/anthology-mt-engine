# v168 snow and texture maintenance

Thin snow previously blended an underlying screen-color copy into its albedo.
Use opaque snow albedo and discard effectively empty coverage instead. Write the
geometry stencil mask explicitly. Drop the color copy and retain the position
alias view until its camera bank changes; keep the private snow VB/IB.

Dead City's detail models use `ccon\cconv1.5`, which has no authored seasonal DDS.
Keep ordinary, ATOC and SSS grass on their seasonal material with the original
atlas fallback. This preserves UV/alpha and enables winter recoloring.

Throttle idle texture scans to 250 ms, or 16 ms under memory pressure. Existing
per-scan limits, grace periods and resident PDA maps are unchanged.
`-texture_profile` reports scan CPU cost; `-legacy_texture_scan` enables the old
cadence for comparison. Local measurements reduced the scan cost from roughly
0.056 to 0.004 ms/frame. This does not explain or resolve the reported twofold
RX 7800 XT regression. No per-frame DDS encoder was found in the DX11 path.

Validation: both DX11 builds, four FXC variants (VS/PS plain/MSAA4), existing
texture residency checks, a production-function regression for missing grass
atlases, in-game snow/PiP and Dead City save/load checks. Existing snow limitations
remain: local transient visual deformation, unchanged level collision, no sole
detail or separate snow-layer shadow pass. Full evidence is in project work-v168.
