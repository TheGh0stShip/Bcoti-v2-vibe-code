# BCOTI V2 — align it, save it, turn it on and use it

**This firmware exists to make an existing BCOTI V2 remember and reapply its
thermal-overlay alignment, and to make adjusting it from a phone less frustrating.**
It is a reliability and usability update—not a new thermal camera or a claim of
greater identification range.

BCOTI projects a thermal image into an analog night-vision device. For that overlay
to be useful, the thermal outline needs to line up with what the night-vision tube
shows. Getting it aligned once is only half the job: the device also needs to save
those settings and actually send them back to the camera after power-off.

This project started with one personal BCOTI V2 on a right-eye PVS-14, where the
existing bridge and mounting hardware prevented the usual bottom mounting
position. The goal was to keep that hardware, make small X/Y corrections while
looking through the device, and stop having to revisit configuration at startup.
It was developed for that unit, not as a universal redesign of every BCOTI assembly.

## Why the original behavior needed work

The baseline firmware had configuration writes without the required NVS commit.
That made persistence unreliable. There was also a separate problem to address:
**values appearing in the WebUI do not prove that the camera has applied them.**
Startup timing, ignored command failures, and settings overridden during preset
application needed attention independently of storage.

The work was informed by the upstream [settings report](https://github.com/Jacob-OTW/BCOTI/issues/5)
and [camera flip/alignment limitation](https://github.com/Jacob-OTW/BCOTI/issues/6).
Missing commits are a concrete source defect; they are not claimed to explain
every instance of a camera failing to apply settings after a restart.

## What this changes in practice

| Problem or frustration | Change in this firmware | What you gain |
| --- | --- | --- |
| Alignment/settings may not survive power-off reliably | Configuration writes are checked, committed, and read back | A meaningful save result, with visible failure reporting instead of silent success |
| Saved numbers can return without the camera visibly using them | Restore settings before application; wait for camera startup; check transmissions and use bounded retries | Startup is designed to restore the overlay without opening the WebUI each time |
| Status replies can be misread when a late acknowledgement arrives first | Parse separate framed replies and validate length, status and CRC | Fixes the false readback warning observed during development on the tested unit |
| Changing presets can override saved preferences or disturb alignment | Use saved preset preferences and reapply global alignment at the end of the image-setting sequence | Keep one alignment while switching thermal presentation modes |
| Fine alignment from a phone is awkward while wearing night vision | Single-step X/Y buttons, hold-to-repeat, sliders, center reset, limits and automatic saving | Make a small adjustment and immediately judge it through the eyepiece |
| It is unclear which preset is active or what a control does | Scene-based preset names, idle status refresh, simpler menus and help for every control | Understand what you are changing and see physical-button preset changes reflected on the page |
| Experimenting makes it easy to lose a useful setup | A page-session alignment checkpoint plus downloadable/importable settings backups | Undo alignment experiments or restore a previously exported setup |
| Every edit can unnecessarily resend image commands | Skip camera commands for healthy metadata-only edits and unchanged values | Less unnecessary camera reconfiguration—not a claim of faster video |
| Updating firmware risks losing the original working configuration | Read-only full-flash backup tools, matching-read checks, app-only flashing and rollback instructions | Preserve a recovery path before trying the firmware |

## What using it should feel like

1. Open Wi-Fi configuration once and select **Digital alignment**.
2. While looking at a warm target through the night-vision device, tap X/Y to
   bring the outlines together. Side mounting or mirroring may reverse the apparent
   direction, so use the direction you actually observe.
3. Wait for **Saved**, then download a settings backup to your phone.
4. Turn the device fully off. On the next normal startup, it restores the saved
   configuration without requiring a phone connection.
5. Use the middle button to switch enabled presets without intentionally changing
   the global alignment.

That is the intended result: **align once, then power on and use it**, provided
the mounting hardware has not moved and the physical error is within the available
optical/electronic adjustment range. A full power-cycle and visual check are still
part of validating your own installation.

## What is fixed versus what is experimental?

**Implemented and tested in software:** persistence/error handling, startup
application, UART reply parsing, settings validation, alignment controls, preset
handling, backup/import, and the new interface. On the development unit, the owner
also reported working X/Y changes, retained settings after power cycling, working
middle-button modes, and the updated WebUI. That is evidence from one device,
not a guarantee for every camera/display assembly.

**Experimental:** the optional **Detail preset** is a convenient comparison of
existing camera processing settings. It does not add pixels, and improved distant
identification has not been demonstrated. Creating it requires confirmation before
replacing a disabled spare preset; it does not automatically overwrite enabled ones.

**Not optically verified by firmware:** a successful storage readback or camera
reply cannot prove the thermal outline is aligned. The page distinguishes saved
settings, transmitted commands and verified flip status. X/Y crop acceptance still
requires looking through the device. See [validation and limitations](docs/VALIDATION.md).

## Is this for my hardware?

The target is an existing **BCOTI V2 / ESP32-C3 / Mini2 CV256 256x192 / LZE039
analog-video display**, using **ESP-IDF v5.3.1**. It retains the legacy 132-byte
settings layout and uses application-only updates on the verified partition layout.
No replacement mount or 6-o'clock position is required by the firmware; it also
cannot guarantee that digital adjustment will cover every mounting error.

Based on Jacob-OTW's `V2_Beta` source. This is an independent, AI-assisted
adaptation, not an upstream release. **`bcoti-personal-3` is the firmware version;
it is not the rumored BCOTI V3 hardware or a digital-video conversion.**

## Get it, use it, or change it

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

## What this does not improve

The ESP32 controls the Mini2; it does not capture/process its analog video.
There is **no browser camera stream**, added resolution, proven identification-range
increase, or claimed video-latency reduction. Camera flip and digital alignment
are mutually exclusive. Independent OLED mirroring depends on the installed
driver. No 90-degree rotation is added. At 1.1x the existing software limits are
X=116–140 and Y=87–105; zoom is not silently increased to conceal physical error.
The existing analog video path is unchanged. This is not a sensor, lens, display
or optical-resolution upgrade, and it cannot correct image roll or scale mismatch
using X/Y translation alone.

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
