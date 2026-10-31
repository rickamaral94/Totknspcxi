#!/bin/bash
# android/push_game.sh <game folder with exefs/ and romfs/>: the player's own game files onto the device over adb,
# set up as the start screen's setup would (docs/las/ANDROID-PORT.md, Fase 2; until the app has a folder picker):
# <data root>/extracted/{exefs,romfs} (skipped when a copy of them finished), romfs.bin built on the device
# (android/tools/romfs_tool.c, no third copy of 16 GB through USB), and the game code from android/build_gamelib.sh
# (android/out).
# ROMFS_HOST=1: romfs.bin built on the computer instead (android/build_romfs.sh) and pushed as one file, with only the
# files the runtime opens by path loose in romfs/ (the movies: runtime/ui/launcher.mm keep_loose) - for TOTK's romfs,
# 304k files that adb copies slowly.
set -euo pipefail
export MSYS_NO_PATHCONV=1  # (Git Bash: leave the device paths alone)
# (or the packages: android/push_game.sh --nsp base.nsp [--update update.nsp] --keys prod.keys [--title-keys ...] /
# --xci game.xci ...: imported first by android/nximport_dump.sh, docs/IMPORT-NSP-XCI.md)
if [ "${1:-}" != "${1#--}" ]; then SRC=$("$(dirname "$0")/nximport_dump.sh" "$@"); fi
cd "$(dirname "$0")/.."
R=$PWD
SRC=${SRC:-${1:?usage: android/push_game.sh <folder with exefs/ and romfs/> | --nsp/--xci ... --keys prod.keys}}
# (GAME_DEST / GAMELIB_OUT: another game version in its own data root, files/v<version> - android/run_ver.sh)
D=${GAME_DEST:-/sdcard/Android/data/com.totkrecompandroid/files/extracted}
LIB=${GAMELIB_OUT:-android/out}
DEV_TMP=/data/local/tmp/totkrecompandroid

SDK=${ANDROID_HOME:-${LOCALAPPDATA:-$HOME}/Android/Sdk}
SDK=${SDK//\\//}
NDK=${ANDROID_NDK:-$SDK/ndk/30.0.16248370}
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) HOST=windows-x86_64; EXE=.exe ;; Darwin) HOST=darwin-x86_64; EXE= ;; *) HOST=linux-x86_64; EXE= ;; esac
CC=$NDK/toolchains/llvm/prebuilt/$HOST/bin/clang$EXE

[ -z "${ROMFS_HOST:-}" ] || bash android/build_romfs.sh "$SRC"  # (first: no device needed for it)

adb shell mkdir -p "$D" "$DEV_TMP"
# A part counts as there only once a copy finished with as many files as the source (.<part>.pushed): a copy cut short
# (TOTK's romfs lost the USB connection once) is copied again instead of becoming a romfs.bin with files missing
parts="exefs romfs"
[ -z "${ROMFS_HOST:-}" ] || parts=exefs
for part in $parts; do
    if ! adb shell test -f "$D/.$part.pushed"; then
        echo "copying $part..."
        adb push "$SRC/$part" "$D/"
        want=$(find "$SRC/$part" -type f | wc -l)
        got=$(adb shell "find '$D/$part' -type f | wc -l" | tr -d '\r ')
        [ "$got" = "$want" ] || { echo "$part: $got files on the device, $want here: run this again"; exit 1; }
        [ "$part" != romfs ] || adb shell rm -f "$D/romfs.bin" "$D/.romfs.bin.built"  # (made from the copy before)
        adb shell touch "$D/.$part.pushed"
    fi
done

# (romfs.bin.part, renamed when complete: an interrupted build or copy doesn't leave a romfs.bin that looks done)
if [ -n "${ROMFS_HOST:-}" ]; then
    if ! adb shell test -f "$D/.romfs.bin.built"; then
        echo "copying romfs.bin..."
        adb push "$SRC/romfs.bin" "$D/romfs.bin.part"
        adb shell mv "$D/romfs.bin.part" "$D/romfs.bin"
        adb shell touch "$D/.romfs.bin.built"
    fi
    if ! adb shell test -f "$D/.movies.pushed"; then
        echo "copying the movies..."
        # (a romfs/ copy that didn't finish, from before: only its movies would ever be read)
        adb shell test -f "$D/.romfs.pushed" || adb shell rm -rf "$D/romfs"
        (cd "$SRC/romfs" && find . -type f \( -path './Movie/*' -o -name '*.webm' \) | sed 's|^\./||') |
            while read -r f; do
                adb shell mkdir -p "$D/romfs/$(dirname "$f")" </dev/null
                adb push "$SRC/romfs/$f" "$D/romfs/$f" </dev/null >/dev/null
            done
        adb shell touch "$D/.movies.pushed"
    fi
elif ! adb shell test -f "$D/.romfs.bin.built"; then
    mkdir -p android/out
    "$CC" --target=aarch64-linux-android30 -O2 -Iruntime -o android/out/romfs_tool android/tools/romfs_tool.c runtime/romfs_build.c
    adb push android/out/romfs_tool "$DEV_TMP/" >/dev/null
    adb shell chmod 755 "$DEV_TMP/romfs_tool"
    echo "building romfs.bin on the device..."
    adb shell "$DEV_TMP/romfs_tool" "$D/romfs" "$D/romfs.bin.part"
    adb shell mv "$D/romfs.bin.part" "$D/romfs.bin"
    adb shell touch "$D/.romfs.bin.built"
fi

if [ -f "$LIB/totkgame.so" ]; then
    adb push "$LIB/totkgame.so" "$LIB/totkgame.so.id" "$D/"
else
    echo "no $LIB/totkgame.so yet (android/build_gamelib.sh)"
fi
# what adb pushes belongs to the shell user (rwxrws---): readable by the app only once others may read it (the app's
# own setup makes these files as the app)
adb shell chmod -R o+rX "$D"
adb shell ls -la "$D"
