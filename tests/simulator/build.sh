#!/usr/bin/env bash
# Desktop simulator of the NOVA firmware (Linux): runs the real UI, scanner and apps
# against a simulated radio, and saves every screen as a picture in out/.
#   LGFX_DIR=/path/to/LovyanGFX/src tests/simulator/build.sh
#   ./tests/simulator/nsim [theme] [clean]     screenshots in out/
#   ./tests/simulator/nsim serve               speaks the USB protocol on stdin/stdout
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
LG="${LGFX_DIR:?Set LGFX_DIR to the LovyanGFX src folder}"
N="$ROOT/firmware/nova"
OBJ="$HERE/obj"; mkdir -p "$OBJ" "$HERE/out"
FLAGS="-std=c++17 -O1 -g -Wall -Wno-unused-function -DNOVA_SIM -DLGFX_LINUX_FB -I$HERE -I$N -I$LG"
for f in lgfx/v1/lgfx_v1_panel.cpp lgfx/v1/lgfx_v1.cpp lgfx/v1/lgfx_fonts.cpp lgfx/v1/LGFXBase.cpp lgfx/v1/lgfx_v1_touch.cpp lgfx/v1/lgfx_v1_platforms.cpp lgfx/v1/panel/Panel_M5HDMI.cpp; do
  o="$OBJ/$(basename "${f%.cpp}").o"
  [ -f "$o" ] || g++ $FLAGS -c "$LG/$f" -o "$o"
done
g++ $FLAGS "$HERE/sim_main.cpp" "$HERE/sim_hw.cpp" \
  $N/Scanner.cpp $N/Stations.cpp $N/Schedule.cpp $N/Usb.cpp $N/Ghost.cpp $N/UiApps.cpp $N/Tools.cpp $N/UiTools.cpp $N/UiScan.cpp \
  $N/UiVisual.cpp $N/System.cpp $N/Ui.cpp $N/UiStations.cpp $N/UiTune.cpp $N/UiMenu.cpp $N/Wifi.cpp $N/Splash.cpp \
  "$OBJ"/*.o -lpthread -Wl,--unresolved-symbols=ignore-all -o "$HERE/nsim"
echo "Built $HERE/nsim"
