#!/usr/bin/env bash
# Builds both NOVA firmware variants into build/ospi and build/qspi.
#   OSPI: radios with 8 MB octal PSRAM (most ATS Mini).  QSPI: 2 or 4 MB quad PSRAM.
# Needs arduino-cli with the esp32 core 3.3.12 and the libraries LovyanGFX 1.2.33
# and PU2CLR SI4735 2.1.8 (see docs/building.md).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="${ARDUINO_CLI:-arduino-cli}"
for VAR in ${@:-ospi qspi}; do
  PS=opi; [ "$VAR" = qspi ] && PS=enabled
  OUT="$ROOT/build/$VAR"
  mkdir -p "$OUT"
  echo "== NOVA $VAR =="
  "$CLI" compile \
    --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=$PS,CPUFreq=80,USBMode=hwcdc,FlashMode=qio,PartitionScheme=custom,DebugLevel=none" \
    ${ARDUINO_EXTRA:-} \
    --output-dir "$OUT" --warnings default "$ROOT/firmware/nova"
  rm -f "$OUT"/*.map "$OUT"/*.elf
done
echo "Done: $ROOT/build"
