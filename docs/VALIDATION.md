# Validation and release notes

## Baseline and changes

The application source is the personal build 3 developed from upstream BCOTI
`V2_Beta` commit `3d0a60b33ea24dbb9db645e426a9cef9cd25c97e`. Publication changes
make build/test/backup tools portable and add these instructions; they do not
change the deployed camera commands, startup timings or settings layout.

Public build version remains `bcoti-personal-3`. This does not denote new BCOTI
hardware. The included application is rebuilt from the source in this repository;
see firmware/manifest.json for the actual build date, hash and source hashes.

## Software checks

Publication checks completed on 2026-11-20. Fresh application size: **948,688
bytes (0xE79D0)**, leaving **99,888 bytes** in the existing 1 MiB partition.
Image checksum/validation hash, packaged binary/source hashes and all test groups
below passed. Only trailing whitespace was normalized in three firmware files;
no camera/control behavior was changed for publication.

- ESP-IDF v5.3.1 native Windows build, ESP32-C3, checked-in sdkconfig/lockfile.
- Actual-C host tests with Windows MSVC AddressSanitizer: NVS set/commit/readback
  failure; simulated restore; bounded UART retries; frame fragmentation, late ACK,
  CRC/status/noise/timeout/tick-wrap handling; preserved camera-video startup;
  crop/mode/preset retention; invalid HTTP input; save retry; JSON backup roundtrip;
  import validation; disabled-only Detail preset; redundant-command avoidance.
- Chromium phone UI tests at 320/390/768 pixels: limits, errors, hold/release,
  remembered alignment, export/import, Detail preview, state polling, tooltip
  coverage and no horizontal overflow or JavaScript errors.
- Synthetic flash extraction and invalid partition rejection.
- Backup CLI tests using mocked esptool subprocesses: only read commands, two
  matching full reads, failures/mismatch stop, no overwrite of backup directories.
- Git whitespace validation and publication review for private paths/device dumps.

These are not camera-video simulations. Windows tests use ASan; the optional Linux
runner adds UBSan. The new portable backup wrapper is software-tested; it has not
been rerun against a physical unit for this publication.

## Prior physical-device evidence

The same application source was previously deployed app-only on one ESP32-C3
revision 0.4 BCOTI V2 with 4 MiB flash. Independent verification confirmed the app
and unchanged bootloader/table/NVS/PHY. Serial logs confirmed configuration
restoration, camera readiness, PAL output query, crop command transmission and
matching flip readback. The owner reported visible X/Y response, retained settings
after power cycling, working middle-button presets, and working updated WebUI.

Private logs, serial identity, alignment values and flash/NVS dumps are deliberately
not published. The freshly rebuilt binary has not been flashed during repository
preparation. Other camera/display assemblies remain unverified. A rollback image
was captured/validated, but a physical rollback was not exercised.

## What remains unproven

- Optical acceptance/readback of arbitrary crop coordinates (no crop query here).
- Better recognition distance or useful Detail-mode improvements in field tests.
- Any reduction of sensor-to-display video latency.
- Independent OLED mirroring/color-state capability or retention on every driver.
- Universal compatibility with other resolutions, lenses, chips or partition layouts.

Keep burn protection and automatic shutter calibration enabled. Do not interpret
sharpened edges as newly captured detail. Follow the cold-boot and optical checks
after changes; passing tests is not a replacement for looking through the device.
