# Amaral.16 — TOTK 1.4.2 timing and vertex upload growth

## Evidence and scope

The supplied NSO identifies main as `5cb42b1cf25469fb0635fd046453d843c18bc8ab` (1.4.2).
The available full amaral.14 diagnosis on the Odin 2 Portal reports 28.75 real FPS
with Frame rate 30, render CPU time 21.69 ms/frame and 18.25 ms/frame running on
the presenting guest thread. These measurements overlap across threads and
must not be added. GPU busy attribution averages 8.43 ms per gameplay interval,
with nested timestamp spans, while selected detailed GPU captures are 27.75–38.20
ms and perturb timing. This is not evidence for a guaranteed GPU budget below
16.67 ms. Unlocking a 30 FPS cap alone cannot establish sustainable 60 FPS.

In a representative two-second upload window, vertex misses copy 1297.50 MiB
cold and 593.04 MiB growing, and the transient ring allocates 10583.94 MiB.
Those are bytes allocated/copied over the window, not resident VRAM.

## Changes

- Fifteen guarded generator patches for **1.4.2 only**: seven controller sites
  permitting interval 1, five world-clock sites (ratio and interval), the
  interval selector, the equipment cooldown comparison and the bounce clock.
- Runtime and Video settings now allow 1.4.2 to select Auto or experimental 60.
  Native execution remains 1.4.3-only by default. Existing 30 FPS settings remain
  unchanged. The clock update still runs on real game presents only.
- Equipment timer at main+0x47e5bc uses zero rather than FLT_EPSILON as the
  threshold. Its load is player+0x109c; the equipment gate at 0x34b454 compares
  the same field with zero. The positive branch at 0x480a90 invokes the timer
  update at 0x536b6c, whose tail computes value += ratio * rate.
- Bounce clock at main+0x2e9ad4 uses x8 (not 1.4.3's x9). It compares the time
  object's inverse step times its ratio, removing FPS from the threshold.
  The original compare against 100 branches to vector scaling at 0x2e9e2c.
- Vertex-copy growth reserves geometrically only once a key actually grows,
  up to 1 MiB. Subsequent requests within that reservation copy only the newly
  reached tail. No speculative guest reads; prior draw prefixes remain intact.
  Invalidation and submission changes still discard the whole snapshot.
  The `[vertex-growth]` log reports tail appends and avoided prefix bytes.

Clock/controller policies reuse this repository's existing 1.4.3 implementations.
The PC timing references and MIT attribution are in PC-TIMING-AMARAL15.md and
PC-TIMING-LICENSE.md. The community independently documents 1.4.2's two clock
readers and controller entry in seedds/nxnative PR #793; offsets and original
instructions here were checked directly against the supplied NSO. No code from
that project was copied. Original game assets/code are not included in the APK
or committed to this repository.

## Rebuild and installation

Application ID remains `com.totkrecompandroid`. VersionCode 19, versionName
`0.2.1-beta-amaral.16`; sign only with the existing Amaral key, certificate SHA256
`dfd0653e2a1df81b42a0381e39f4251ac82001a0f8bd8bfa7070157f4c36f1e3`.
Install as an update. Do not uninstall or clear application data.

The generator changes intentionally change GAMELIB_ID to `5e8124d5d005ada0`.
The APK replaces assets/gen/tools/patches.py and recalculates assets/gen.id, so
Android unpacks the updated builder. The user must rebuild game code once from
their existing game files. The old compiled library is not accepted as if it
contained these fixes. Saves and game version folder layout are unchanged.

After the build, select Video > Frame rate > 60 fps for the uncapped experiment.
Keep frame generation **off** for the first comparison against .14 so real FPS
can be assessed. Framegen 2x 30->60 intentionally caps real FPS at 30. A 60->120
mode, if used later, requires its existing display/presentation support and
additional GPU headroom. Neither generated frames nor a target imply measured
real performance.

## Verification and remaining limits

- Full Android runtime compiled with NDK r30, Release -O2.
- Every 1.4.2 patch asserted the original instruction(s) in the supplied main;
  deliberate wrong instructions are rejected. Production lifted patch C was
  compiled and executed for the world step, 30/60 policy, cooldown and bounce.
- All five affected guest functions were generated with --dyn-base / ARM64 SIMD
  and compiled with the Android compiler at -O1, as used by the on-device build.
- Guest-copy tests cover growth, immutable prefixes, collision eviction, overlap
  invalidation, 5000 invalidation/refill cycles, changed submission and flush.
  ASan/UBSan pass (LeakSanitizer disabled for the managed /proc restriction).
- Existing arena merge regression, native FG selection, native timing handlers
  and long-run clock tests pass.
- Package verification requires exactly four modified entries: libmain.so,
  AndroidManifest.xml, assets/gen/tools/patches.py and assets/gen.id. All other
  assets, Java code and libraries remain byte-identical to .15.

**No Odin gameplay/performance validation has been performed for .16.**
Cutscene markers specific to 1.4.3 were not guessed for 1.4.2. Movies retain
existing runtime protection, but in-engine cutscenes may require selecting 30.
Cloth/hair, other high-FPS physics and scene-size flicker are not fully ported
or validated. 60 FPS remains experimental and is not enabled by default.
The .14 arena correctness fix is retained; no driver or forced LRZ change.
