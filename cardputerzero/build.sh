#!/usr/bin/env bash
# Builds HM-01 for the M5Stack CardputerZero (Debian on a Raspberry Pi CM0,
# arm64) and its .deb, on any Linux with an aarch64 cross compiler:
#
#   sudo apt install g++-aarch64-linux-gnu dpkg-dev curl
#   cardputerzero/build.sh            -> cardputerzero/build/hm01, build/hm01-hi-monitor_<ver>_arm64.deb
#
# The binary is linked statically, so it does not depend on the device's
# glibc version. CXX=g++ builds a host binary instead (no .deb).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
core="$root/firmware/lib/hm_core/src"
out="$here/build"
CXX="${CXX:-aarch64-linux-gnu-g++}"
STRIP="${STRIP:-aarch64-linux-gnu-strip}"
version="$(tr -d '\r' < "$root/firmware/src/config.h" | sed -n 's/^#define FW_VERSION "\(.*\)"/\1/p')"
mkdir -p "$out/include"

# ArduinoJson (header only): PlatformIO's copy when there is one, else the
# release's single header
aj_inc="$out/include"
if [ -f "$root/firmware/.pio/libdeps/native/ArduinoJson/src/ArduinoJson.h" ]; then
    aj_inc="$root/firmware/.pio/libdeps/native/ArduinoJson/src"
elif [ ! -f "$out/include/ArduinoJson.h" ]; then
    curl -fsSL -o "$out/include/ArduinoJson.h" \
        https://github.com/bblanchon/ArduinoJson/releases/download/v7.4.2/ArduinoJson-v7.4.2.h
fi

echo "HM-01 $version for the CardputerZero, with $CXX"
"$CXX" -std=gnu++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
    -DHM01_VERSION="\"$version\"" -I"$core" -I"$aj_inc" \
    "$core"/*.cpp "$here"/src/*.cpp \
    -static -pthread -o "$out/hm01"

if [ "$CXX" != "aarch64-linux-gnu-g++" ]; then
    echo "host build: $out/hm01"
    exit 0
fi
"$STRIP" "$out/hm01"

# The .deb: the binary, the launcher entry and its icon under the
# launcher's root (/usr/share/APPLaunch). Staged in a Linux temp directory:
# dpkg-deb refuses the 777 permissions of a Windows drive under WSL.
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
pkg="$stage/deb"
mkdir -p "$pkg/DEBIAN" "$pkg/usr/share/APPLaunch/bin" "$pkg/usr/share/APPLaunch/applications" \
         "$pkg/usr/share/APPLaunch/share/images"
install -m 0755 "$out/hm01" "$pkg/usr/share/APPLaunch/bin/hm01"
install -m 0644 "$here/packaging/hm01.desktop" "$pkg/usr/share/APPLaunch/applications/hm01.desktop"
install -m 0644 "$here/packaging/hm01_100.png" "$pkg/usr/share/APPLaunch/share/images/hm01_100.png"
sed "s/@VERSION@/$version/" "$here/packaging/control" > "$pkg/DEBIAN/control"
deb="$out/hm01-hi-monitor_${version}_arm64.deb"
dpkg-deb --build --root-owner-group "$pkg" "$deb" >/dev/null
ls -l "$out/hm01" "$deb"
