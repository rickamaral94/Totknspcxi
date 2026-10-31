#!/bin/bash
# android/build_gamelib.sh <exefs folder>: totkgame.so for Android from the player's own game files - the game code
# generated with gen.py --dyn-base (into generated-android-dyn/, skipped when it's there; delete it to redo) and built
# with the NDK against the app's libmain.so (build the app first). Output: android/out/totkgame.so and totkgame.so.id,
# for <data root>/extracted/ on the device (android/push_game.sh), where the app picks them up.
set -euo pipefail
# (or the packages: android/build_gamelib.sh --nsp base.nsp [--update update.nsp] --keys prod.keys [--title-keys ...]
# / --xci game.xci ...: imported first by android/nximport_dump.sh, docs/IMPORT-NSP-XCI.md)
if [ "${1:-}" != "${1#--}" ]; then EXEFS=$("$(dirname "$0")/nximport_dump.sh" "$@")/exefs; fi
cd "$(dirname "$0")/.."
R=$PWD
EXEFS=${EXEFS:-${1:?usage: android/build_gamelib.sh <exefs folder (main, sdk, ...)> | --nsp/--xci ... --keys prod.keys}}
# (GAMELIB_GEN / GAMELIB_OUT: another game version beside the usual one, e.g. generated-android-dyn-<version>)
GEN=${GAMELIB_GEN:-$R/generated-android-dyn}
OUT=${GAMELIB_OUT:-$R/android/out}
W=$OUT/gamelib-build

SDK=${ANDROID_HOME:-${LOCALAPPDATA:-$HOME}/Android/Sdk}
SDK=${SDK//\\//}
NDK=${ANDROID_NDK:-$SDK/ndk/30.0.16248370}
CMAKE_DIR=$SDK/cmake/3.30.4/bin
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=.exe; PY=tools/venv/Scripts/python.exe ;; *) EXE=; PY=tools/venv/bin/python ;; esac

LIBMAIN=$(ls -t "$R"/android/totkrecompandroid/app/build/intermediates/cxx/*/*/obj/arm64-v8a/libmain.so 2>/dev/null | head -1 || true)
[ -n "$LIBMAIN" ] || { echo "build the app first (android/totkrecompandroid: gradlew assembleDebug)"; exit 1; }

if [ ! -f "$GEN/main/main_table.c" ] || [ ! -f "$GEN/sdk/sdk_table.c" ]; then
    (cd tools && "../$PY" gen.py sdk "$EXEFS/sdk" 0x7004000000 "$GEN/sdk" --hle ../runtime/hle/hle_symbols.txt --simd asm --dyn-base)
    (cd tools && "../$PY" gen.py main "$EXEFS/main" 0x7000000000 "$GEN/main" --simd asm --dyn-base)
fi

# the files whose largest function has over 15000 labels: compiled at -O0 (android/gamelib/CMakeLists.txt, heavy.txt).
# (BOTW 1.6.0: 4 files, 15k-34k labels - each took clang hours at -O1/-O2; the largest under it, 14.5k, compiled normally)
(cd "$GEN" && for f in main/*.c sdk/*.c; do
    awk -v f="$f" '/^[a-zA-Z].* f_[0-9a-f]+\(.*\) \{$/ {n = 0} /^L_[0-9a-f]+:/ {if (++n > 15000) {print f; exit}}' "$f"
done) > "$GEN/heavy.txt"
echo "heavy (at -O0): $(tr '\n' ' ' < "$GEN/heavy.txt")"

"$CMAKE_DIR/cmake$EXE" -S "$R/android/gamelib" -B "$W" -G Ninja -DCMAKE_MAKE_PROGRAM="$CMAKE_DIR/ninja$EXE" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 \
    -DCMAKE_BUILD_TYPE=Release -DLAS_GEN="$GEN" -DLAS_LIBMAIN="$LIBMAIN"
"$CMAKE_DIR/cmake$EXE" --build "$W" -j"$(nproc 2>/dev/null || echo 8)"
mkdir -p "$OUT"
cp "$W/totkgame.so" "$W/totkgame.so.id" "$OUT/"
# (debug info off: ~580 MB -> ~110 MB to push and load; the symbols stay, for simpleperf and crash reports)
"$NDK/toolchains/llvm/prebuilt/$(ls "$NDK/toolchains/llvm/prebuilt" | head -1)/bin/llvm-strip$EXE" --strip-debug "$OUT/totkgame.so"
ls -la "$OUT/totkgame.so" "$OUT/totkgame.so.id"
