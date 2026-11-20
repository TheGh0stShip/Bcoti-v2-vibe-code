"""Exercise the actual backup CLI with mocked serial subprocesses; no USB access."""
from pathlib import Path
from unittest.mock import patch
import contextlib
import importlib.util
import io
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'tools'))
import backup_device

build = root / 'Esp32_Files/ESP_IDF_Project/build'
flash = bytearray(b'\xff' * 0x400000)
table = (build / 'partition_table/partition-table.bin').read_bytes()
app = (build / 'DIY_COTI.bin').read_bytes()
flash[0x8000:0x8000+len(table)] = table
flash[0x10000:0x10000+len(app)] = app
real_run = subprocess.run

with tempfile.TemporaryDirectory(prefix='bcoti-backup-cli-') as temporary:
    for mode in ('success', 'mismatch', 'read_failure'):
        out = Path(temporary) / mode
        commands = []
        def fake_run(command, **kwargs):
            if '-m' not in command:  # Real offline partition validation, no hardware.
                return real_run(command, **kwargs)
            commands.append(command)
            assert command[2] == 'esptool'
            assert command[command.index('--before')+1] == 'default_reset'
            assert command[command.index('--after')+1] == 'no_reset'
            action = command[command.index('--after')+2]
            assert action in ('chip_id','get_security_info','flash_id','read_flash')
            if mode == 'read_failure' and action == 'flash_id':
                return subprocess.CompletedProcess(command, 1, 'mock read failure\n')
            if action == 'read_flash':
                data = bytearray(flash)
                if mode == 'mismatch' and command[-1].endswith('check.bin'):
                    data[-1] ^= 1
                Path(command[-1]).write_bytes(data)
            return subprocess.CompletedProcess(command, 0, 'mock read only\n')
        with patch.object(sys, 'argv', ['backup_device.py','--port','MOCK','--output',str(out)]), \
             patch.object(backup_device, 'version', return_value='4.12.0'), \
             patch.object(backup_device.subprocess, 'run', side_effect=fake_run), \
             contextlib.redirect_stdout(io.StringIO()):
            if mode == 'success':
                backup_device.main()
                assert len(commands) == 5
                assert (out / 'partitions/nvs.bin').stat().st_size == 0x6000
                assert (out / 'full-flash.bin').read_bytes() == flash
                try:
                    backup_device.main()
                    raise AssertionError('Existing backup directory was not rejected')
                except FileExistsError:
                    pass
                assert len(commands) == 5  # No hardware touched on overwrite attempt.
            else:
                try:
                    backup_device.main()
                    raise AssertionError('Failure was not propagated')
                except RuntimeError:
                    pass
                assert not (out / 'partitions').exists()
                assert (out / 'inspection.txt').exists()
                if mode == 'read_failure':
                    assert len(commands) == 3
print('PASS read-only backup sequence, matching reads, mismatch/failure stop, overwrite refusal')
