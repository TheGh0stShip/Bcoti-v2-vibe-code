"""Copy a successful build into firmware/ with source hashes. No flash or upload."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil

root = Path(__file__).resolve().parents[1]
project = root / 'Esp32_Files/ESP_IDF_Project'
build = project / 'build'
description = json.loads((build / 'project_description.json').read_text())
if description['target'] != 'esp32c3' or description['project_version'] != 'bcoti-personal-3':
    raise SystemExit('Unexpected target/version. Review packaging/version naming before proceeding.')
binary = build / 'DIY_COTI.bin'
if binary.stat().st_size > 0x100000:
    raise SystemExit('Application exceeds the supported partition.')
files = [project / name for name in ('CMakeLists.txt','sdkconfig','dependencies.lock')]
for directory in (project / 'main', project / 'components'):
    files += [path for path in directory.rglob('*') if path.is_file()]
if any(path.stat().st_mtime > binary.stat().st_mtime for path in files):
    raise SystemExit('A source is newer than the binary. Rebuild before packaging.')
out = root / 'firmware'
out.mkdir(exist_ok=True)
destination = out / 'BCOTI-personal-v3.bin'
manifest_path = out / 'manifest.json'
if destination.exists() or manifest_path.exists():
    raise SystemExit('Packaged artifact already exists; preserve it and review a new release explicitly.')
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
shutil.copy2(binary, destination)
manifest = {
    'packaged_utc': datetime.now(timezone.utc).isoformat(),
    'app_version': description['project_version'],
    'idf_version': 'v5.3.1', 'target': 'esp32c3',
    'upstream_base': '3d0a60b33ea24dbb9db645e426a9cef9cd25c97e',
    'application_offset': '0x10000', 'partition_size': '0x100000',
    'file': destination.name, 'size_bytes': destination.stat().st_size,
    'sha256': sha(destination),
    'this_rebuilt_binary_hardware_tested': False,
    'source_hash_normalization': 'UTF-8 text bytes with CRLF converted to LF',
    'source_sha256': {path.relative_to(root).as_posix():
                      hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
                      for path in sorted(files)},
}
manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
print(json.dumps({key:value for key,value in manifest.items() if key!='source_sha256'}, indent=2))
