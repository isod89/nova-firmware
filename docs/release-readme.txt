NOVA - open source firmware for the ATS Mini (ESP32-S3 + SI4732)
https://github.com/isod89/nova-firmware

INSTALL
  1. Open NOVA-Installer.html in Chrome, Edge or Opera (it works offline).
  2. Plug the radio in with a USB data cable, click Connect radio.
  3. Back up the original firmware, then Install.
  4. Switch the radio off and on again when it is done (it does not always
     restart by itself after a USB install).

AFTER INSTALLING
  Open NOVA-Editor.html in Chrome or Edge and click Connect radio for station
  names, the clock and every setting. On the radio: hold the knob for the menu.

IN THIS FOLDER
  NOVA-Installer.html   install, update, back up, restore (both builds inside)
  NOVA-Editor.html      the web editor
  firmware/ospi         most radios (8 MB octal PSRAM)
  firmware/qspi         radios with 2 or 4 MB quad PSRAM
                        nova.ino.bin = update (keeps your data)
                        nova.ino.merged.bin = clean install at 0x0
  scripts/              command line installers (Windows .bat, macOS/Linux .sh)

Documentation: https://github.com/isod89/nova-firmware/tree/main/docs
License: GPL-3.0 (LICENSE), third-party notices in THIRD_PARTY_LICENSES.md
