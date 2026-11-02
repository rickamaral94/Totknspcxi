"""Sign with the durable Amaral key; never generate a replacement key.

Usage: python tools/sign_amaral_apk.py BUILD_TOOLS SIGNING_JSON INPUT_APK OUTPUT_APK
The signing JSON and PKCS12 belong outside the repository.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

EXPECTED_CERT = 'dfd0653e2a1df81b42a0381e39f4251ac82001a0f8bd8bfa7070157f4c36f1e3'
bt, config, src, dst = map(Path, sys.argv[1:])
d = json.loads(config.read_text())
key = config.parent / d['keystore']
cert = config.parent / 'signing-certificate.der'
if hashlib.sha256(cert.read_bytes()).hexdigest() != EXPECTED_CERT:
    raise SystemExit('Wrong signing certificate; refusing to sign.')
if not key.is_file():
    raise SystemExit('Fixed signing key is missing; restore the private backup.')
env = os.environ.copy()
env['TOTK_STORE_PASSWORD'] = d['store_password']
env['TOTK_KEY_PASSWORD'] = d['key_password']
aligned = dst.with_suffix('.aligned.apk')
subprocess.run([str(bt/'zipalign'), '-P', '16', '-f', '4', str(src), str(aligned)], check=True)
subprocess.run([str(bt/'apksigner'), 'sign', '--ks', str(key), '--ks-key-alias', d['alias'],
                '--ks-pass', 'env:TOTK_STORE_PASSWORD', '--key-pass', 'env:TOTK_KEY_PASSWORD',
                '--out', str(dst), str(aligned)], env=env, check=True)
result = subprocess.run([str(bt/'apksigner'), 'verify', '--verbose', '--print-certs', str(dst)],
                        check=True, capture_output=True, text=True)
if f'Signer #1 certificate SHA-256 digest: {EXPECTED_CERT}' not in result.stdout:
    dst.unlink()
    raise SystemExit('APK certificate differs from the fixed certificate; output removed.')
subprocess.run([str(bt/'zipalign'), '-c', '-P', '16', '4', str(dst)], check=True)
aligned.unlink()
print(result.stdout)
