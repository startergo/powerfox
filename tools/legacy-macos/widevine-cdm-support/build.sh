#!/bin/sh
# Builds the Widevine legacy-CDM support files into dist-10.7/widevine/:
#   - libWidevineLegacyShim.dylib, copied next to XUL in Contents/MacOS
#   - LocalAuthentication/CryptoTokenKit stub frameworks for
#     Contents/Frameworks (the CDM hard-depends on them; 10.10+ has them
#     natively) and libpmenergy/libpmsample stub dylibs for Contents/MacOS
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

make_fw() {
  NAME="$1" SRC="$2" EXTRA="$3"
  FW="$OUT/$NAME.framework"
  mkdir -p "$FW/Versions/A/Resources"
  cc -arch x86_64 -mmacosx-version-min=10.7 -dynamiclib $EXTRA \
    -install_name "/System/Library/Frameworks/$NAME.framework/Versions/A/$NAME" \
    -o "$FW/Versions/A/$NAME" "$SRC" -lobjc -framework Foundation
  ln -sf A "$FW/Versions/Current"
  ln -sf Versions/Current/$NAME "$FW/$NAME"
  ln -sf Versions/Current/Resources "$FW/Resources"
  printf 'APPL????\n' > "$FW/Versions/A/Resources/Info.plist"
}

make_fw LocalAuthentication "$DIR/la.m" ""
: > "$OUT/empty.c"
make_fw CryptoTokenKit "$OUT/empty.c" ""

for PM in libpmenergy libpmsample; do
  cc -arch x86_64 -mmacosx-version-min=10.7 -dynamiclib \
    -install_name "/usr/lib/$PM.dylib" \
    -o "$OUT/$PM.dylib" "$OUT/empty.c"
done
rm -f "$OUT/empty.c"
