#!/bin/bash
# tools/nximport/verify.sh: nximport's output against hactool's extraction of the same packages, with the player's
# own files and keys (docs/IMPORT-NSP-XCI.md). Compares the list of files, their sizes and their SHA-256s.
#
#   tools/nximport/verify.sh --keys prod.keys [--title-keys title.keys] (--nsp base.nsp | --xci game.xci)
#                            [--update update.nsp] [--work <dir>] [--hactool <path>]
#
# Exit: 0 identical; 1 different (or a step failed); 2 the reference comparison was NOT run (no hactool) - only
# nximport's own checks (its hash trees) ran, which is not a comparison against hactool.
# hactool reads title keys from ~/.switch/title.keys (a content with a rights id needs them there).
# The work folder holds two extractions of the game (2 x ~16 GB): it's deleted at the end unless --keep.
set -euo pipefail
R=$(cd "$(dirname "$0")/../.." && pwd)
KEYS= TITLE_KEYS= BASE= KIND= UPDATE= WORK= HACTOOL=${HACTOOL:-hactool} KEEP=0
while [ $# -gt 0 ]; do
    case "$1" in
    --keys) KEYS=$2; shift ;;
    --title-keys) TITLE_KEYS=$2; shift ;;
    --nsp) BASE=$2 KIND=nsp; shift ;;
    --xci) BASE=$2 KIND=xci; shift ;;
    --update) UPDATE=$2; shift ;;
    --work) WORK=$2; shift ;;
    --hactool) HACTOOL=$2; shift ;;
    --keep) KEEP=1 ;;
    *) sed -n '2,13p' "$0"; exit 1 ;;
    esac
    shift
done
[ -n "$KEYS" ] && [ -n "$BASE" ] || { sed -n '2,13p' "$0"; exit 1; }
WORK=${WORK:-$(mktemp -d "${TMPDIR:-/tmp}/nximport-verify-XXXXXX")}
mkdir -p "$WORK"
[ "$KEEP" = 1 ] || trap 'rm -rf "$WORK"' EXIT

# nximport: a build in build/nximport-host (made here when missing)
NX=${NXIMPORT:-$R/build/nximport-host/nximport}
if [ ! -x "$NX" ]; then
    cmake -S "$R/tools/nximport" -B "$R/build/nximport-host" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$R/build/nximport-host" --target nximport_cli -j"$(nproc 2>/dev/null || echo 4)" >/dev/null
fi
ARGS=(--keys "$KEYS" "--$KIND" "$BASE")
[ -n "$TITLE_KEYS" ] && ARGS+=(--title-keys "$TITLE_KEYS")
[ -n "$UPDATE" ] && ARGS+=(--update "$UPDATE")

echo "== nximport"
"$NX" "${ARGS[@]}" -o "$WORK/nximport"
OURS=$WORK/nximport/01007EF00011E000

# each file of a tree: "path size sha256", sorted
listing() { (cd "$1" && find . -type f -printf '%P\n' | LC_ALL=C sort | while IFS= read -r f; do
    printf '%s %s %s\n' "$f" "$(stat -c %s "$f")" "$(sha256sum "$f" | cut -d' ' -f1)"; done); }

if ! command -v "$HACTOOL" >/dev/null 2>&1; then
    listing "$OURS" > "$WORK/nximport.txt"
    echo "== $(wc -l < "$WORK/nximport.txt") files extracted by nximport (its hash trees checked every byte)"
    echo
    echo "COMPARAÇÃO DE REFERÊNCIA NÃO EXECUTADA: hactool não encontrado (--hactool <caminho> ou HACTOOL=...)."
    echo "REFERENCE COMPARISON NOT RUN: hactool not found. This is not a pass."
    [ "$KEEP" = 1 ] && echo "listing: $WORK/nximport.txt"
    exit 2
fi

echo "== hactool"
PROGS=$("$NX" "${ARGS[@]}" --programs)
BASE_NCA=$(awk '$1 == "base" {print $2}' <<< "$PROGS")
UPDATE_NCA=$(awk '$1 == "update" {print $2}' <<< "$PROGS")
if [ "$KIND" = xci ]; then "$HACTOOL" -k "$KEYS" -t xci --securedir="$WORK/base_pkg" "$BASE" >/dev/null
else "$HACTOOL" -k "$KEYS" -t pfs0 --outdir="$WORK/base_pkg" "$BASE" >/dev/null; fi
REF=$WORK/hactool
mkdir -p "$REF"
if [ -n "$UPDATE" ]; then
    "$HACTOOL" -k "$KEYS" -t pfs0 --outdir="$WORK/update_pkg" "$UPDATE" >/dev/null
    "$HACTOOL" -k "$KEYS" --basenca="$WORK/base_pkg/$BASE_NCA" --exefsdir="$REF/exefs" --romfsdir="$REF/romfs" \
        "$WORK/update_pkg/$UPDATE_NCA" >/dev/null
else
    "$HACTOOL" -k "$KEYS" --exefsdir="$REF/exefs" --romfsdir="$REF/romfs" "$WORK/base_pkg/$BASE_NCA" >/dev/null
fi

echo "== comparing"
listing "$OURS" > "$WORK/nximport.txt"
listing "$REF" > "$WORK/hactool.txt"
STATUS=0
if ! diff <(cut -d' ' -f1,2 "$WORK/nximport.txt") <(cut -d' ' -f1,2 "$WORK/hactool.txt") > "$WORK/paths.diff"; then
    echo "DIFFERENT file lists or sizes ($(grep -c '^[<>]' "$WORK/paths.diff") lines; < nximport, > hactool):"
    head -20 "$WORK/paths.diff"
    STATUS=1
fi
if ! diff "$WORK/nximport.txt" "$WORK/hactool.txt" > "$WORK/hashes.diff"; then
    echo "DIFFERENT contents (SHA-256):"
    grep '^<' "$WORK/hashes.diff" | head -20
    STATUS=1
fi
dirs() { (cd "$1" && find . -mindepth 1 -type d -printf '%P\n' | LC_ALL=C sort); }
if ! diff <(dirs "$OURS") <(dirs "$REF") > "$WORK/dirs.diff"; then
    echo "note: the folder lists differ (empty folders?):"
    head -10 "$WORK/dirs.diff"
fi
N=$(wc -l < "$WORK/nximport.txt")
if [ $STATUS = 0 ]; then
    echo "IDENTICAL: $N files, same paths, sizes and SHA-256 as hactool"
else
    echo "FAILED: nximport and hactool differ (work folder: $WORK)"
    trap - EXIT
fi
exit $STATUS
