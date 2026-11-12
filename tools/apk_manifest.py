#!/usr/bin/env python3
"""Lists what an APK carries, for a build note: the APK's own SHA-256 and size, and the SHA-256 and size of every native
library and of the asset that identifies the generated game code (assets/gen.id).

A candidate made by reusing libraries from an earlier APK (third-party .so files not rebuilt) is reproducible only if
the note names which ones and their hashes: compare two APKs with --diff to see exactly which files changed.

  python3 tools/apk_manifest.py app.apk                 # Markdown table
  python3 tools/apk_manifest.py new.apk --diff old.apk  # only what differs, and what was carried over unchanged
"""
import argparse
import hashlib
import os
import sys
import zipfile

KEEP_PREFIXES = ("lib/",)
KEEP_FILES = ("assets/gen.id",)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def entries(apk):
    out = {}
    with zipfile.ZipFile(apk) as z:
        for info in z.infolist():
            if info.is_dir():
                continue
            if info.filename.startswith(KEEP_PREFIXES) or info.filename in KEEP_FILES:
                out[info.filename] = (hashlib.sha256(z.read(info)).hexdigest(), info.file_size)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("apk")
    ap.add_argument("--diff", metavar="OLD_APK", help="compare with an earlier APK")
    a = ap.parse_args()
    new = entries(a.apk)
    size = f"{os.path.getsize(a.apk):,}".replace(",", ".")
    print(f"APK `{os.path.basename(a.apk)}`: SHA-256 `{sha256_file(a.apk)}`, {size} bytes")
    print()
    if not a.diff:
        print("| Arquivo | SHA-256 | Bytes |\n|---|---|---|")
        for name in sorted(new):
            print(f"| `{name}` | `{new[name][0]}` | {new[name][1]} |")
        return 0
    old = entries(a.diff)
    print(f"Comparado com `{os.path.basename(a.diff)}`: SHA-256 `{sha256_file(a.diff)}`")
    print()
    print("| Arquivo | Estado | SHA-256 novo |\n|---|---|---|")
    for name in sorted(set(new) | set(old)):
        if name not in old:
            state = "novo"
        elif name not in new:
            state = "removido"
        elif new[name][0] == old[name][0]:
            state = "igual (reaproveitado)"
        else:
            state = "mudou"
        print(f"| `{name}` | {state} | `{new[name][0] if name in new else '-'}` |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
