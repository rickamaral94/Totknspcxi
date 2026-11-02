# Amaral.7 — targeted effect diagnostics

VersionCode10, versionName0.2.0-beta-amaral.7. Fixed signing key from amaral.5; update over amaral.5 or6. Native Release arm64 runtime rebuilt; Java/resources/assets preserved from amaral.3 with only libmain.so and manifest versions changed. GAMELIB_ID remains9588e7371082fde1.

## Decisions

Amaral.6 no-snapshot-reuse experiment ran (reused=0) but the user reported persistent fire/smoke flicker. Summary27.03realFPS vs29.59in amaral.5, with differing routes/temperatures. Restore snapshot reuse; preserve the explicit environment diagnostic override. No additional gather rounding: glsl_gather already rounds 2D coordinates and array D16 compare references. Eden review in EDEN-TOTK-REVIEW.md. No Turnip change without evidence.

## New evidence

At the existing GPU profiling points within the performance measurement, collect up to3bursts, each with3consecutive frames, max64unique effect-binding combinations per frame. Filter blended or depth-tested/non-depth-writing draws. Log fs hash and code address, vertex shader address, render target ID/dimensions, blend factors, depth state, discard, cbuf mask, sampled texture handles/types/formats/IDs/dimensions/views and actual feedback image vs original image. Up to8textures and4generated texture expressions of384characters per retained row. Cap messages identify omissions. IDs identify candidates, not proof that a shader draws fire/smoke.

No new Vulkan queries, readbacks, waits or GPU data dumps. Logging can perturb those few captured frames; do not use their timings alone for performance claims. Captures are tied to the existing performance measurement; keep the camera facing the fire/smoke during it. New writeback aggregates record bytes checked and bytes skipped because whole chunks were unchanged, retaining256-byte conflict handling.

## Validation

Capture-state tests cover consecutive frames, deduplication and row/session budgets. Writeback tests compare memory+shadow results with legacy behavior over5000cases and verify new telemetry counts. ASan/UBSan run with leak detection disabled due ptrace restriction. Native build, manifest, APK CRC/alignment and fixed certificate verified before delivery. No physical Odin test available. This is a diagnostic update, not a validated flicker fix.

## Phone test

Install over amaral.6; no uninstall. Same Turnip, resolution1x, framegenoff,30FPS, same LRZ/sync lag/AA as previous run. Keep fire/smoke in view while measuring60s, then send exported log and short video. Report whether the whole effect disappears or only its surface/lighting changes.
