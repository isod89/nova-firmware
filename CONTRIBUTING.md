# Contributing to NOVA

Thanks for helping! Bug reports, ideas, translations of the docs, screenshots from real radios and code are all welcome.

## Reporting a problem
Open an issue with the NOVA version (Settings > About), your radio (V3 or V4), the build (OSPI or QSPI) and what you did.
A photo of the screen helps a lot.

## Changing the code
1. Fork, create a branch.
2. Build both variants: `tools/build-firmware.sh` (see [docs/building.md](docs/building.md)). The build should have no warnings.
3. Run the simulator and look at the screens you touched: `tests/simulator`.
4. If you change the editor or the installer, rebuild them and try them in Chrome.
5. Open a pull request describing what changed and how you tested it.

## Style
- C++17, two spaces, braces on their own line for functions and blocks, as in the existing files.
- Comments explain *why*, in plain English.
- UI text is short, plain English; the radio shows it in capitals by default, so keep words short enough to fit.
- No new heap allocations in the audio and tuning paths; large buffers go to PSRAM.
- The radio must stay usable without the editor and without Wi-Fi.

## Licence
By contributing you agree that your work is released under the GPL-3.0, like the rest of NOVA.
