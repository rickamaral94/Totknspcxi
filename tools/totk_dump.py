"""Check a Tears of the Kingdom dump: the exefs modules, their build ids (which game version), what each one is, the NPDM
and the romfs.

Usage: totk_dump.py [dump dir]   (default: Dump/0100F2C0115B6000, else the only folder under ../Dump with an exefs/)

The dump is <dir>/exefs (main, main.npdm, rtld, sdk, subsdk0) and <dir>/romfs, with the update applied (the base game's
NCA as --basenca of the update's: hactoolnet -t nca <update program nca> --basenca <base program nca> --exefsdir ...
--romfsdir ...). Nothing is written.
"""
import argparse
import os
import struct
import sys

from nso import Nso

TITLE_IDS = {'0100F2C0115B6000': 'The Legend of Zelda: Tears of the Kingdom'}

# build id of main (first 16 hex digits) -> game version (the generator and the patches are tied to one exact build)
VERSIONS = {
    '277178b7dba1b6d4': '1.4.3',  # TOTK + the update 0100F2C0115B6800 v720896 (SDK 20.5.22)
    '5cb42b1cf25469fb': '1.4.2',  # TOTK + the update v655360 (SDK 20.5.6): beside 1.4.3, recompiled only, no patches
}
TARGETS = ('1.4.3', '1.4.2')

# what each module is (its own build paths, the SDK tags in it and the NEEDED list of main), and how it's treated
MODULES = [
    ('main', "the game (EX-King.nss): EPD's engine after BOTW's (sead, agl, gsys), Havok 2022.1 (AI, Animation, "
     'Cloth), NintendoWare (G3d2, Vfx2, Ui2d, Font)', 'recompiled'),
    ('sdk', "nnSdk 20.5.22: the Nintendo SDK with NVN and display (vi) inside - BOTW 1.8.2's line (20.5.6 there)",
     'recompiled, services HLE, NVN by name (nvnBootstrapLoader: runtime/nvn)'),
    ('subsdk0', 'multimedia.nss: movie playback (nn::movie), SDK 20.5.22', 'HLE: FFmpeg (runtime/hle/movie.c)'),
    ('rtld', 'the dynamic loader', 'not used: runtime/loader.c'),
]

# the romfs: Zstandard-compressed files (.zs; the dictionaries in Pack/ZsDic.pack.zs), the boot pack, the messages of
# each language (Mals/), the resource size table of this version
ROMFS_FILES = ['Pack/ZsDic.pack.zs', 'Pack/Bootup.Nin_NX_NVN.pack.zs', 'Mals/USen.Product.142.sarc.zs']  # (messages .142 in both)
# and the resource size table, named by the version (.Product.143 in 1.4.3, .Product.142 in 1.4.2)
RSIZETABLE = 'System/Resource/ResourceSizeTable.Product.{}.Nin_NX_NVN.rsizetable.zs'


def find_dump():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Dump')
    subs = [d for d in sorted(os.listdir(root)) if os.path.isdir(os.path.join(root, d, 'exefs'))] \
        if os.path.isdir(root) else []
    if not subs:
        sys.exit(f'no dump under {os.path.normpath(root)} (expected Dump/<title id>/exefs and romfs)')
    return os.path.join(root, '0100F2C0115B6000' if '0100F2C0115B6000' in subs else subs[0])


def npdm(path):
    """Program id, address space type, main thread priority / stack and the cores, from main.npdm."""
    d = open(path, 'rb').read()
    assert d[:4] == b'META', path
    aci = struct.unpack_from('<I', d, 0x70)[0]
    out = {'flags': d[0xC], 'prio': d[0xE], 'core': d[0xF], 'stack': struct.unpack_from('<I', d, 0x1C)[0],
           'sysres': struct.unpack_from('<I', d, 0x14)[0], 'program': None}
    if d[aci:aci + 4] == b'ACI0':
        out['program'] = f'{struct.unpack_from("<Q", d, aci + 0x10)[0]:016X}'
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dump', nargs='?')
    args = ap.parse_args()
    dump = os.path.normpath(args.dump or find_dump())
    exefs, romfs = os.path.join(dump, 'exefs'), os.path.join(dump, 'romfs')
    print(f'dump: {dump}')
    ok = True
    version = None  # (main's, from its build id)

    for name, what, how in MODULES:
        path = os.path.join(exefs, name)
        if not os.path.isfile(path):
            print(f'  {name:8} MISSING')
            ok = False
            continue
        n = Nso(path)
        print(f'  {name:8} build {n.build_id[:32]}  image {n.size / 2**20:6.1f} MiB  '
              f'syms {len(n.syms):5}  relocs {len(n.relocs):6}')
        print(f'           {what} -> {how}')
        if name == 'main':
            version = VERSIONS.get(n.build_id[:16])
            print(f'           NEEDED: {", ".join(os.path.basename(x) for x in n.needed)}')
            if version is None:
                print(f'  main: unknown build: this project runs v{" or v".join(TARGETS)} (apply the update)')
                ok = False
            else:
                print(f'  game version: v{version}' + (' (recompiled game code only, without 1.4.3\'s patches)'
                                                        if version == '1.4.2' else ''))
    path = os.path.join(exefs, 'main.npdm')
    if not os.path.isfile(path):
        print('  main.npdm MISSING')
        ok = False
    else:
        m = npdm(path)
        title = TITLE_IDS.get(m['program'], 'not Tears of the Kingdom')
        print(f'  main.npdm: program {m["program"]} ({title}), address space type {(m["flags"] >> 1) & 7}, main thread '
              f'priority {m["prio"]} on core {m["core"]}, stack {m["stack"]:#x}, system resource {m["sysres"]:#x}')
        ok &= m['program'] in TITLE_IDS

    wanted = ROMFS_FILES + ([RSIZETABLE.format(version.replace('.', ''))] if version else [])
    missing = [f for f in wanted if not os.path.isfile(os.path.join(romfs, f))]
    if missing:
        print(f'  romfs: missing {", ".join(missing)}')
        ok = False
    else:
        files, size = 0, 0
        for dirpath, _, names in os.walk(romfs):
            for f in names:
                files += 1
                size += os.path.getsize(os.path.join(dirpath, f))
        print(f'  romfs: {files} files, {size / 2**30:.1f} GiB')

    print('OK' if ok else 'problems found')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
