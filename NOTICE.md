# Licenses of TOTKDroid

- **TOTKDroid's own code is MIT** ([LICENSE](LICENSE)), as are LAS Recomp's, LASRecompAndroid's and
  BOTWRecompAndroid's, which it builds on.
- **Frame generation (Totknspcxi, from BOTWRecompAndroid):** `runtime/nvn/vulkan/framegen.c` (MIT) drives lsfg-vk's
  framegen from [`third_party/lsfg-vk-android`](third_party/lsfg-vk-android) (MIT, Copyright (c) 2025 lsfg-vk), built as
  its own `liblsfg_framegen.so`. Lossless.dll's shaders are translated with pe-parse and DXVK's dxbc
  (`android/totkrecompandroid/app/src/main/cpp/lsfg_deps.cmake`). Upstream TOTKDroid's GPL-3.0-or-later `lsfg/` folder
  is not in this fork (amaral.26), so the APK stays MIT as a whole.
- **Lossless Scaling's shaders are never included.** The app reads them at run time from the player's own copy of
  `Lossless.dll` (Lossless Scaling, proprietary, sold on Steam), picked under Video > Frame generation: only the
  shaders translated from it are kept, in the app's private storage. The DLL is not part of this repository or of any
  APK.
- **Third-party code** keeps its own license, listed in [README.md](README.md#credits).
- **Ideas and layouts from [totk-android](https://github.com/AurelioB/totk-android)** (AurelioB, MIT): the HD rumble
  mixing in `runtime/hle/input.c` (two bands to one strength, the silence threshold, the 250 ms stale value) follows its
  `runtime/rumble_mix.h` and `runtime/hid_feedback.c`. The 3D reverb and the auxiliary buffer in
  `runtime/services/audio_reverb3d.inc` are adapted from its `games/totk/runtime/audio_reverb.c` and
  `audio_mixer.c` (Copyright (c) 2026 MacNativeGames contributors, MIT), whose reverb network is based on
  Ryujinx.Audio's Reverb3d (Copyright (c) Ryujinx Team and Contributors, MIT). The software keyboard in
  `runtime/hle/swkbd.c` (the argument layout, the UTF-16 / UTF-8 answer, the disabled keys and the cancel result)
  follows its `games/totk/runtime/swkbd_host.c` (Copyright (c) 2026 MacNativeGames contributors, MIT). The optional
  rendering reductions in `runtime/native.c` (shadow map size, the game's FXAA, the render far plane) use the sites
  its `games/totk/runtime/game_patches_v143.c` found (same copyright, MIT).
- **From [Savage Wind Recomp](https://github.com/Revivedx/SavageWindRecomp)** (MIT, Copyright (c) 2026 Revivedx and the
  Savage Wind Recomp contributors; Copyright (c) 2026 Peiikai), its `mali-fixes` branch (StonedEdge): the low bits of
  16 / 8-bit integer colour targets on Mali (`rt_kind_key` in `runtime/nvn/vulkan/exec.c`, the narrow outputs in
  `runtime/nvn/maxwell/translate.c`; its 9950b9e, through BOTWRecompAndroid 86ac04d), the pass barriers held to
  fragment work (`frag_barrier_on`, its 8defa9b), the colour targets a pass doesn't draw into left out of it
  (`det_tab`, its 087f2c5), and the GPU's load and clock through devfreq in `runtime/ui/perf_overlay.mm` (its 9dadb24).
