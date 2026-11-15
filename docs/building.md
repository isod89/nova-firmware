# Building NOVA

## Firmware

Requirements:

- [arduino-cli](https://arduino.github.io/arduino-cli/) (or the Arduino IDE 2)
- ESP32 Arduino core **3.3.12** (board manager URL `https://espressif.github.io/arduino-esp32/package_esp32_index.json`)
- Libraries: **LovyanGFX 1.2.33** and **PU2CLR SI4735 2.1.8**

```sh
arduino-cli core install esp32:esp32@3.3.12
arduino-cli lib install "LovyanGFX@1.2.33" "PU2CLR SI4735@2.1.8"
tools/build-firmware.sh            # build/ospi and build/qspi
tools/build-firmware.sh ospi       # one build only
```

Board options (set by the script): ESP32S3 Dev Module, USB CDC on boot, 8 MB flash, QIO, custom partitions
(`firmware/nova/partitions.csv`), 80 MHz CPU, PSRAM *OPI* (OSPI build) or *enabled* (QSPI build).

In the Arduino IDE: open `firmware/nova/nova.ino`, choose *ESP32S3 Dev Module* with the options above.

## Editor and installer

```sh
python3 editor/build.py         # dist/NOVA-Editor.html
python3 installer/build.py      # dist/NOVA-Installer.html (after the firmware build)
tools/make-release.sh           # everything, and dist/NOVA-<version>.zip
```

`esptool-js` 0.7.1 (Apache-2.0) is vendored in `editor/vendor/esptool-js`.

## Fonts

`firmware/nova/Fonts.h` holds the Inter typeface rendered as smooth VLW fonts. To change sizes or weights, edit
`tools/fontgen/mkvlw.py` and run it (needs `pip install freetype-py` and the Inter `.otf` files in `INTER_DIR`).

## Simulator

`tests/simulator` compiles the real firmware for Linux against a simulated SI4732 with a few dozen stations. It runs
a long scenario (scans, every screen, every app, the Ghost Box features) and saves each screen as a picture; it can
also speak the USB protocol on stdin/stdout to test the editor.

```sh
LGFX_DIR=/path/to/LovyanGFX/src tests/simulator/build.sh
cd tests/simulator && ./nsim 5 clean      # theme 5 (NOVA), without toasts: pictures in out/
./nsim splash                             # the boot animation, frame by frame
python3 ../../tools/docs-images.py out    # refresh docs/images/screens
```

## Releasing

1. Set `NOVA_VERSION` in `firmware/nova/Nova.h`, update `CHANGELOG.md` and `docs/release-notes.md`.
2. Tag: `git tag v2.0.0 && git push --tags`.
3. GitHub Actions builds everything, publishes the release with the zip and the installer, and updates the website.
