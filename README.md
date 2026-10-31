# TOTKRecompAndroid

**English** | [Português](README.pt-BR.md)

A static recompilation of *The Legend of Zelda: Tears of the Kingdom* (Nintendo Switch, version **1.4.3**) for
Android, in its first steps. This is **not emulation**: the game's ARM64 code becomes C compiled for your phone, and its
graphics (NVN) are translated to Vulkan.

It is built on the engine of **[LAS Recomp](https://github.com/peiikai/LASRecomp)** by peiikai (a recompilation of
*Link's Awakening*), its Android port **[LASRecompAndroid](https://github.com/mamcjr/LASRecompAndroid)** and
**[BOTWRecompAndroid](https://github.com/mamcjr/BOTWRecompAndroid)**, which this repository was forked from.

> [!WARNING]
> **Early.** On the phone (with the Turnip driver) the game loads a save in the prologue's cave and Link can be moved,
> at about 30 fps; little beyond that has been played. The plan and the state of each phase are in
> [docs/TOTK-PORT.md](docs/TOTK-PORT.md) (in Portuguese).

## Supported platform

Android only (arm64, Vulkan). Windows, Linux or macOS may be used as development hosts with the Android SDK/NDK.

## Status

- The app (start screen, settings, touch controls, controllers, mods, Vulkan driver choice) comes from
  BOTWRecompAndroid and builds.
- 1.4.3 runs on Nintendo SDK 20.5.22, the line of BOTW 1.8.2's 20.5.6 (NVN 55): what the engine learned from BOTW 1.8.2
  applies. The game's patches and much of what is known about it come from
  [MacNativeGames](https://github.com/ly05010419/MacNativeGames), which runs this very build natively on Macs and iPhones.
- The game's code generates and builds: 228,238 functions from `main` and 40,516 from the SDK, in a 224 MB
  `totkgame.so`.
- On the phone (POCO F7 Ultra, the Turnip driver): the title screen, then a save loaded in the prologue's cave with
  Link under control and the HUD and hints in Portuguese, at about 30 fps walking around (Turnip). The scene is held at 1600x896 (the game's
  dynamic resolution gave black frames at each step) and the world steps by real time. Character voices, music and
  ambience reach the mix (Opus decoding and the audio renderer's splitters; checked by recording the output on 1.4.2,
  the port's previous version). With Qualcomm's own driver it plays too, at about 15 fps.
- Not done yet: movies (not checked), widescreen, 60 fps, performance beyond the cave.

> [!IMPORTANT]
> Like LAS Recomp, this project is made almost entirely with AI (Claude, by Anthropic, through Claude Code), directed,
> tested and reviewed by a human.

> [!NOTE]
> **No game files are included.** You need your own legally obtained *Tears of the Kingdom* with the 1.4.3 update:
> an extracted folder (`exefs` and `romfs`, update applied), or the base NSP/XCI and the 1.4.3 update NSP dumped from
> your own console together with that console's keys (see [Importing from NSP/XCI](#importing-from-nspxci)).
> Instructions on obtaining game files or keys are not provided.

## Importing from NSP/XCI

The app (and the `nximport` tool on a PC) can set the game up straight from the base game's NSP or XCI and the 1.4.3
update NSP (`0100F2C0115B6800`, v720896), using the `prod.keys` (and, when a package has no ticket, `title.keys`) from
**your own console**. No keys come with this project, and none are downloaded. The base game alone is 1.0.0, which this
app doesn't run: the update is required.

- **In the app:** **Game Files > Import from NSP/XCI > Game package** and **Update**, then **prod.keys** / **title.keys**,
  then **Import**. The previous setup stays playable until the new one is complete and checked. **Remember the keys**
  keeps them in the app's private storage, excluded from backups and device transfer; **Forget the keys** deletes
  them. **Verify packages** checks each NCA against its SHA-256 in the package's metadata, useful when an import
  reports damaged data.
- **On a PC:** `tools/nximport` builds a CLI that writes `Dump/0100F2C0115B6000/{exefs,romfs}`;
  `android/build_gamelib.sh` and `android/push_game.sh` accept `--nsp/--xci … --update … --keys … [--title-keys …]`.
- Every block read is checked against the hashes in the package (and the update's patched RomFS against the update's
  own hashes), so a successful import is the game's data exactly. NSZ/XCZ are not supported.

Details, formats and tests: [docs/IMPORT-NSP-XCI.md](docs/IMPORT-NSP-XCI.md). The importer is BOTWRecompAndroid's,
with TOTK's title IDs and version.

## Building (developers)

With the Android SDK / NDK and your dump in `Dump/0100F2C0115B6000/` (`exefs/` and `romfs/`, or the RomFS image
`romfs.bin`; the folder is ignored by git), with Python and the `lz4`, `capstone` and `unicorn` packages
(`tools/nsp_extract.py` unpacks NSPs and their title keys for hactoolnet):

```bash
cd tools && python totk_dump.py                              # check the dump (modules, version, romfs)
cd android/totkrecompandroid && ./gradlew assembleDebug      # the app
android/build_gamelib.sh Dump/0100F2C0115B6000/exefs         # the game code (~20 min on a PC)
ROMFS_HOST=1 android/push_game.sh Dump/0100F2C0115B6000      # game files and game code onto the phone
android/run_boot.sh 120                                      # start the game, log to android/out/boot.log
```

## Credits

- [peiikai](https://github.com/peiikai) for [LAS Recomp](https://github.com/peiikai/LASRecomp), the recompiler and
  runtime this project starts from (MIT, see [LICENSE](LICENSE)). Its original README is kept in [docs/las/](docs/las/).
- [MacNativeGames](https://github.com/ly05010419/MacNativeGames) (MIT), which runs Tears of the Kingdom 1.4.3 natively on
  Macs and iPhones: its verified 1.4.3 patch sites (the fixed resolution, the real-time frame step, the cloth and hair
  clock, the render size) and its findings (NVN's early-Z flag and block height rounding, depth reads and the D16
  shadow compare, the visibility counter).
- [LASRecompAndroid](https://github.com/mamcjr/LASRecompAndroid), its Android port, and
  [BOTWRecompAndroid](https://github.com/mamcjr/BOTWRecompAndroid) (its README in [docs/botw/](docs/botw/)): the app,
  the phone-side game code build, touch controls, mods, driver compatibility, the Portuguese menus, the Sheikah look and
  everything the engine learned from BOTW 1.8.2's SDK.
- PKMSVRecompAndroid (a sibling port of *Pokémon Scarlet*, not published): the version handling for a game with one
  version, the program id from the NPDM and the 16-byte exclusive pairs in the recompiler.
- [Revivedx](https://github.com/Revivedx) for [SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp) (MIT):
  the SDK 20 kernel fixes, setjmp, NVN 55.
- [Nycolazs](https://github.com/Nycolazs) for LASRecomp-Android-Plus (MIT): Vulkan driver compatibility.
- The [zeldaret/botw](https://github.com/zeldaret/botw) decompilation and [open-ead/sead](https://github.com/open-ead/sead),
  for what is known about the engine TOTK grew from.
- [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) (Apache 2.0, AES and SHA-256 for the NSP/XCI import) and
  [hactool](https://github.com/SciresM/hactool) by SciresM (ISC), the reference for the Switch package formats (no code
  taken) and for `tools/nximport/verify.sh`.
- [lsfg-vk](https://lsfg-vk.dev) 1.0.0 by PancakeTAS (MIT) through [GameNative/lsfg-vk-android](https://github.com/GameNative/lsfg-vk-android)
  (MIT), with [DXVK](https://github.com/doitsujin/dxvk)'s dxbc (zlib) and [pe-parse](https://github.com/trailofbits/pe-parse) (MIT):
  the frame generation's shaders, translated on the phone from the user's own Lossless.dll (none included).
- Upscaling and anti-aliasing: [AMD FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) (MIT),
  [Qualcomm Snapdragon GSR](https://github.com/SnapdragonStudios/snapdragon-gsr) (BSD-3-Clause),
  [SMAA](https://github.com/iryoku/smaa) (MIT).
- Libraries: [SDL](https://libsdl.org) (zlib), [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
  [libadrenotools](https://github.com/bylaws/libadrenotools) (BSD-2-Clause), [volk](https://github.com/zeux/volk) (MIT),
  [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (MIT),
  [shaderc](https://github.com/google/shaderc) (Apache 2.0), [FFmpeg](https://ffmpeg.org) (LGPL),
  [LZ4](https://github.com/lz4/lz4) (BSD-2-Clause), [Capstone](https://www.capstone-engine.org) (BSD-3-Clause),
  [Unicorn](https://www.unicorn-engine.org) (GPLv2, built from its source by `android/deps/build_unicorn.sh`),
  [Python](https://www.python.org) (PSF) and [Zig](https://ziglang.org) (MIT) for the phone-side build.
- [Mesa](https://www.mesa3d.org)'s Turnip driver (MIT), which players load themselves.
- Fonts: [Baloo 2](https://github.com/EkType/Baloo2) (OFL), DejaVu Sans (Bitstream Vera license). The Android robot
  is reproduced or modified from work created and shared by Google (CC BY 3.0).
- Made with [Claude Code](https://claude.com/claude-code) (Anthropic).

*The Legend of Zelda* and *Tears of the Kingdom* are trademarks of Nintendo. This project is not affiliated with or
endorsed by Nintendo.
