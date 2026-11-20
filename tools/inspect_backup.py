"""Validate/extract a captured flash backup; never opens USB or writes the device."""
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
from idf_path import idf_path
sys.path.insert(0, str(idf_path() / "components/partition_table"))
from gen_esp32part import PartitionTable

backup = Path(sys.argv[1]).resolve()
data = backup.read_bytes()
if len(data) < 0x110000:
    raise SystemExit("Backup is too short for the expected layout; do not flash.")
table = PartitionTable.from_binary(data[0x8000:0x8C00])
table.verify()
print(table.to_csv())
expected = {("nvs", 1, 2, 0x9000, 0x6000),
            ("phy_init", 1, 1, 0xF000, 0x1000),
            ("factory", 0, 0, 0x10000, 0x100000)}
actual = {(p.name, p.type, p.subtype, p.offset, p.size) for p in table}
if actual != expected or any(p.encrypted for p in table):
    raise SystemExit("Layout/encryption differs from this build. Preserve backup; review before flashing.")
if data[0x10000] != 0xE9:
    raise SystemExit("Application is not a plain ESP image; review security info before flashing.")
out = backup.parent / "partitions"
out.mkdir(exist_ok=False)
parts = {"bootloader-region.bin": (0, 0x8000),
         "partition-table-region.bin": (0x8000, 0x1000),
         "nvs.bin": (0x9000, 0x6000),
         "phy_init.bin": (0xF000, 0x1000),
         "factory-app.bin": (0x10000, 0x100000)}
hashes = {"full-flash.bin": hashlib.sha256(data).hexdigest()}
for name, (offset, size) in parts.items():
    blob = data[offset:offset + size]
    (out / name).write_bytes(blob)
    hashes[name] = hashlib.sha256(blob).hexdigest()
(out / "sha256.json").write_text(json.dumps(hashes, indent=2) + "\n")
print("Matching V2 layout; rollback app/NVS extracted to", out)
