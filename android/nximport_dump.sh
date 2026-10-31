#!/bin/bash
# android/nximport_dump.sh (--nsp base.nsp | --xci game.xci) [--update update.nsp] --keys prod.keys
#                          [--title-keys title.keys]
# The player's own packages into the extracted dump the other scripts take (tools/nximport, docs/IMPORT-NSP-XCI.md):
# <NXIMPORT_OUT, default <repo>/Dump>/0100F2C0115B6000/{exefs,romfs}, then checked by tools/totk_dump.py (which game
# version it is). Prints the dump folder on stdout, everything else on stderr. Used by build_gamelib.sh and
# push_game.sh when they're given these options instead of a folder.
set -euo pipefail
# (the packages and keys as the caller named them: made absolute before the cd below)
ARGS=()
for a in "$@"; do case "$a" in --*|/*) ARGS+=("$a") ;; *) ARGS+=("$PWD/$a") ;; esac; done
cd "$(dirname "$0")/.."
R=$PWD
OUT=${NXIMPORT_OUT:-$R/Dump}
DUMP=$OUT/0100F2C0115B6000
if [ -e "$DUMP" ]; then
    echo "$DUMP already exists: use it as a folder, delete it, or set NXIMPORT_OUT (e.g. NXIMPORT_OUT=Dump/v143)" >&2
    exit 1
fi

NX=${NXIMPORT:-$R/build/nximport-host/nximport}
if [ ! -x "$NX" ]; then
    echo "building tools/nximport..." >&2
    git -C "$R" submodule update --init third_party/mbedtls >&2
    cmake -S tools/nximport -B build/nximport-host -DCMAKE_BUILD_TYPE=Release -DNXIMPORT_TOOLS=ON >&2
    cmake --build build/nximport-host --target nximport_cli -j"$(nproc 2>/dev/null || echo 4)" >&2
fi
"$NX" "${ARGS[@]}" -o "$OUT" >&2

case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) PY=$R/tools/venv/Scripts/python.exe ;; *) PY=$R/tools/venv/bin/python ;; esac
[ -x "$PY" ] || PY=python3
if "$PY" -c "import lz4.block" 2>/dev/null; then
    (cd tools && "$PY" totk_dump.py "$DUMP") >&2 || { echo "tools/totk_dump.py found problems in $DUMP (above)" >&2; exit 1; }
else
    echo "(tools/totk_dump.py skipped: no Python with lz4. Set up tools/venv and run it on $DUMP to see the game version)" >&2
fi
echo "$DUMP"
