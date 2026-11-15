#!/usr/bin/env bash
# NOVA - open source firmware for the ATS Mini (ESP32-S3 + SI4732)
# Command line installer for macOS and Linux. The easiest way is NOVA-Installer.html;
# use this script if your browser cannot. Needs Python 3 (esptool is installed with pip).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BACKUPS="$ROOT/backups"
PORT="${1:-}"

if command -v esptool >/dev/null 2>&1; then ESPTOOL=(esptool)
elif command -v esptool.py >/dev/null 2>&1; then ESPTOOL=(esptool.py)
elif python3 -c "import esptool" 2>/dev/null; then ESPTOOL=(python3 -m esptool)
else
  echo "Installing esptool (Espressif, official) for this user..."
  python3 -m pip install --user --quiet esptool || { echo "Could not install esptool. Run: python3 -m pip install esptool"; exit 1; }
  ESPTOOL=(python3 -m esptool)
fi
PORTARG=()
[ -n "$PORT" ] && PORTARG=(--port "$PORT")
run() { "${ESPTOOL[@]}" --chip esp32s3 "${PORTARG[@]}" "$@"; }

variant() {
  local info; info="$(run flash-id 2>&1 || true)"
  echo "$info" | grep -E "Chip|PSRAM|flash size" || true
  echo "$info" | grep -q "Detected flash size" || { echo "The radio did not answer. Hold BOOT while plugging it in, then retry."; return 1; }
  if echo "$info" | grep -qE "PSRAM (8|16)MB"; then VARIANT=ospi
  elif echo "$info" | grep -qE "PSRAM (2|4)MB"; then VARIANT=qspi
  else
    read -rp "Memory type not reported. Build: [O]SPI (most radios) or [Q]SPI? " a
    [[ "$a" =~ ^[Qq] ]] && VARIANT=qspi || VARIANT=ospi
  fi
}

while true; do
  cat <<MENU

  NOVA command line installer
  1  Back up the radio's current firmware (8 MB, about 2 min)
  2  Clean install of NOVA (erases everything)
  3  Update NOVA (keeps stations and settings)
  4  Restore a backup
  5  Quit
MENU
  read -rp "  Choose 1-5: " c
  case "$c" in
    1) mkdir -p "$BACKUPS"; out="$BACKUPS/ats-mini-backup-$(date +%Y%m%d-%H%M).bin"
       run --baud 921600 read-flash 0 ALL "$out" && echo "Backup saved: $out" ;;
    2) variant && read -rp "Clean install of NOVA ($VARIANT), everything is erased. Continue? [y/N] " y &&
       [[ "$y" =~ ^[Yy] ]] && run --baud 921600 write-flash -z --flash-mode keep --flash-freq keep --flash-size keep 0x0 "$ROOT/firmware/$VARIANT/nova.ino.merged.bin" &&
       echo "NOVA is installed. Open NOVA-Editor.html in Chrome or Edge for the station names, the clock and all settings." ;;
    3) variant && run --baud 921600 write-flash -z --flash-mode keep --flash-freq keep --flash-size keep 0x10000 "$ROOT/firmware/$VARIANT/nova.ino.bin" &&
       echo "NOVA updated. Stations and settings were kept." ;;
    4) ls -1 "$BACKUPS"/*.bin 2>/dev/null || echo "  (no backups in $BACKUPS)"
       read -rp "  Backup file (full path): " f
       [ -f "$f" ] && run --baud 921600 write-flash -z 0x0 "$f" && echo "Backup restored." || echo "File not found." ;;
    5) exit 0 ;;
  esac
done
