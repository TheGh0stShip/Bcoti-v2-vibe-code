# Prebuilt application

Download [BCOTI-personal-v3.bin](BCOTI-personal-v3.bin) using GitHub's **Download raw
file** button. This is **application-only**, not a full flash image. Its SHA-256,
size and normalized source hashes are in [manifest.json](manifest.json).

Built locally from this repository's source using ESP-IDF v5.3.1 for ESP32-C3.
The rebuilt artifact has not been flashed during publication; the application
source was previously tested on one BCOTI V2. See [validation](../docs/VALIDATION.md).

**Read [backup, compatibility, app-only flashing and rollback](../docs/FLASHING.md)
before using this file.** Expected application offset: 0x10000; partition size:
0x100000. Never erase the entire chip or flash other people's NVS/settings.

For a source build, follow [BUILD.md](../docs/BUILD.md). For normal operation,
follow [USAGE.md](../docs/USAGE.md). SDK/component notices are included in
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
