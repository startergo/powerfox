#!/bin/sh
# Builds libWidevineLegacyShim.dylib into dist-10.7/widevine/.
# The dylib must be copied next to XUL in Contents/MacOS for GMPLoader to
# find it in the GMP child process.
set -e
DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
OUT="${1:-$DIR/../dist-10.7/widevine}"
mkdir -p "$OUT"
cc -arch x86_64 -mcx16 -mmacosx-version-min=10.7 -dynamiclib \
  -Wl,-reexport_library,/usr/lib/libSystem.B.dylib \
  -Wl,-current_version,1351.0.0 -Wl,-compatibility_version,1.0.0 \
  -install_name @loader_path/libWidevineLegacyShim.dylib \
  -o "$OUT/libWidevineLegacyShim.dylib" \
  "$DIR/shim.c" "$DIR/hooks.s" -lobjc
