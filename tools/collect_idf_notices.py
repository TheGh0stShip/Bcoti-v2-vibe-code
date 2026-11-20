"""Collect upstream SDK/component license texts for redistribution with the app."""
from pathlib import Path
import re
from idf_path import idf_path

root = Path(__file__).resolve().parents[1]
idf = idf_path()
dest = root / 'firmware/THIRD_PARTY_NOTICES.txt'
if dest.exists():
    raise SystemExit('Notices already exist; do not replace a reviewed package silently.')
sources = [('ESP-IDF v5.3.1', idf, [idf / 'LICENSE'])]
pattern = re.compile(r'^(LICENSE|COPYING|NOTICE)([.-].*)?$', re.I)
sources.append(('ESP-IDF v5.3.1', idf, sorted(p for p in (idf / 'components').rglob('*')
                                             if p.is_file() and pattern.match(p.name))))
managed = root / 'Esp32_Files/ESP_IDF_Project/managed_components'
sources.append(('Managed components (see dependencies.lock)', managed,
                sorted(p for p in managed.rglob('*') if p.is_file() and pattern.match(p.name))))
dest.parent.mkdir(exist_ok=True)
with dest.open('x', encoding='utf-8') as output:
    output.write('Upstream SDK/component notices collected for the prebuilt application.\n'
                 'Includes SDK components that may not be linked; inclusion does not assert use.\n'
                 'These licenses are not replaced by the BCOTI adaptation license.\n\n')
    count = 0
    for label, base, paths in sources:
        for path in paths:
            output.write('\n' + '=' * 72 + '\n' + label + ': ' + path.relative_to(base).as_posix() + '\n\n')
            output.write(path.read_text(encoding='utf-8', errors='replace') + '\n')
            count += 1
print(f'Collected {count} upstream notice files into {dest.name}')
