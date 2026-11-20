# Attribution and modification notice

This repository adapts the firmware from **BCOTI (Budget-Clip-On-Thermal-Imager)**
by **Jacob-OTW**: <https://github.com/Jacob-OTW/BCOTI>.
Base: branch `V2_Beta`, commit `3d0a60b33ea24dbb9db645e426a9cef9cd25c97e`.
The unmodified upstream license text is included as LICENSE.md:
Creative Commons Attribution-NonCommercial 4.0 International.
License reference: <https://creativecommons.org/licenses/by-nc/4.0/>.
Adaptations in this repository are distributed under that same license.
No endorsement by the original author is claimed.

Changes: checked NVS persistence/restoration; bounded camera readiness/application;
UART framing, CRC and late-ACK parsing; preset restoration; request validation;
phone alignment controls, accessibility/help, status polling and settings export/
import; opt-in Detail preset; regression tests and backup/deployment tooling.
These changes were developed with AI assistance and reviewed/tested as described
in docs/VALIDATION.md. This is a firmware-only source snapshot, not a mirror of
all upstream CAD assets or upstream commit history.

ESP-IDF and downloaded managed components retain their respective upstream
licenses. The dependency manager fetches dependencies described in
`main/idf_component.yml` and `dependencies.lock`. Their licenses are not replaced
by this repository's license. See the generated managed_components directories
and the ESP-IDF source for those notices when redistributing dependencies.
Collected upstream SDK/component license texts accompany the prebuilt application
in `firmware/THIRD_PARTY_NOTICES.txt` (including some components not linked).
