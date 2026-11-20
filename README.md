# BCOTI V2 — persistence, alignment and WebUI improvements

Firmware for an existing **BCOTI V2 / ESP32-C3 / Mini2 CV256 256x192 / LZE039
analog-video display**. Based on Jacob-OTW's BCOTI `V2_Beta` source. This is an
independent, AI-assisted firmware adaptation, not an upstream release or the
rumored BCOTI V3 hardware. Firmware version: **bcoti-personal-3**.

## Start here

- [Use the device and WebUI](docs/USAGE.md)
- [Back up, inspect, flash application-only, and roll back](docs/FLASHING.md)
- [Build from source or modify the firmware](docs/BUILD.md)
- [What changed, test evidence, and limitations](docs/VALIDATION.md)
- [Attribution and license](NOTICE.md)
- [Prebuilt application and SHA-256 manifest](firmware/)

**Back up your own device before flashing.** Do not erase the chip or blindly
flash a bootloader/partition table. This project does not include anyone's NVS,
calibration, serial logs, full-flash dumps, or personal alignment settings.
Use is experimental and at your own risk; compatibility with every assembly is
not established. Do not rely on the overlay alone for identification or navigation.

## Features

- Checked NVS writes, commit and readback; retain the legacy 132-byte settings ABI.
- Restore configuration before camera application, bounded readiness/retries,
  framed camera reply parsing with CRC and late-acknowledgement handling.
- X/Y controls, hold-to-repeat, fixed-zoom limits and automatic persistence.
- Named presets, live status polling, phone-friendly layout, tooltips and help.
- Session alignment checkpoint, JSON settings export/import and optional Detail
  preset in a confirmed disabled slot; existing enabled presets remain untouched.
- Separate storage/TX/readback status. No false claim that camera crop was read back.
- Original middle-button preset cycle and independent OLED controls retained.

## Important limits

The ESP32 controls the Mini2; it does not capture/process its analog video.
There is **no browser camera stream**, added resolution, proven identification-range
increase, or claimed video-latency reduction. Camera flip and digital alignment
are mutually exclusive. Independent OLED mirroring depends on the installed
driver. No 90-degree rotation is added. At 1.1x the existing software limits are
X=116–140 and Y=87–105; zoom is not silently increased to conceal physical error.

## Quick use (already installed)

Hold the middle button while powering on; release after about five seconds.
Connect to **BCOTI**, password **wifipass1234**, then open **http://192.168.4.1**.
Wait for startup, refresh the page, and check the status. In normal use, simply
power on without the button; the saved configuration is restored automatically.

The password is the public upstream default, not a secret. The local HTTP API
has no per-user authentication. Enable Wi-Fi only while configuring, keep it off
untrusted networks, and do not forward it to the Internet.

## Source layout

```text
Esp32_Files/ESP_IDF_Project/  ESP-IDF project, sdkconfig and dependency lock
  main/main.c               NVS, startup, buttons, HTTP API
  main/index.html           Main WebUI (embedded in the application)
  main/debug.html           Explicit recovery controls
  components/Mini2/         Camera UART driver
tools/                      Build, backup and offline inspection utilities
tests/                      Actual-C host tests, phone UI tests, synthetic backups
firmware/                   Prebuilt app only; no bootloader or device dumps
docs/                       Usage, build/edit, deployment/rollback, validation
```

Get the complete source with `git clone
https://github.com/TheGh0stShip/Bcoti-v2-vibe-code.git`, or GitHub's **Code →
Download ZIP**. The firmware project is not the repository root. See the build
guide for ESP-IDF **v5.3.1**, not Arduino or PlatformIO.

## Credits and licensing

Original hardware/software project: [Jacob-OTW/BCOTI](https://github.com/Jacob-OTW/BCOTI),
`V2_Beta` base commit `3d0a60b33ea24dbb9db645e426a9cef9cd25c97e`.
The upstream **CC BY-NC 4.0** license is preserved in [LICENSE.md](LICENSE.md).
This is not an MIT/Apache relicensing and does not imply upstream endorsement.
The modification summary is in [NOTICE.md](NOTICE.md). Hardware CAD and the
upstream build guide remain available in the original repository.
