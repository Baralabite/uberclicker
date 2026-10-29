# Dependency notices and firmware source

UberClicker's authored source is [MIT licensed](../LICENSE). “Noncommercial”
describes this project's operation and does not restrict downstream reuse.
Third-party code retains its licenses. The linked firmware includes **GxEPD2
under GPLv3**; the combined firmware binary is distributed under GPLv3 terms,
with the project's MIT permissions retained for its own source. It is not an
MIT-only binary.

Each firmware release includes `COPYING-GPL-3.txt`, `COPYING-LGPL-2.1.txt`, `firmware-source.zip`,
`THIRD_PARTY_NOTICES.md` and `SHA256SUMS`. The source archive contains the public
firmware/contracts/scripts and the actual installed Arduino core and library
sources, including their copyright/license texts. It excludes credentials,
private reference projects, generated build caches and compiler binaries.
Use the pinned `firmware/platformio.ini` and release build script to rebuild.
`manifest.json` identifies the source commit and exact candidate version. No
signing keys, encrypted firmware, secure boot or installation lock is introduced.

| Component | Build pin | License/source |
| --- | --- | --- |
| GxEPD2 | 1.6.9 | GPLv3; https://github.com/ZinggJM/GxEPD2 |
| RadioLib | 7.1.2 | MIT; https://github.com/jgromes/RadioLib |
| Adafruit GFX | 1.12.4 | BSD; https://github.com/adafruit/Adafruit-GFX-Library |
| Adafruit BusIO | 1.17.4 | MIT; https://github.com/adafruit/Adafruit_BusIO |
| ArduinoJson | 7.4.2 | MIT; https://github.com/bblanchon/ArduinoJson |
| Arduino ESP32 core | 3.3.11 via pioarduino 55.03.311 | Per-file LGPL-2.1-or-later/Apache-2.0 and retained third-party terms; https://github.com/espressif/arduino-esp32 |
| ESP-IDF/mbedTLS and vendor SDK | Pinned platform's ESP32-S3 SDK | Apache-2.0 and retained third-party terms; https://github.com/espressif/esp-idf |
| esptool-js installer | 0.7.0 | Apache-2.0; https://github.com/espressif/esptool-js |
| pako in installer bundle | 2.1.0 | MIT/Zlib; https://github.com/nodeca/pako |

The browser installer retains local esptool-js/pako licenses in
`website/dist/flash/vendor/`. The approved website retains Three.js MIT and Barlow
SIL OFL notices in its `vendor/` and `assets/` directories. Capacitor, the BLE plugin
and secure storage package retain their npm/native license texts; app artifact
packaging supplies a companion notices file. The build tools (PlatformIO,
TypeScript, Vite, compilers, Android SDK and Xcode) are separate dependencies,
not project-authored software or a claim of store publication.
