# Contributing to TOTKDroid

Thanks for wanting to help! Bug reports, testing on different hardware and code contributions are all welcome.

> [!NOTE]
> This project is developed mostly with AI (see the README). Contributions written by hand, with AI assistance or
> anything in between are equally welcome - what matters is that a change works, is tested, and follows the rules
> below.

## The one hard rule: no Nintendo files

Nothing owned by Nintendo may ever be committed or attached to an issue or pull request:

- no copies of the game in any form
- no dumps (`Dump/`), extracted game files (`extracted/`), generated code (`generated*/`) or fonts (`.bfttf` / Switch
  `.ttf`)
- no game assets of any kind: textures, models, music, text, map files - also not "just one file for testing"

Pull requests that include game files can't be merged. Screenshots and short clips of the game in bug reports are
fine.

## Reporting bugs

Open a [GitHub issue](https://github.com/mamcjr/TOTKRecompAndroid/issues). Please include the Android device, OS version, GPU and driver, what you did, what happened, and logcat output (tag `TOTKRecompAndroid`).

## Building

Android is the only application target. The plan and phase notes are in [docs/TOTK-PORT.md](docs/TOTK-PORT.md)
(Portuguese); BOTWRecompAndroid's, where the engine comes from, in [docs/botw/BOTW-PORT.md](docs/botw/BOTW-PORT.md).
Build the APK with `cd android/totkrecompandroid && ./gradlew assembleDebug` (Windows: `gradlew.bat assembleDebug`).
Check your dump with `cd tools && python totk_dump.py`. Build its game library with
`android/build_gamelib.sh Dump/0100F2C0115B6000/exefs`.

## Project layout

- `tools/gen.py` - the recompiler: translates the game's executables into C (`generated/`, never committed).
  `tools/patches.py` holds code patches applied during generation; every patch site checks the original instruction
  word, so a wrong address fails loudly instead of silently.
- `runtime/` - the host side:
  - `kernel.c`, `loader.c`, `services/` - the Switch OS replacement
  - `hle/` - host implementations of SDK functions (input, movies, display, fonts) and the game hooks (`game.c`)
  - `nvn/` - the graphics layer: command recording (`cmdbuf.c`), the Vulkan (`vulkan/`) backend, the shader translator (`maxwell/translate.c`)
  - `ui/` - the ImGui overlay menu and the start screen
- `android/` - the Android app (Gradle), its dependencies and the game code build for the phone.

## Testing your change

- Build the Android APK. For rendering changes, report the device, Vulkan driver and scene used for testing.
- Test in game, and look at the result: a fix isn't verified until you've seen it work.
- Many debug hooks are documented where they're defined - grep for `getenv("LARECOMP_` (the engine's prefix, kept
  from LAS Recomp). Give test runs a scratch save folder (`LARECOMP_SAVE_DIR`) so your own saves stay untouched.
- State that belongs to one boot of the game must be reset when the game reboots in place: use an `RT_ON_RESET` hook
  (see `runtime/hle.h`).

## Code style

- Match the surrounding code: its naming, structure and comment density. Comments explain *why* (what the game does,
  what an address is, which bug showed the problem), not what the next line does.
- Engine changes that aren't about TOTK should stay easy to bring back to LAS Recomp and
  BOTWRecompAndroid (and the other way round).

## Pull requests

- Keep each pull request focused on one change; small is easier to review.
