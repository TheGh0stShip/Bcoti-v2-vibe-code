"""Verify the published binary and normalized source hashes, without USB access."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / 'firmware/manifest.json').read_text(encoding='utf-8'))
binary = root / 'firmware' / manifest['file']
if binary.parent.resolve() != (root / 'firmware').resolve():
    raise SystemExit('Invalid binary path in manifest.')
if hashlib.sha256(binary.read_bytes()).hexdigest() != manifest['sha256']:
    raise SystemExit('Firmware checksum mismatch.')
if binary.stat().st_size != manifest['size_bytes']:
    raise SystemExit('Firmware size mismatch.')
for name, expected in manifest['source_sha256'].items():
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()):
        raise SystemExit('Invalid source path in manifest.')
    actual = hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
    if actual != expected:
        raise SystemExit(f'Source changed since this packaged build: {name}')
print('PASS firmware size/SHA-256 and all normalized application source hashes')
