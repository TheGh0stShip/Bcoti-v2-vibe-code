"""Check backup extraction using a synthetic image, never real hardware."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
original = root / "Esp32_Files/ESP_IDF_Project/build"
blob = bytearray(b"\xff" * 0x400000)
table = (original / "partition_table/partition-table.bin").read_bytes()
app = (original / "DIY_COTI.bin").read_bytes()
blob[0x8000:0x8000+len(table)] = table
blob[0x10000:0x10000+len(app)] = app
with tempfile.TemporaryDirectory(prefix="bcoti-synthetic-backup-") as temporary:
    folder = Path(temporary)
    backup = folder / "full-flash.bin"
    backup.write_bytes(blob)
    subprocess.run([sys.executable, str(root / "tools/inspect_backup.py"), str(backup)], check=True)
    assert (folder / "partitions/factory-app.bin").read_bytes() == blob[0x10000:0x110000]
    assert (folder / "partitions/nvs.bin").stat().st_size == 0x6000
    damaged = folder / "damaged.bin"
    blob[0x8000] = 0
    damaged.write_bytes(blob)
    result = subprocess.run([sys.executable, str(root / "tools/inspect_backup.py"), str(damaged)], capture_output=True)
    assert result.returncode != 0
print("PASS backup partition extraction and malformed-table rejection (synthetic data)")
