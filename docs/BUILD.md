# Build and modify it yourself

## Prerequisites

Use **ESP-IDF v5.3.1**, target **ESP32-C3**, with the checked-in `sdkconfig` and
dependency lock. This is not an Arduino/PlatformIO project. Install Git, Python
and the toolchain using Espressif's [version-specific setup guide](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32c3/get-started/index.html).
Do not upgrade IDF or change target merely to resolve a setup error.

On Windows, install/select ESP-IDF 5.3.1 with the Espressif tools installer, then
open its **ESP-IDF PowerShell** shortcut. On Linux/macOS, a typical installation is:

```sh
git clone --branch v5.3.1 --recursive https://github.com/espressif/esp-idf.git esp-idf-v5.3.1
cd esp-idf-v5.3.1
./install.sh esp32c3
. ./export.sh
```

Use the same activated shell for subsequent commands. Verify `idf.py --version`
reports v5.3.1. Installation needs Internet access; the first build may download
the managed components locked in `dependencies.lock`.

## Clone and build

```sh
git clone https://github.com/TheGh0stShip/Bcoti-v2-vibe-code.git
cd Bcoti-v2-vibe-code
idf.py -C Esp32_Files/ESP_IDF_Project build
```

Windows PowerShell alternative: `./tools/Build.ps1`. The wrapper checks IDF 5.3.1
and **only builds**. It does not select a USB port, erase or flash anything.

Outputs in `Esp32_Files/ESP_IDF_Project/build/`:

- `DIY_COTI.bin`: application image for offset **0x10000** on the verified layout.
- `DIY_COTI.elf` and `.map`: debugging/link information; not flash backups.
- `partition_table/partition-table.bin`: generated table for compatibility review.
- `bootloader/bootloader.bin`: generated bootloader; not needed for app-only updates.

Do **not** copy the broad `idf.py flash` command printed at the end of the build.
Follow [backup and app-only deployment](FLASHING.md). The repository includes
only an application binary under `firmware/`; it deliberately does not supply a
merged full-device image or someone else's settings.

## Rebuild provenance

`firmware/manifest.json` identifies the included binary and source-file hashes.
Source hashes normalize CRLF to LF, matching the repository's text attributes.
Run `python tools/verify_package.py` to verify the binary and application source
against that manifest (expected to report a mismatch after you modify the source).
Compare it with `Get-FileHash firmware/BCOTI-personal-v3.bin` on PowerShell or
`sha256sum firmware/BCOTI-personal-v3.bin` on Linux. Builds on different days or
paths can differ because compile timestamps and build metadata are embedded.
Functional source correspondence is not a promise of bit-for-bit reproducibility.
For a reviewed new package, `python tools/package_firmware.py` copies the built
application and generates the manifest, and `python tools/collect_idf_notices.py`
collects SDK license notices. Both refuse to overwrite an existing package; use a
new release directory/version when maintaining later releases, rather than hiding
changes to an already reviewed artifact. Neither script flashes or publishes.

## Where to edit

| File | Purpose |
| --- | --- |
| `main/index.html` | Main UI, help text, phone controls, polling, backup/import |
| `main/debug.html` | Explicit repair controls and warnings |
| `main/main.c` | Settings/NVS, HTTP validation, startup and button behavior |
| `components/Mini2/Mini2.c` | UART framing, camera commands and application sequence |
| `components/Mini2/Mini2.h` | Camera enums and preset/alignment structure definitions |
| Project `CMakeLists.txt` | Application version |

Paths above are relative to `Esp32_Files/ESP_IDF_Project`. HTML is embedded in the
firmware: editing it requires rebuilding/reflashing to change the device page.
For UI-only work, use the fixture below before touching hardware.

Create a local branch, make one change at a time, and preserve a working binary
plus your own flash backup. Update `PROJECT_VER` and the startup banner for a new
release. Do not casually alter structure order/types: static assertions protect
the legacy 132-byte NVS layout. A new layout needs an explicit migration design.
Never add automatic chip erasure or internal-camera parameter saves at boot.

The HTTP interface accepts one setting per POST `/set`. GET `/get` returns a
snapshot. Restore uses a versioned compact array; tests verify length/ranges and
reject invalid input before camera/NVS changes. Keep frontend and backend schemas
in sync. The existing spelling `edge_enhancment_gear` is part of this API.

## Run tests

### C driver, persistence and HTTP tests

From the repository root in an activated IDF environment:

```sh
python tests/test_core.py
```

Linux needs GCC with AddressSanitizer and UndefinedBehaviorSanitizer. Windows
needs Visual Studio C++ tools (`cl.exe`) including AddressSanitizer: activate an
x64 Native Tools environment as well as IDF, then run the same Python command.
The test harness uses actual source functions with mocked UART/NVS/HTTP I/O.
Windows runs ASan; Linux additionally runs UBSan. Neither simulates optical video.

### Phone UI

Use a separate Python environment if preferred; installing Playwright into the
firmware environment is unnecessary:

```sh
python -m venv .venv
# Windows: .venv\Scripts\activate
# Linux/macOS: . .venv/bin/activate
python -m pip install playwright
python -m playwright install chromium
python tools/ui_fixture.py
```

Leave the fixture terminal running; in another terminal with that environment
activated, run `python tests/test_ui.py`. The fixture binds **127.0.0.1:8765**,
never the device. Stop it with Ctrl+C afterward. Screenshots go to ignored
`tests/artifacts/`. This is a development mock, not a video stream or camera emulator.

### Backup tools

After a firmware build, with IDF activated:

```sh
python tests/test_backup.py
python tests/test_backup_cli.py
git diff --check
```

These tests use synthetic data/mock processes, not a connected BCOTI. They check
partition extraction, rejection paths and read-only backup command sequencing.
Before actual deployment, repeat the physical power-cycle, image, preset-button
and Wi-Fi tests in USAGE.md. A software pass does not establish optical alignment.
