"""Repackage the signed .15's Java/resources with .16 runtime and 1.4.2 generator.
Only the runtime, manifest, generator patch file and builder fingerprint change.
Run sign_amaral_apk.py afterwards with the fixed certificate.
"""
import argparse, ast, hashlib, struct, zipfile
from pathlib import Path

p=argparse.ArgumentParser();p.add_argument('base',type=Path);p.add_argument('native',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
r=Path(__file__).resolve().parents[1]
# Share the existing binary XML string-pool implementation without its CLI.
module=ast.parse((r/'tools/repack_amaral15.py').read_text())
fn=next(x for x in module.body if isinstance(x,ast.FunctionDef) and x.name=='replace_version')
exec(compile(ast.Module(body=[fn],type_ignores=[]),'replace_version','exec'))
native=a.native.read_bytes();assert native[:4]==b'\x7fELF'
assert b'amaral.16 TOTK 1.4.2 timing and vertex growth build' in native
assert b'[vertex-growth]' in native
patch=(r/'tools/patches.py').read_bytes();assert b'PATCHES_142' in patch
with zipfile.ZipFile(a.base) as src:
 gen={i.filename.removeprefix('assets/gen/'):src.read(i) for i in src.infolist() if i.filename.startswith('assets/gen/') and not i.is_dir()}
 gen['tools/patches.py']=patch
 digest=hashlib.sha256()
 for name,data in sorted(gen.items()):
  digest.update(name.encode());digest.update(b'\0');digest.update(str(len(data)).encode());digest.update(b'\0');digest.update(data)
 fingerprint=digest.hexdigest().encode()
 manifest=replace_version(src.read('AndroidManifest.xml'),'0.2.1-beta-amaral.15','0.2.1-beta-amaral.16')
 buf=bytearray(manifest);pos=8;ids=[];changed=0
 while pos<len(buf):
  kind,header,size=struct.unpack_from('<HHI',buf,pos);assert size>=header and size>0
  if kind==0x180:ids=list(struct.unpack_from('<'+'I'*((size-header)//4),buf,pos+header))
  elif kind==0x102:
   start,stride,count=struct.unpack_from('<HHH',buf,pos+24)
   for i in range(count):
    at=pos+16+start+i*stride;name=struct.unpack_from('<I',buf,at+4)[0]
    if name<len(ids) and ids[name]==0x0101021b:
     assert buf[at+15]==0x10 and struct.unpack_from('<I',buf,at+16)[0]==18
     struct.pack_into('<I',buf,at+16,19);changed+=1
  pos+=size
 assert changed==1
 replacements={'AndroidManifest.xml':bytes(buf),'lib/arm64-v8a/libmain.so':native,'assets/gen/tools/patches.py':patch,'assets/gen.id':fingerprint}
 with zipfile.ZipFile(a.output,'w') as dst:
  for info in src.infolist():
   if not info.filename.startswith('META-INF/'):dst.writestr(info,replacements.get(info.filename,src.read(info)))
with zipfile.ZipFile(a.base) as src,zipfile.ZipFile(a.output) as dst:
 changed={n for n in dst.namelist() if src.read(n)!=dst.read(n)}
 assert changed==set(replacements),changed
 assert dst.read('assets/gen.id')!=src.read('assets/gen.id')
 print('Verified .16: four changed entries, refreshed game builder, all other assets byte-identical.')
 print('Builder SHA256:',fingerprint.decode())
