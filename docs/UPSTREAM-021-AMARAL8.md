# Upstream0.2.1-beta — Amaral.8

Backport of mamcjr/TOTKDroid commit309f1e0e80f7d2be8dd70e6078fb0a6c79a76656 and version release58754913ed8456f11810b52a95e85a30b9504e36, from baseline6b785c23292d3fcb793b911622230b782ddb0f61. All nine changed upstream paths accounted for; app version adapted to0.2.1-beta-amaral.8/code11 rather than upstream code4.

## Imported

Cached GPU arena with explicit flush/invalidate for non-coherent memory; cached readbacks and readiness calls; Mali driver gating;32-entry GMap tables;48MB resident arena with deferred reclaim; learned vs storing range separation and bounded table assembly; pending writeback cleanup on reset; transient chunk retirement; BC1-to-ASTC encoder and upload path. No external Turnip binary changed.

## Preserved and resolved

NSP/XCI import, game1.0.0 SDK-layout fix, game1.4.2 path and recompiler, framegen, telemetry and driver diagnostics remain local. One Android pipeline worker retained. Our chunk-comparison writeback helper remains, with upstream ga_invalidate inserted before it. This was the only exec.c rejected hunk. New upstream BC1 files and test normalized to their exact upstream blob hashes. Signing remains the fixed Amaral key; release signing has no debug fallback.

Corrected effect-trace filter ignores blend bits of unused attachments, tiny auxiliary targets and depth-only passes; trace adds early-Z flags. Conditional-render draws/clears have a separate suppression count. Arena logs expose table size, stored/live/learned regions, resident bytes and upstream fallback counts (counters may reset at upstream diagnostic reporting points).

## Verification

Native arm64 Release build with NDK30/CMake3.30.4. Source-extracted range builder under ASan/UBSan:1000learned/store fixtures +32bound entries under pressure. Existing5000writeback-equivalence tests, effect-capture limit/filter tests and driver counter tests passed. BC1 conversion test100000blocks, zeroillegalblocks, noalphaerrors, maximum channel difference2/255. GAMELIB_ID remains9588e7371082fde1. APK uses unchanged amaral.3 Java/resources/assets; tools/repack_amaral8.py verifies only libmain.so and manifest version changed. Signature/alignment/CRC verified by signing tool. This is a native-runtime backport, not a full Gradle assembleRelease.

No physical-device validation yet. Upstream's torch fix was seen on a POCO simulating Mali, not proof of the cause on Adreno740. First phone run: same area/options as last LRZ-off test, framegenoff,30FPS,1x. Keep LRZoff for an isolated comparison, then export60smeasurement and shortvideo. No uninstall required over amaral.5–7.
