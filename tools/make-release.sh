#!/usr/bin/env bash
# Builds everything and packs dist/NOVA-<version>.zip, ready for a GitHub release.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
VER="$(sed -n 's/#define NOVA_VERSION *"\(.*\)"/\1/p' firmware/nova/Nova.h)"
[ "${SKIP_FIRMWARE:-0}" = 1 ] || tools/build-firmware.sh
python3 editor/build.py
python3 installer/build.py "$VER"
PKG="dist/NOVA-$VER"
rm -rf "$PKG"; mkdir -p "$PKG/firmware/ospi" "$PKG/firmware/qspi" "$PKG/scripts"
for v in ospi qspi; do
  cp build/$v/nova.ino.bin build/$v/nova.ino.bootloader.bin build/$v/nova.ino.partitions.bin build/$v/nova.ino.merged.bin "$PKG/firmware/$v/"
  cp installer/boot_app0.bin "$PKG/firmware/$v/"
done
cp dist/NOVA-Installer.html dist/NOVA-Editor.html "$PKG/"
cp scripts/nova-install.bat scripts/nova-install.sh "$PKG/scripts/"
cp LICENSE THIRD_PARTY_LICENSES.md "$PKG/"
cp docs/release-readme.txt "$PKG/README.txt"
(cd dist && rm -f "NOVA-$VER.zip" && zip -qr "NOVA-$VER.zip" "NOVA-$VER")
( cd dist && sha256sum "NOVA-$VER.zip" > "NOVA-$VER.zip.sha256" )
echo "Release ready: dist/NOVA-$VER.zip"
