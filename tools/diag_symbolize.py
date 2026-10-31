#!/usr/bin/env python3
"""Names the addresses of a performance diagnosis (runtime/diag.c) offline.

The report names game code as f_<guest address> already. The runtime's own code, where its library exports no symbol,
appears as <module>+<offset> (64-byte buckets). This script resolves those offsets with llvm-symbolizer against the
unstripped library of the same build (checked by build id) and adds the samples up per function.

  tools/diag_symbolize.py botwrecomp-log.txt --lib android/totkrecompandroid/app/build/intermediates/merged_native_libs/debug/mergeDebugNativeLibs/out/lib/arm64-v8a/libmain.so
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from collections import OrderedDict, defaultdict

ENTRY = re.compile(r"^\[diag\]\s+([0-9.]+)%\s+(\S+)\s+\[([a-z0-9-]+)\]\s*$")
HEAD = re.compile(r"^\[diag\] (\S+): \d+ samples kept")
SECTION = re.compile(r"^\[diag\]\s+(functions|callers) \(")
MODULE = re.compile(r"^\[diag\] (\S+) base 0x[0-9a-f]+ build-id ([0-9a-f-]+)")
BUCKET = re.compile(r"^(.+?)\+0x([0-9a-f]+)$")


def build_id(path):
    tool = shutil.which("llvm-readelf") or shutil.which("readelf")
    if not tool:
        return None
    out = subprocess.run([tool, "-n", path], capture_output=True, text=True).stdout
    m = re.search(r"Build ID:\s*([0-9a-f]+)", out)
    return m.group(1) if m else None


def symbolize(lib, offsets):
    tool = shutil.which("llvm-symbolizer") or shutil.which("addr2line")
    if not tool:
        sys.exit("needs llvm-symbolizer (Android NDK: toolchains/llvm/prebuilt/*/bin) or addr2line")
    # (the middle of each 64-byte bucket)
    addrs = ["0x%x" % (o + 0x20) for o in offsets]
    if "llvm-symbolizer" in tool:
        out = subprocess.run([tool, "--obj=" + lib, "--functions=short", "--no-inlines", "--output-style=GNU"] + addrs,
                             capture_output=True, text=True).stdout.split("\n")
        names, i = [], 0
        while i < len(out) and len(names) < len(addrs):
            names.append(out[i].strip() or "?")
            i += 2
            while i < len(out) and out[i] == "":
                i += 1
    else:
        out = subprocess.run([tool, "-f", "-C", "-e", lib] + addrs, capture_output=True, text=True).stdout.split("\n")
        names = [out[k].strip() for k in range(0, 2 * len(addrs), 2)]
    return dict(zip(offsets, names))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--lib", action="append", default=[], help="an unstripped library of the same build (repeatable)")
    a = ap.parse_args()
    libs = {os.path.basename(p): p for p in a.lib}
    ids = {}
    lists = OrderedDict()  # (thread, section) -> [(percent, name, category)]
    who = section = None
    with open(a.log, errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            line = line[line.find("[diag]"):] if "[diag]" in line else line
            m = HEAD.match(line)
            if m:
                who, section = m.group(1), None
                continue
            m = SECTION.match(line)
            if m and who:
                section = m.group(1)
                lists[(who, section)] = []
                continue
            m = MODULE.match(line)
            if m:
                ids[m.group(1)] = m.group(2)
                continue
            m = ENTRY.match(line)
            if m and who and section:
                lists[(who, section)].append((float(m.group(1)), m.group(2), m.group(3)))
    for mod, path in libs.items():
        got = build_id(path)
        if mod in ids and got and got[:40] != ids[mod][:40]:
            print("warning: %s's build id %s is not the report's %s - the names would be wrong" % (path, got, ids[mod]))
    wanted = defaultdict(set)
    for entries in lists.values():
        for _, name, _ in entries:
            m = BUCKET.match(name)
            if m and m.group(1) in libs:
                wanted[m.group(1)].add(int(m.group(2), 16))
    names = {}
    for mod, offs in wanted.items():
        offs = sorted(offs)
        for off, nm in symbolize(libs[mod], offs).items():
            names["%s+0x%x" % (mod, off)] = "%s:%s" % (mod, nm)
    for (who, section), entries in lists.items():
        total = defaultdict(float)
        cats = {}
        for pct, name, cat in entries:
            key = names.get(name, name)
            total[key] += pct
            cats.setdefault(key, cat)
        print("== %s, %s (the report's top entries, buckets added up per function)" % (who, section))
        for key, pct in sorted(total.items(), key=lambda kv: -kv[1])[:60]:
            print("  %6.2f%%  %-70s [%s]" % (pct, key, cats[key]))


if __name__ == "__main__":
    main()
