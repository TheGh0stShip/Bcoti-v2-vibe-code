"""Read-only flash backup for ESP32-C3. Resets/halts the device, never writes flash.

Requires the activated ESP-IDF 5.3.1 environment and esptool 4.x. Output is private.
"""
import argparse
from datetime import datetime
import hashlib
from pathlib import Path
import subprocess
import sys
from importlib.metadata import version
from idf_path import idf_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='BCOTI serial port, e.g. COM4 or /dev/ttyACM0')
    parser.add_argument('--baud', type=int, choices=[115200, 460800], default=460800)
    parser.add_argument('--output', type=Path, help='New, nonexistent output directory')
    args = parser.parse_args()
    idf_path()  # Fail before resetting hardware if inspection dependencies are unavailable.
    if version('esptool').split('.')[0] != '4':
        parser.error('Use esptool 4.x from ESP-IDF 5.3.1; command syntax is version-specific.')
    root = Path(__file__).resolve().parents[1]
    out = (args.output or root / 'backups' / datetime.now().strftime('%Y%m%d-%H%M%S')).resolve()
    out.mkdir(parents=True, exist_ok=False)  # Never overwrite an earlier backup.
    with (out / 'inspection.txt').open('x', encoding='utf-8') as log:
        def run(arguments):
            command = [sys.executable, '-m', 'esptool', '--chip', 'esp32c3', '--port', args.port,
                       '--baud', str(args.baud), '--before', 'default_reset', '--after', 'no_reset', *arguments]
            log.write('COMMAND: ' + repr(arguments) + '\n'); log.flush()
            print('Running ' + arguments[0] + (' -> ' + Path(arguments[-1]).name
                  if arguments[0] == 'read_flash' else '') + '...', flush=True)
            # Each connection enters the ROM loader again. Reusing no_reset before
            # a new process stalled on the tested native USB/JTAG device.
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, errors='replace')
            print(result.stdout, end=''); log.write(result.stdout); log.flush()
            if result.returncode:
                raise RuntimeError(f'Read command failed: {arguments[0]}. Partial backup: {out}')
        for command in ('chip_id', 'get_security_info', 'flash_id'):
            run([command])
        first, second = out / 'full-flash.bin', out / 'full-flash-check.bin'
        for path in (first, second):
            run(['read_flash', '--no-progress', '0', 'ALL', str(path)])
        digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
        if digest(first) != digest(second):
            raise RuntimeError('Full reads differ. Preserve both files and DO NOT FLASH.')
        log.write('Matching full-flash SHA256: ' + digest(first) + '\n'); log.flush()
        result = subprocess.run([sys.executable, str(root / 'tools/inspect_backup.py'), str(first)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
        print(result.stdout, end=''); log.write(result.stdout)
        if result.returncode:
            raise RuntimeError('Layout inspection failed. Preserve backup and DO NOT FLASH.')
    print(f'Backup complete: {out}\nReview security information before any write. '
          'Device remains in loader mode; power-cycle to resume. No flash was written.')


if __name__ == '__main__':
    main()
