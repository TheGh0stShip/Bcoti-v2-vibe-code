"""Read only the BCOTI settings blob from a captured NVS image."""
import json
from pathlib import Path
import struct
import sys

root = Path(__file__).resolve().parents[1]
from idf_path import idf_path
sys.path.insert(0, str(idf_path() / "components/nvs_flash/nvs_partition_tool"))
from nvs_parser import NVS_Partition

path = Path(sys.argv[1])
nvs = NVS_Partition(path.name, bytearray(path.read_bytes()))
entries = [e for page in nvs.pages for e in page.entries if e.state == "Written"]
namespaces = [e.data["value"] for e in entries if e.metadata["namespace"] == 0 and e.key == "storage"]
if len(namespaces) != 1:
    raise SystemExit("Expected exactly one storage namespace; review required.")
items = [e for e in entries if e.metadata["namespace"] == namespaces[0] and e.key == "stored_values"]
indexes = [e for e in items if e.metadata["type"] == "blob_index"]
if len(indexes) != 1:
    raise SystemExit("Expected exactly one current blob index; review required.")
index = indexes[0]
parts = []
for number in range(index.data["chunk_start"], index.data["chunk_start"] + index.data["chunk_count"]):
    chunks = [e for e in items if e.metadata["type"] == "blob_data" and e.metadata["chunk_index"] == number]
    if len(chunks) != 1:
        raise SystemExit("Missing/ambiguous settings chunk.")
    chunk = chunks[0]
    chunk.compute_crc()
    crc = chunk.metadata["crc"]
    if crc["original"] != crc["computed"] or crc["data_original"] != crc["data_computed"]:
        raise SystemExit("Settings chunk CRC mismatch.")
    parts.append(b"".join(bytes(child.raw) for child in chunk.children)[:chunk.data["size"]])
blob = b"".join(parts)
if len(blob) != index.data["size"] or len(blob) != 132:
    raise SystemExit(f"Incompatible settings size: {len(blob)}")
u32 = lambda offset: struct.unpack_from("<I", blob, offset)[0]
u16 = lambda offset: struct.unpack_from("<H", blob, offset)[0]
report = dict(blob_bytes=len(blob), active_preset=blob[0],
              flip_mode=u32(4), av_format=u32(8), zoom=blob[12],
              zoom_x=u16(14), zoom_y=u16(16), fps=u32(20),
              refresh_flip_mode=blob[24], first_boot=blob[128])
report["presets"] = [
    dict(enabled=blob[o], pseudo_color=u32(o+4), scene_mode=u32(o+8),
         brightness=blob[o+12], contrast=blob[o+13], edges=blob[o+14],
         detail=blob[o+15], burn_protection=blob[o+16],
         auto_shutter=blob[o+17], breathing=blob[o+18])
    for o in range(28, 128, 20)]
print(json.dumps(report, indent=2))
