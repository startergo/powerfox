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
  "$DIR/shim.c" "$DIR/subscripting.m" "$DIR/hooks.s" -lobjc -framework Foundation

# The harness links without a C++ runtime library (host11.cpp defines its
# own operator new/delete and pure-virtual trap; compiled -fno-rtti) so the
# binary loads on 10.6.
cc -arch x86_64 -mmacosx-version-min=10.6 -fno-exceptions \
  -c "$DIR/harness.c" -o "$OUT/harness.o"
cc -arch x86_64 -mmacosx-version-min=10.6 -fno-exceptions -fno-rtti \
  -c "$DIR/host11.cpp" -o "$OUT/host11.o"
cc -arch x86_64 -mmacosx-version-min=10.6 -o "$OUT/widevine-cdm-harness" \
  "$OUT/harness.o" "$OUT/host11.o" -lobjc -ldl -lpthread
rm -f "$OUT/harness.o" "$OUT/host11.o"

make_fw() {
  NAME="$1" SRC="$2" EXTRA="$3"
  FW="$OUT/$NAME.framework"
  mkdir -p "$FW/Versions/A/Resources"
  # The CDM enforces compatibility versions on its dependencies; mirror the
  # real 10.10-era numbers so dyld accepts the stubs.
  cc -arch x86_64 -mmacosx-version-min=10.7 -dynamiclib $EXTRA \
    -install_name "/System/Library/Frameworks/$NAME.framework/Versions/A/$NAME" \
    -Wl,-compatibility_version,1.0.0 -Wl,-current_version,1.0.0 \
    -o "$FW/Versions/A/$NAME" "$SRC" -lobjc -framework Foundation
  ln -sf A "$FW/Versions/Current"
  ln -sf Versions/Current/$NAME "$FW/$NAME"
  ln -sf Versions/Current/Resources "$FW/Resources"
  printf 'APPL????\n' > "$FW/Versions/A/Resources/Info.plist"
}

make_fw LocalAuthentication "$DIR/la.m" ""
: > "$OUT/empty.c"
make_fw CryptoTokenKit "$OUT/empty.c" ""

# The CDM hard-references top-level CoreGraphics and CoreText, which only
# exist there from 10.8; on 10.6/10.7 they are ApplicationServices
# subframeworks. Symlink stubs inside the bundle resolve through the
# DYLD_FALLBACK_FRAMEWORK_PATH the loader sets, so a stock install needs
# no system changes.
for SUB in CoreGraphics CoreText; do
  rm -rf "$OUT/$SUB.framework"
  ln -s "/System/Library/Frameworks/ApplicationServices.framework/Versions/A/Frameworks/$SUB.framework" \
        "$OUT/$SUB.framework"
done

for PM in libpmenergy libpmsample; do
  cc -arch x86_64 -mmacosx-version-min=10.7 -dynamiclib \
    -install_name "/usr/lib/$PM.dylib" \
    -Wl,-compatibility_version,1.0.0 -Wl,-current_version,2.0.0 \
    -o "$OUT/$PM.dylib" "$OUT/empty.c"
done
rm -f "$OUT/empty.c"
