"""Repackage amaral.3 Java/resources with the newly compiled native runtime.

Native-only changes: retain all game import/recompilation assets byte for byte.
Signing and zip alignment must be performed after this script.
"""
import argparse
import hashlib
import struct
import zipfile
from pathlib import Path

def replace_version(xml, old, new):
    # Rebuild UTF-16 string-pool offsets and sizes: .10 is longer than .3.
    pos = 8
    while pos < len(xml):
        kind, header, size = struct.unpack_from('<HHI', xml, pos)
        if kind != 1:
            pos += size
            continue
        pool = xml[pos:pos+size]
        count, styles, flags, start, style_start = struct.unpack_from('<IIIII', pool, 8)
        assert not flags & 0x100, 'Expected UTF-16 string pool'
        offsets = struct.unpack_from('<'+'I'*count, pool, header)
        strings = bytearray()
        revised = []
        changed = 0
        for offset in offsets:
            at = start + offset
            length = struct.unpack_from('<H', pool, at)[0]
            prefix = 2
            if length & 0x8000:
                length = ((length & 0x7fff) << 16) | struct.unpack_from('<H', pool, at+2)[0]
                prefix = 4
            raw = pool[at:at+prefix+length*2+2]
            text = pool[at+prefix:at+prefix+length*2].decode('utf-16le')
            if text == old:
                encoded = new.encode('utf-16le')
                raw = struct.pack('<H', len(encoded)//2) + encoded + b'\0\0'
                changed += 1
            revised.append(len(strings))
            strings.extend(raw)
        assert changed == 1
        strings.extend(b'\0' * (-(start+len(strings)) % 4))
        rebuilt = bytearray(pool[:start])
        struct.pack_into('<'+'I'*count, rebuilt, header, *revised)
        if style_start:
            struct.pack_into('<I', rebuilt, 24, start+len(strings))
        rebuilt.extend(strings)
        if style_start:
            rebuilt.extend(pool[style_start:])
        struct.pack_into('<I', rebuilt, 4, len(rebuilt))
        result = bytearray(xml[:pos] + rebuilt + xml[pos+size:])
        struct.pack_into('<I', result, 4, len(result))
        return bytes(result)
    raise AssertionError('Missing string pool')

p = argparse.ArgumentParser()
p.add_argument('base_apk', type=Path)
p.add_argument('native_library', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
native = a.native_library.read_bytes()
assert native[:4] == b'\x7fELF'
assert b'[graphics-diag] feedback copies:' in native
assert b'pipeline workers=%d' in native
with zipfile.ZipFile(a.base_apk) as src, zipfile.ZipFile(a.output, 'w') as dst:
    for info in src.infolist():
        if info.filename.startswith('META-INF/'):
            continue
        data = src.read(info)
        if info.filename == 'lib/arm64-v8a/libmain.so':
            assert hashlib.sha256(data).hexdigest() == '7ab5ceec7079290952d3004f2bff27b22b84bc69e1f6f2fbace39638407e9bea'
            data = native
        elif info.filename == 'AndroidManifest.xml':
            old = '0.2.0-beta-amaral.3'.encode('utf-16le')
            assert data.count(old) == 1
            data = replace_version(data, '0.2.0-beta-amaral.3', '0.2.1-beta-amaral.15')
            # Binary XML resource map resolves each attribute name to its resource ID.
            buf = bytearray(data)
            pos = 8
            resource_ids = []
            changed = 0
            while pos < len(buf):
                kind, header, size = struct.unpack_from('<HHI', buf, pos)
                assert size >= header and size > 0
                if kind == 0x180:
                    resource_ids = list(struct.unpack_from('<' + 'I' * ((size-header)//4), buf, pos+header))
                elif kind == 0x102:
                    start, stride, count = struct.unpack_from('<HHH', buf, pos+24)
                    for i in range(count):
                        at = pos+16+start+i*stride
                        name = struct.unpack_from('<I', buf, at+4)[0]
                        if name < len(resource_ids) and resource_ids[name] == 0x0101021b:
                            assert buf[at+15] == 0x10
                            assert struct.unpack_from('<I', buf, at+16)[0] == 6
                            struct.pack_into('<I', buf, at+16, 18)
                            changed += 1
                pos += size
            assert changed == 1
            data = bytes(buf)
        dst.writestr(info, data)
with zipfile.ZipFile(a.base_apk) as src, zipfile.ZipFile(a.output) as dst:
    changed = [n for n in dst.namelist() if src.read(n) != dst.read(n)]
    assert set(changed) == {'AndroidManifest.xml', 'lib/arm64-v8a/libmain.so'}, changed
print('Verified: only native runtime and APK version changed.')
