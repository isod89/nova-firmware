# Third-party licenses

NOVA is released under the GNU General Public License v3.0 (see `LICENSE`). It includes or uses the following work.

| Component | Where | Author | License |
|---|---|---|---|
| Display driver setup, SI4735 patch wrapper, button handling (`Display.h`, `SI4735-fixed.h`, `patch_init.h`, `Button.*`) | firmware | ATS Mini project (esp32-si4732/ats-mini) | MIT, see `firmware/nova/LICENSE-ats-mini.txt` |
| Rotary encoder driver (`Rotary.*`) | firmware | Ben Buxton | GPL-3.0 |
| PU2CLR SI4735 library | firmware (library) | Ricardo Lima Caratti | MIT |
| SI4732 SSB patch | firmware | Silicon Labs, distributed with the PU2CLR library | as distributed |
| LovyanGFX | firmware (library) | lovyan03 | FreeBSD |
| Arduino core for the ESP32 | firmware (platform) | Espressif Systems | LGPL-2.1 |
| Inter typeface, rendered in `Fonts.h` and used by the web pages | firmware, editor, installer | The Inter Project Authors | SIL Open Font License 1.1 |
| esptool-js 0.7.1 | editor, installer | Espressif Systems | Apache-2.0, see `editor/vendor/esptool-js/LICENSE` |
| esptool (downloaded by the command line scripts) | scripts | Espressif Systems | GPL-2.0 |
| EiBi shortwave schedules | downloaded by the user, not included | Eike Bierwirth, eibispace.de | free for personal use |

The Inter glyphs are converted to anti-aliased VLW bitmaps without changing their design. The font is not sold on its own.
