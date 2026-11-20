# Back up, inspect, flash safely, and roll back

Commands below use esptool **4.x** in the activated **ESP-IDF v5.3.1** Python
environment. Run from this repository root. Replace `PORT` and `BACKUP` with your
actual values. Do not type the placeholders literally. Windows uses `COM4`-style
ports; Linux often uses `/dev/ttyACM0`. Close serial monitors first.

## 1. Connect and identify (no flash writes)

Use a known USB data cable and a direct PC connection, turn the brightness/power
knob on, and remove other power supplies. The housing must expose USB data, not
just power. You normally do not need to hold the middle button for USB flashing.

```sh
python -m serial.tools.list_ports -v
```

Identify the newly appearing ESP32-C3 USB device. Do not assume the example port
belongs to your BCOTI. If it does not enumerate, check cable, port and wiring;
repeated firmware writes cannot fix a power-only cable.

## 2. Capture two full backups

Cross-platform command:

```sh
python tools/backup_device.py --port PORT
```

Windows convenience wrapper:

```powershell
.\tools\Read-Device.ps1 -Port COM4
```

Both use the same implementation. Optional `--baud 115200` / `-Baud 115200` is
slower but can help unstable links. `--output NEW_DIRECTORY` / `-OutputDirectory`
selects a new directory; an existing directory is refused to protect earlier
backups. By default, files go under `backups/YYYYMMDD-HHMMSS/`.

The script resets/halts the application, reads chip/security/flash information,
reads the entire detected flash twice, compares SHA-256 hashes and validates the
partition table. It never calls write_flash, erase_flash or efuse-writing tools.
On error it stops and retains partial files. **Do not flash on a failed check.**
The device remains in loader mode; power-cycle to resume old firmware if stopping.

Files retained:

- `inspection.txt`: read commands/results; inspect security information manually.
- `full-flash.bin`, `full-flash-check.bin`: matching whole-flash copies.
- `partitions/bootloader-region.bin`, `partition-table-region.bin`, `phy_init.bin`.
- `partitions/nvs.bin`: original private settings.
- `partitions/factory-app.bin`: your previously installed rollback application.
- `partitions/sha256.json`: extraction hashes.

Keep a second copy off the development machine. Never commit or upload these
files; they can contain device identifiers and credentials. `.gitignore` helps
but cannot prevent deliberately adding private data with `git add -f`.

## 3. Review compatibility before writing

This build expects exactly:

| Partition | Offset | Size |
| --- | --- | --- |
| NVS | 0x9000 | 0x6000 |
| PHY | 0xF000 | 0x1000 |
| Factory application | 0x10000 | 0x100000 |

Partition table is at 0x8000. The tested device had 4 MiB flash, an ESP32-C3 rev
0.4, and no secure boot/flash encryption. **Stop for a different chip/layout,
enabled security features, incompatible settings, or unreadable backup.** Matching
partition layout does not establish every camera/display variant is compatible.

```sh
python -m esptool image_info BACKUP/partitions/factory-app.bin
python -m esptool image_info firmware/BCOTI-personal-v3.bin
python tools/inspect_config.py BACKUP/partitions/nvs.bin
```

The settings inspector reads only the existing `storage:stored_values` legacy
132-byte blob and checks chunk CRCs. Missing/ambiguous/unsupported blobs require
review; do not force conversion or erase NVS. Firmware validates saved values at
startup and disables persistence on incompatible stored data rather than erasing it.

## 4. Application-only write, after the owner's approval

Verify the binary SHA-256 against `firmware/manifest.json`. Preserve the backup
before proceeding. These commands erase **only the application's affected
sectors**; they do not intentionally rewrite the bootloader, table, PHY or NVS.

```sh
python -m esptool --chip esp32c3 --port PORT --baud 460800 --before default_reset --after no_reset write_flash --flash_mode keep --flash_freq keep --flash_size keep 0x10000 firmware/BCOTI-personal-v3.bin
python -m esptool --chip esp32c3 --port PORT --baud 460800 --before default_reset --after no_reset verify_flash 0x10000 firmware/BCOTI-personal-v3.bin
python -m esptool --chip esp32c3 --port PORT --baud 460800 --before default_reset --after no_reset verify_flash 0x0 BACKUP/partitions/bootloader-region.bin 0x8000 BACKUP/partitions/partition-table-region.bin 0x9000 BACKUP/partitions/nvs.bin 0xF000 BACKUP/partitions/phy_init.bin
```

For your own rebuild, substitute `Esp32_Files/ESP_IDF_Project/build/DIY_COTI.bin`.
If any verification fails, stop and investigate; do not use `--force`, full-chip
erase, `idf.py flash`, or a merged image as a shortcut. Keep all power stable.

The bundled build/backup tools never perform these writes automatically. A
firmware write requires your deliberate invocation and, if helping someone else,
their explicit approval. Nothing in this repository uploads firmware over Wi-Fi.

## 5. Confirm startup and true cold boot

Capture native USB/JTAG startup after the write:

```sh
python tools/capture_serial.py --port PORT --reset --seconds 25 --output BACKUP/new-startup.log
```

This helper's optional reset is for native ESP32-C3 USB/JTAG. On an external USB
UART adapter, omit `--reset` and reset manually, or use a suitable serial monitor.
An existing output filename is refused. Re-list ports if USB re-enumerates.

Look for the expected app version, `NVS restored`, camera readiness, alignment TX
and camera status. A reset is **not** a full power cycle. Turn off/unplug all power
for ten seconds, boot normally and visually confirm alignment. Then test Wi-Fi
values, middle-button cycling and the new UI. Readback of flip does not verify crop.

## 6. Roll back without a chip erase

After approval, restore your own previously working application:

```sh
python -m esptool --chip esp32c3 --port PORT --baud 460800 --before default_reset --after no_reset write_flash --flash_mode keep --flash_freq keep --flash_size keep 0x10000 BACKUP/partitions/factory-app.bin
python -m esptool --chip esp32c3 --port PORT --baud 460800 --before default_reset --after hard_reset verify_flash 0x10000 BACKUP/partitions/factory-app.bin
```

The NVS layout is unchanged, so settings normally remain usable by compatible
previous firmware. To restore the exact old settings too, first back up the
current ones, obtain separate approval to discard later edits, then:

```sh
python -m esptool --chip esp32c3 --port PORT --baud 460800 --before default_reset --after no_reset write_flash 0x9000 BACKUP/partitions/nvs.bin
python -m esptool --chip esp32c3 --port PORT --baud 460800 --before default_reset --after hard_reset verify_flash 0x9000 BACKUP/partitions/nvs.bin
```

Normal rollback needs no bootloader/table rewrite or full-chip erase. Flash backups
do not capture the OLED driver's or camera's internal settings; a previous
**Repair Camera** action or physical mirroring change may need separate recovery.
