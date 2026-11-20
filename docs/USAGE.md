# Using the firmware

[WebUI preview](images/webui.png) (local test fixture with synthetic settings,
not a live camera or anyone's saved alignment).

## Normal startup and Wi-Fi

Power on normally to restore saved alignment and the last selected preset.
Camera setup includes a five-second startup wait and bounded checks; allow roughly
ten seconds. No page needs to be opened. Turn up the brightness knob above minimum.

For configuration, hold the middle button while powering on, release after about
five seconds, wait for startup, and connect your phone to **BCOTI** with password
**wifipass1234**. Open **http://192.168.4.1**, not HTTPS. Accept the phone's option
to remain on a network without Internet; temporarily disable mobile data if it
routes away. Refresh if an old page is cached. Power off/on without the button
after configuration to leave Wi-Fi off.

## Align once, then verify after power-off

1. Seat the existing mount consistently. Choose a stationary warm object with a
   clear outline at the intended viewing distance. Keep focus and eye position
   consistent. Near/far parallax cannot be removed everywhere by one translation.
2. Choose **Digital alignment**. Saved zoom is retained; the default is 1.1x.
3. Tap X + and watch its actual direction, then undo; repeat for Y. At a side-mounted
   right-eye installation, movements may appear diagonal or reversed. Do not assume
   X means visual left/right. Bottom mounting is not a firmware requirement.
4. Use single steps or hold a button. Release to stop. Each completed step is saved;
   slider changes apply/save on release. Wait for **Saved**, then check the image.
5. **Remember alignment** stores a checkpoint in the open page only.
   **Restore remembered** applies and saves that checkpoint at the same zoom.
   Reloading the page loses it; use a downloaded backup for lasting protection.
6. **Restore center** saves X=128, Y=96 without changing zoom.
7. Turn off and remove USB/all external power for ten seconds. Boot normally and
   inspect the image before opening Wi-Fi. Then check that middle-button preset
   changes preserve alignment. Finally check saved X/Y in Wi-Fi mode if desired.

At 1.1x the software limits are X=116–140 and Y=87–105. These are not a measured
guarantee of every camera's useful edge behavior. If remaining offset exceeds
the usable range, firmware cannot remove it without other tradeoffs. Translation
does not fix roll, optical scale mismatch or a shifted mount. Do not increase zoom
just to hide misalignment: it changes thermal/night-vision scale matching.

## Orientation and white-hot output

Camera flip cannot be used with digital zoom/alignment. Camera-flip mode retains
saved crop values but disables those controls. If your OLED driver supports it,
hold **brightness +** about four seconds while running to mirror X; holding it
during startup toggles Y. These are independent hardware-driver controls and
cannot be detected/read back through this firmware.

The intended combination is **camera White Hot** plus the OLED driver's
**full-color mode**. The driver cycles its three color modes by holding brightness
minus about four seconds; other modes can illuminate the background. Leave a
working dark background alone. The displayed palette is the requested camera
palette, not measured OLED color. No 90-degree rotation exists here.

## Presets and Advanced controls

The selector names each stored preset by scene mode. It can preview disabled
presets; the physical middle button skips disabled ones. Preset 0 is always in
the cycle. Existing NVS presets are retained; new-device defaults need not match
another user's presets. Idle polling updates the page about every three seconds.

Expand **Advanced image settings** for scene mode, edge thickness, contrast,
sharpening/detail, inclusion in the button cycle and the existing breathing effect.
Tooltips, focus/touch help, and the **Control help** catalogue explain each item.
Higher sharpening is not extra resolution and can exaggerate noise or halos.

**Try a Detail preset** asks before replacing the highest disabled slot numbered
2–4. Download a backup first if you want its old values. It previews General
full-image mode, White Hot, contrast 50, edges 0, detail 55, shutter correction and
burn protection on, breathing off. Alignment and enabled presets are preserved.
The slot stays outside the button cycle until enabled. It may obscure more of the
night-vision image; improved recognition/range has not been demonstrated.

## Settings backup/import

Under **Backup and restore**, download a JSON file to your phone. It contains
alignment and all image presets. Import requires confirmation and device-side
validation of the entire snapshot before application/commit. It replaces those
settings, not firmware, video timing, OLED configuration or internal calibration.
It is not a substitute for a full flash backup before firmware updates.

## Read the status accurately

- **Saved**: NVS write/commit/readback matched (or a validated boot restore).
- **Commands sent**: UART transmission completed, not an optical measurement.
- **Flip verified**: a valid camera reply matched the requested flip mode.
- **Crop: visual check required**: X/Y/zoom acceptance is not read back.

If saving fails, keep power on and use **Reapply / retry save**; RAM/image may
already reflect a change that did not reach persistent storage. If a request
times out, reload status rather than assuming it failed. A missing camera reply
does not alone mean no thermal video. Check the image and serial logs.

`/debug` is for explicit recovery. **Repair Camera** changes internal parameters
and video configuration; do not use it for ordinary alignment or on a working
camera. Its confirmation prompt is intentional. Leave periodic flip refresh off
unless troubleshooting camera-flip mode. Do not disable protection or calibration
to pursue speed. There is no firmware claim of reduced video latency.
