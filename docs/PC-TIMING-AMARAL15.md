# Android timing adaptation — amaral.15

## Sources and scope

Compared `valentderah/totk-pc-recomp` at c38e0ca13d2bceb0590b195f69a010315913850d (`runtime/frame_rate.c`, `runtime/patches.c`, `recomp/patches-v143.txt`) and `kuraeow/ultratears` (`src/host/frame_rate.c`, `tools/lifter/patches.py`). The runtime timing policy is adapted from the MIT PC implementation; see PC-TIMING-LICENSE.md. UltraTears was studied for behavior, without copying its GPL additions.

| Mechanism | Android result |
| --- | --- |
| World clock, cloth/hair time, positive visibility fade increments | Already present; retained |
| Average real frame time over five samples | Retained as default |
| Minimum step applied after averaging, bounded at 1/240 s | Ported; avoids premature rounding of faster samples |
| Long frame capped at 1/15 s; gaps outside 1 ms–250 ms reset history | Ported; retains previous clock step during discontinuities |
| Select last real frame or five-frame smoothing | Added in video settings; five remains default |
| Real 60 FPS with 2x framegen to display 120 | Added as an opt-in experimental Android mode |
| Equipment cooldown epsilon patch | Pending; requires a new generated guest patch |
| High-FPS bounce/step correction | Pending; requires a new generated guest patch |

This is a runtime adaptation, not a claim that every PC patch has been ported. No game generator, guest patch table, or GAMELIB_ID change is included. Imported/compiled game assets remain compatible (`9588e7371082fde1`).

## Using the build

The normal profile remains 30 real FPS → 60 displayed with 2x framegen. For an experiment, select `2x (60 -> 120, experimental)` and use a 120 Hz display. Devices lacking the required presentation support fall back to the existing 30→60 mode. The new mode requests 60 real FPS even if the ordinary FPS selector remains at 30; cutscenes keep the 30 FPS path. Other FG modes retain their previous policy.

Generated images do not advance simulation. Only real game presents update the clock. 60 real FPS requires CPU and GPU work to fit within approximately 16.67 ms; selecting this mode does not make rendering cheaper or guarantee that the Odin can sustain it. Five-sample smoothing is the recommended starting point. The last-frame option is experimental and can make frame-time noise visible.

The amaral.14 GPU arena merge correction is retained. No new Turnip binary or forced driver setting is introduced. Native 120 FPS and complete high-FPS physics correctness are not claimed.

## Verification

Android native library compiled successfully with NDK r30. Production timing helper tested at 15/20/30/40/60/120/240 Hz, in both smoothing modes, including pause, timer rollback, short intervals, maximum step, and post-average minimum clamp. Production FPS selection and settings encoding tested, including unchanged legacy mode values. Arena merge regression test passed. These are host/build checks, not an Odin GPU performance or visual validation.

APK is repackaged from the existing Java/resources with the newly compiled native library; game import assets remain byte-identical. Signed with the fixed Amaral certificate; versionCode 18, versionName 0.2.1-beta-amaral.15.
