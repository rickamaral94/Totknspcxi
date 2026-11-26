# Render thread: where its time goes and what to cut (2026-11-14)

The game's NVN command lists are translated to Vulkan on one render thread (`runtime/nvn/vulkan/async.c`, `exec.c`).
On the POCO F7 Ultra (Adreno 830, Turnip) it is what holds TOTK under 60 fps on the surface. This is a measured plan
for making it cheaper: a fresh profile, the options asked about (descriptor update templates, push descriptors, more
than one translating thread), what the profile showed besides them, and an order.

**Update 2026-11-15**: the user chose items 1-3 of the order below. They were built and measured on the phone, see
"Results of items 1-3" at the end. Two changes stayed:
- `bcopy_pending` asks the GPU once a submission: −0.75 ms a frame, +2.4 fps;
- the sampled checks of static textures run on a helper thread: −0.38 ms a frame.

Together they make −1.1 ms (~7% of the render thread). The other three tries gained nothing and were taken out.

## How it was measured

- **Scene**: the surface, from a copy of the 100% save `files/play100-11132032b` (the area of the user's clock tests),
  no input after loading. About **3370 draws, 178 passes, 25 dispatches and 9.4 submissions a frame**, ~1600 textures.
  The Sky Island profile of the same day had ~500 draws a frame. The save's start-of-game cave (`seed-100surf-1113`)
  is a new game: not this scene. About 95 s in, something in this save holds the game at 30 fps (a cutscene's hold),
  so the measuring window is ~45-95 s.
- **Forced 60** (`LARECOMP_SETTINGS=.../files/f60.ini`). There the scene ran **50-60 fps**, falling as the phone warmed
  (59 → 48 fps over three minutes in one run).
- **Uncapped** (`LARECOMP_PRESENT_WAIT=0` - no frame limiter - plus `LARECOMP_VK_PRESENT=mailbox`): the menus ran up to
  267 fps, the scene the same 50-60. The scene is below 60 already, so forced 60 shows a gain in fps too; near 60 the
  render thread's time per frame shows it when the fps can't.
- **CPU profile**: simpleperf at 2000 Hz for 20 s, frame-pointer call chains, report with `rt_analyze.py` (the
  session's scratchpad): the render thread ("Thread-10") was **86% of a core** (17.3 s of 20), 29.6% of the process's
  samples. The Turnip build (`Turnip_Gen8_V37`) is stripped: its time is attributed to the libmain call site that
  entered it. DWARF call chains don't get through it either (no unwind tables reachable on the device).
- **Comparisons**: in-run A/B (`LARECOMP_AB=<frames>:<key>`) with `LARECOMP_LOG_PACE=1`, whose `render` field is the
  render thread's work between two presents. Back-to-back launches drift with heat; alternating blocks in one launch
  don't. `LARECOMP_VK_PROF` is not used: its clock reads cost ~10 fps.
- **Driver call timing**: `LARECOMP_DRV_TIMES=1` (new, `exec.c`): time and calls per frame in the driver calls around
  submissions, every 120 frames ("[drvt]").

## The profile (render thread = 100%)

By where the samples were: **libmain 51%, libc 25%, Turnip 23%**. ~5 µs of render thread per draw.

| What | Share | Notes |
|---|---|---|
| Turnip, entered from `draw_` | 14.9% | `vkCmdDrawIndexed` 7.6, `vkCmdBindPipeline` 2.6 (+3.8 of libc memcpy inside it), `vkCmdBindDescriptorSets` 2.1 (+1.1 libc) |
| Turnip, other | 7.7% | `vkCmdBeginRendering` 2.1 (`begin_pass`), `vkCmdEndRendering` 1.3, `vkUpdateDescriptorSets` 1.4, pool resets 1.1 |
| `vkGetSemaphoreCounterValue` → `ioctl` | **~11%** | ~3000 calls a frame from `bcopy_pending` in `guest_copy`: 1.7 ms a frame. **Fixed** (below) |
| `stage_set` (own code) | 12.5% | the binding loops, `cbuf_info` inlined, the set cache's hashing; spread thin, no single hot line |
| `draw_` (own code) | 13.9% | `draw_writes` 3.0 (the first touch of the `Prog`: a cache miss), the pipeline key's hash 0.8, the fragment shader hash 0.8 |
| Texture checks | ~9-12% | `texture_info` → `vk_view` → `vk_tex` → `upload_texture`: the first use of each texture in a frame samples its memory (`tex_hash_sample` ~3%) or hashes it whole (`tex_hash_full` 3.1%) |
| Guest copies (memcpy) | ~3-4% | constant buffers and vertices copied per submission (`guest_copy`, `vk_transient_copy`) |
| `begin_pass` | 7.0% incl. | 178 passes a frame; mostly the driver |
| Present, ImGui overlay | ~4% | |

Driver calls around submissions (`[drvt]`, per frame): `vkQueueSubmit2` 0.23 ms (9.4 calls, 24 µs each),
`vkResetDescriptorPool` 0.14 ms, `vkAllocateDescriptorSets` 0.17 ms (1443 calls), command pool reset / begin / end
0.04 ms. Submissions are cheap; the `ioctl` time was the semaphore queries.

## Done here: the GPU's progress asked once a submission (`bcopy_pending`)

`guest_copy` asks whether its range is still being written by a GPU buffer copy (`gpu_buffer_copy`'s small
`CopyBufferToBuffer`s). It asked the driver for the GPU's progress on every call while any such copy was pending:
an `ioctl` on Turnip / KGSL, ~0.7 µs, ~3000 times a frame. Now it asks once a submission. A stale answer only keeps a
copy that already landed bound in place in the heap, which holds the same bytes. `gpu_buffer_copy` itself still
asks every time: a stale answer there would send the copy down the CPU path, which waits for the GPU.

| | Render thread a frame | fps (60 cap) |
|---|---|---|
| Asked every call (A/B side 0) | 15.95 ms | 58.4 |
| Once a submission (side 1) | **15.27 ms** | 58.5 |

7 of 7 alternating pairs (150 frames each) favour side 1, by 0.68 ms on average (−4.3%). A second run, at 300 frames,
showed −0.4 ms at 60 and −0.8 ms where the game sat at 30. `LARECOMP_AB=<frames>:bcpoll` keeps the A/B. A first
attempt that skipped the query only when no pending copy overlapped the range changed nothing: the ranges do overlap
(the copies go into constant buffers the next draws read).

## The options

Gains are in render thread time, against the ~16-17 ms a frame of this scene.

### (a) Descriptor update templates

`vkUpdateDescriptorSetWithTemplate` replaces only `vkUpdateDescriptorSets`, which runs on the set cache's misses
(12% of the sets): **1.4%** of the render thread in all. A template saves part of the driver's loop over the writes,
and `stage_set` still builds the infos to hash them. **Expected ≤ 0.2 ms a frame (≤ 1%)**. Low risk, ~1 day. The
profile already bounds it, so no prototype was built. Worth it only alongside something else that touches `stage_set`.

### (b) Push descriptors (`VK_KHR_push_descriptor`)

Push descriptors would remove the allocation (0.17 ms), the writes on misses (0.24 ms) and the set cache's hashing,
but they push every descriptor of both sets on **every** draw. Today the driver writes them for one draw in eight (88%
hits). Turnip's push path writes them into the command stream, much like an update, so its cost would grow about
eightfold: ~1.4% → ~10%, for ~3-4% saved. Two more problems:
- the dynamic uniform buffers that made the sets shareable aren't allowed in a push set (their offsets would go into
  each push);
- a fragment set reaches 64 textures plus ~20 buffers, past Turnip's 32-descriptor push limit.

**Expected: a loss.** Not recommended. A hybrid (the per-draw uniform buffers pushed, the textures in cached sets:
three sets instead of two) might break even; it is medium effort for an uncertain gain.

### (c) More than one translating thread

The subagent's hazard analysis (notes folder: `render-thread-option-c-hazards.md`) found the following.
- **Whole lists in parallel: not feasible.**
  - `st` is never reset between lists (`st_defaults` runs only at init and reset).
  - `render_off`, the snapshots, the aliases, the texture ownership (`gpu_owned`, which decides whether `vk_tex`
    uploads from guest memory) and the per-submission caches all carry from one list to the next.
  - Submissions happen mid-list (`begin_pass` chunking, `split_long_pass`).
  - The commands themselves are ordered on the CPU side:
    - `UPDATE_UNIFORM_BUFFER` writes guest memory that later draws copy;
    - `COPY_BUFFER_TO_BUFFER` waits for the GPU and copies;
    - `REPORT_COUNTER` writes counters that `SET_RENDER_ENABLE_COND` reads back;
    - `BIND_TEXTURES_DEFERRED` reads guest handles when run.
- **Feasible: moving only the Vulkan recording off the thread.** A serial front-end keeps everything up to resolved
  descriptors, copies, uploads and pass decisions, and emits a packet per draw: pipeline, dynamic state, the sets'
  writes and offsets, vertex and index buffers, the draw. Two shapes:
  - **secondary command buffers per pass on workers**, stitched at `vk_end_pass` (the subagent's design: in-pass
    clears and timestamps must move into the secondaries, per-worker descriptor and command pools, per-worker set
    caches that hit less; 12-18 days);
  - **one in-order replay thread** (DXVK's CS thread) recording the primary itself. No secondary-buffer rules, one
    set cache, ordering kept for free. The front-end hands over packets and waits for the replay only where it must
    record itself (outside passes) or submit. Roughly 8-12 days.

  What moves is Turnip's `vkCmd*` and descriptor writes: ~20-23% of the render thread (draw 7.6, pipeline bind 6.4
  with its memcpy, set binds 3.2, rendering begin/end ~3.4, updates 1.4, the dynamic state calls). Packets cost
  ~0.2 µs a draw (~0.7 ms a frame).
- **Expected: −2.5 to −3.5 ms a frame (15-20%)**, if the replay thread keeps up and the game's job threads leave a
  core for it.
- **Risk: high**:
  - object lifetimes while packets are in flight (`pipe_evict`, `vk_collect_dead`, view eviction);
  - the many diagnostics in `draw_` that read live state;
  - the mid-pass restarts (`rt_dirty`, `pass_dup_null`);
  - one more busy thread on a phone that throttles.

  Worth doing only after the cheaper items below have shrunk the front-end, since the front-end stays serial.

### (d) What else the profile shows

1. **Fewer submissions: ruled out.** Batching lists into one submission was prototyped (`listbatch`) and never fired:
   600 of 600 lists carry a fence (or event), and when a list ends the next one isn't queued yet (the render thread
   keeps pace with the game list by list). Submissions cost only 0.37 ms a frame anyway, pool resets included.
2. **Texture checks (~9-12%)**: the first use of each texture in a frame runs the upload check.
   - **How it can be cut**: run the checks for the textures the last frame used on a helper thread at the frame's
     start (the hash helpers of `99735c5` exist), or check unchanged static textures less often.
   - **Risk**: a CPU update seen late. The comments in `textures.c` record such a case: the glyph cache that lost
     letters when sampled.
   - **Expected**: −1 to −1.5 ms a frame. 2-4 days.
3. **Pipeline binds (6.4%)**: ~1000 binds a frame, and Turnip copies state on each one (the memcpy under it).
   `ss_forget` at every pass start (178 a frame) rebinds a pipeline that didn't change. Count the redundant ones
   first (a counter, an hour); dropping them is cheap if they are many. **Expected: −0.3 to −0.8 ms**.
4. **The `Prog` cache miss** (`draw_writes` 3%, plus the loads in `stage_set`): prefetch the program at
   `CMD_BIND_PROGRAM`, or gather the hot fields (`fs_out_mask`, the descriptor plans) into a small struct.
   **Expected: −0.3 to −0.5 ms**. ~1 day.
5. **Hashes per draw** (the pipeline key, the fragment shader: 1.6%): small. Folded into item 4's work.
6. **Constant buffers read in place** (`LARECOMP_UBO_DIRECT`, off by default): it would remove most of the guest copies'
   memcpy (~3-4%). It is off because the sync lag lets the CPU write the next frame's values into them. Not pursued.

## Recommended order

| # | What | Expected (render thread a frame) | Effort | Risk |
|---|---|---|---|---|
| 1 | `bcopy_pending` once a submission | **−0.68 ms, measured** (−0.75 ms in the 2026-11-15 run) | done | low |
| 2 | Redundant pipeline binds; `Prog` hot fields / prefetch | −0.6 to −1.3 ms | 1-2 days | low |
| 3 | Texture checks on a helper thread / less often | −1 to −1.5 ms | 2-4 days | medium |
| 4 | (c) replay thread for the Vulkan recording | −2.5 to −3.5 ms | 8-12 days | high |
| - | (a) templates | ≤ −0.2 ms | 1 day | low: only with other `stage_set` work |
| - | (b) push descriptors | a loss | - | not recommended |

Items 1-3 together would take the scene from ~16-17 ms to ~13.5-14.5 ms of render thread work: 60 fps on the
surface with headroom, before the GPU becomes the limit. Item 4 is the large step after that, if the GPU allows it.

## Results of items 1-3 (2026-11-15)

Each change had its own `LARECOMP_AB` key. Every run is one launch: the surface save copy, forced 60, blocks of 150
frames alternating, the render thread's time a frame from `[pace]`, and only the blocks above 40 fps. "Pairs" counts
the alternating pairs where side 1 came out lower.

| Change | Key | Side 0 → side 1 | Pairs | Kept |
|---|---|---|---|---|
| The GPU's progress asked once a submission (`bcopy_pending`, final version) | `bcpoll` | 16.63 → **15.88 ms**, 56.3 → **58.7 fps** | 7 of 7 | **yes**, on |
| Static textures' sampled checks on a helper thread (`textures.c` tex_check) | `texhelper` | 15.87 → **15.49 ms** | 7 of 7 | **yes**, on |
| State kept across passes (no `ss_forget` at each `begin_pass`) | `sskeep` | 16.22 → 16.42 ms | mixed | no |
| The `Prog` and its `ShaderInfo`s prefetched at `CMD_BIND_PROGRAM` | `pfprog` | 16.37 → 16.28 ms | mixed | no |
| Prefetch in `tex_hash_sample` (with the helper on) | `texpf` | 16.11 → 16.38 ms | mixed | no |

- **Why keeping the state gained nothing.** A counter of binds that repeat what the command buffer already had found
  ~35 pipeline binds a frame (of ~900), and no set binds at all. A pass's first draw almost always changes the
  pipeline anyway, and the sets' dynamic offsets change on every draw. Forgetting the state at a pass costs nothing
  worth saving.
- **Why the prefetches gained nothing.** The out-of-order cores already overlap those loads, or they come too late to
  help. The `draw_writes` samples in the profile are probably the first touch of state that only looks hot.
- **The texture helper.** It takes 55-80 checks a frame off the render thread; none found a change in this scene.
  - A texture whose sample differs is marked, and its next use checks it on the render thread at once. A change
    shows up at most one frame later than before. Static textures were already checked only every 8th frame, at their
    turn.
  - Destroying a texture waits for the helper to be done with it.
  - `LARECOMP_TEX_CHECK_THREAD=0` puts the checks back on the render thread.

Together, the render thread does about **−1.1 ms a frame (~7%)** less in this scene. The remaining plan, in order:
1. **The texture checks' other half**: the whole hashes of dynamic textures (~2.3%) and the per-texture view lookups
   (`texture_info` → `vk_view`, ~3%). Dynamic textures must be checked as they are when their list runs, so they
   can't simply move to a helper.
2. **(c), the replay thread for the Vulkan recording**: the large step (−2.5 to −3.5 ms), with high risk.

## On top of main, with BOTWRecomp's GPU-progress changes (2026-11-15, night)

The branch was rebased onto main (`488e559` with BOTW's `1885c8e`: the GPU progress read from a waiter thread, on low
RAM) and BOTW's `b8a1d05` was ported on top. That port has the waiter wait on a fence per submission, makes it the
default for any Turnip, and no longer signals the timeline.
- **Everything against main's behaviour** (`LARECOMP_SEQ_THREAD=0,LARECOMP_TEX_CHECK_THREAD=0,LARECOMP_BCOPY_ASK_EVERY=1`),
  alternated launches:
  - **at forced 30**: the render thread **22.58 → 21.50 / 21.43 ms** a frame, 2 of 2 pairs (28 fps both: capped);
  - **at forced 60**: launch order and heat decided the fps (the first launch, cool, held 60), so the in-run A/Bs
    above are the numbers for 60.
- **The fence waiter on the POCO**, alternated launches against `LARECOMP_SEQ_THREAD=0` (46-64 s window): frame period
  17.72 / 18.48 / 17.37 ms against 17.61 / 18.21 / 17.31. It costs **~0.15 ms (~0.5 fps)**, 3 of 3 pairs: the game
  thread waits a little longer for its fences. It stays on, as in BOTW, for the weak Turnip phones (Tab A9+:
  +25% fps there).
- **The whole hash split from 256 KB** instead of 2 MB (`hashmin`): slightly worse, 5 of 6 launches. Dropped.
- **What the texture checks do a frame** (`LARECOMP_DRV_TIMES`): ~123 upload checks; whole hashes 7.3 under 256 KB,
  1.4 to 2 MB and 1.3 larger (the glyph cache), 14 MB in all; snapshot compares of dynamic textures up to 1 MB.
  - **The snapshot table thrashes.** Each 2 s, ~235-360 compares found the slot taken against ~310-350 hits; a miss
    is a 1 MB copy plus a whole hash.
  - **Tried: 32 slots** by a hash of the texture, instead of 8 by address bits (`snap32`). The misses were true
    collisions (none from a re-upload), and with 32 slots they fell from ~120-230 to 0-48 every 2 s. But those
    textures are tiny (~0.06 MB a frame), and the render thread didn't gain (15.31 → 15.63 ms, worse in 3 of 4 pairs).
    Dropped.

So the texture checks have little left that is cheap. The big remaining item is (c): moving the Vulkan recording to a
replay thread. It needs the user's go-ahead, because of its size and risk.

## (c) The replay thread: first stages (2026-11-15, morning; no gain measured, off by default)

The user gave the go-ahead. `runtime/nvn/vulkan/replay.h` / `replay.c`, off by default (`LARECOMP_VK_REPLAY=1`;
`LARECOMP_AB=<frames>:replay` alternates it between submissions; `LARECOMP_VK_REPLAY_LOG=1`: packets, the replay thread's
time per packet type, the render thread's waits for the ring and where they come from).

**How it works.**
- `draw_` writes the draw's commands into a single-producer ring of packets instead of calling Turnip: the pipeline,
  the dynamic state, the descriptor sets with their offsets, the vertex and index buffers, the draw.
- The same goes for the commands between passes (end of rendering, the full barrier, begin of rendering with the
  attachments copied), compute dispatches (barrier, binds, dispatch), and the write-back copies at the end of a pass.
- A "vk replay" thread calls the driver for them, in order.
- **Order is kept by construction.** `vk_rp_install` puts a check in front of every volk `vkCmd*` pointer the
  runtime and ImGui use. A command into the buffer the packets go to first waits until the ring is empty (so do
  `vk_cmd()`, the command pool calls, `vk_async_drain`, and the heap re-import).
- So any recording not converted to packets stays correct, and only costs a wait.
- The upload buffer gets its own command pool, since one pool's buffers may not record on two threads.
- An Opus review of the ring, the encoding and the lifetimes found two use-after-free risks, both fixed: the heap
  buffer destroyed on growth while queued binds still named it, and `vk_async_drain` not waiting for the ring.

**What was measured on the POCO** (surface save copy, forced 60):
- **Stable, and the image looks right**, in every run.
- **First version (draws only): worse** (15.46 → 18.90 ms render).
  - The render thread waited for the ring at every pass end (~150 a frame) and at every wake-up of the replay thread.
  - The replay thread slept after 50 µs idle, and a `sched_yield` in the wait handed the render thread's core to a
    game thread for a whole slice.
  - Fixed:
    - pass begin / end / barrier, dispatches and write-backs became packets;
    - no `sched_yield`;
    - the replay thread spins 2 ms before sleeping;
    - one publish of the ring position per draw instead of per packet;
    - the free space is cached.
- **With those fixes: −0.19 ms** (15.39 → 15.20 ms, 6 of 6 pairs, fps the same at the cap).
  - A later A/B was noisy: the phone was heating, and its battery was running out.
  - About 23 waits a frame are left, ~2.6 ms in all: depth clears inside passes (`clear()`, ~11 a frame, ~1.3 ms),
    the mid-frame submissions (`begin_pass` chunking, ~4 a frame), the end of each submission.
- **The profile says more than the `render` field.**
  - With replay on, the render thread is on its core ~10 ms a frame, against ~15 ms without (simpleperf, 20 s).
  - The `render` field counts its waits too (at a held 30 fps it reads ~30 ms), so at the 60 cap it can't show a
    thread that got faster.
  - The replay thread spends ~5 ms a frame in Turnip, against ~4.3 ms when the render thread did it. Per-packet
    timing exaggerates that: its own clock reads cost ~1 ms.
- **Not measured yet, and needed**: an uncapped A/B with the phone charged.
  - The last two runs ran with the battery at 1%: the CPU was capped at 1.4 GHz and the game sat at ~28 fps on both
    sides. They are discarded.
  - The question is whether the ~5 ms the render thread no longer spends turns into fps, or whether the GPU and the
    game's threads hold the frame at the cap.
  - Also whether the spinning core costs fps through the phone's power budget: some blocks hinted at it.

**With the phone charged (100%, on the charger; 08:48-09:08)**: no gain, capped or uncapped. Three runs of 170 s on the
surface seed, `LARECOMP_AB=300:replay`, the first 2700 frames skipped:

| Run | Off (side 0) | Replay (side 1) |
|---|---|---|
| forced 60 (`rp_ab8`) | 46.8 fps, render 19.35 ms | 46.5 fps, render 19.10 ms |
| uncapped (`rp_unc2`: `PRESENT_WAIT=0`, mailbox) | 42.7 fps, render 21.50 ms | 42.1 fps, render 21.27 ms |

- The pairs split both ways. Replay saves ~0.25 ms of `render` and gives no fps.
- **Heat is the limit in this scene.** The SoC reached ~93 °C within a minute of each run, with the prime cores
  capped at 1.8-2.0 GHz. It dropped to 43 °C within minutes after.
  - Hot, the frame doesn't follow the render thread's own work: the replay thread takes ~11.5 ms a frame of Turnip
    recording off it, yet `render` stays ~20 ms on both sides.
  - Splitting the work over two cores costs power: encoding plus decoding, the 2 ms spin, ~24 waits a frame
    (~2.7 ms). Under a power cap, that power is what the frame lacks.
- **An event in the seed holds 30 fps.** A cutscene mark from ~50 s (`[totk] cutscene: presents held at 30 fps`,
  never "over") held the first run (`rp_ab7`) at 30 fps from ~40 s. That is the "event from ~95 s" seen before. A/Bs
  on this seed need `LARECOMP_CUTSCENE_FPS30=0`.
- What that leaves for (c): the next stages would only shave waits (~2.7 ms a frame) from a thread that, hot, isn't
  what holds the frame. The ~5 ms of render-thread CPU it saves would show only on a cool phone, or on one whose
  render thread holds the frame at its clocks.

**Next, in order** (1 done: no gain):
1. ~~Uncapped and capped A/Bs with the phone charged.~~
2. Clears inside passes as packets (`vkCmdClearAttachments`, the clear draw's binds).
3. The submission itself on the replay thread (end, submit), so the render thread stops waiting at each of the
   ~10 submissions a frame.
4. Then decide the default. It stays off until a measured gain.

## Files

- `runtime/nvn/vulkan/exec.c`: `bcopy_pending` / `gpu_buffer_copy` (the fix), `LARECOMP_DRV_TIMES` (the probe).
- `runtime/nvn/vulkan/textures.c`: the texture check helper (`tcheck_*`, `tex_check_forget`); `vk.h`: `check_suspect`.
- `runtime/nvn/vulkan/replay.h` / `replay.c`: the replay thread (the ring, the packets, the checks in front of volk's
  pointers); `exec.c`: `vk_cmd_rp`, the `rpCmd*` calls in `draw_`, `begin_pass`, `vk_end_pass`, `dispatch_compute`,
  `wb_queue`; the GPU waits by cause in `[drvt]`.
- Measurements and scripts (copied to the sessions' notes folder, `render-thread-scripts/`, with the profile report): `rt_spot.sh` (a run from a save copy, with simpleperf),
  `rt_analyze.py` (the render thread by leaf, caller and line), `ab_sum.py` (A/B blocks from `[pace]` lines).
