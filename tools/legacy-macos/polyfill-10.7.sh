#!/bin/sh
# Polyfill a freshly built .app for pre-10.10 macOS, mirroring the Momiji CI
# (aobaharuki2005/momiji-web-browser, build-test workflows):
#   1. build libMacportsLegacySystem.B.dylib from the local fork of
#      macports-legacy-support (branch 1.5.2_addCCRandom + our
#      _availability_version_check addition), which re-exports libSystem
#      and fills the symbol gaps of 10.6-10.9,
#   2. copy it into Contents/MacOS and rewrite /usr/lib/libSystem.B.dylib
#      to it for the three dylibs that carry post-10.9 symbol references,
#   3. rewrite CoreText to its 10.7 home inside ApplicationServices.
# Usage: polyfill-10.7.sh <path/to/Nightly.app>
set -e
APP="$1"
[ -d "$APP" ] || { echo "usage: $0 <app>" >&2; exit 1; }
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SRCROOT="$HERE/legacysupport"
MACOS="$APP/Contents/MacOS"

( cd "$SRCROOT" && \
  CC="clang --target=x86_64-apple-macos10.6" \
  CXX="clang++ --target=x86_64-apple-macos10.6" \
  CFLAGS="-Os" CXXFLAGS="-Os" \
  LDFLAGS="--target=x86_64-apple-macos10.6" make -s )

cp "$SRCROOT/lib/libMacportsLegacySystem.B.dylib" "$MACOS/"

cd "$MACOS"
LEGACY="@executable_path/libMacportsLegacySystem.B.dylib"
# Momiji rewrites only XUL/libmozavutil/libgkcodecs; builds made against the
# 26.5 SDK carry post-10.9 libSystem references in more images (libmozglue
# needs clock_gettime_nsec_np, for one), so rewrite libSystem everywhere.
# Safe by construction: the legacy dylib re-exports the real libSystem.
for BIN in $(find . -type f -perm +111 ! -name "libMacportsLegacySystem.B.dylib"); do
  file -b "$BIN" | grep -q "Mach-O" || continue
  otool -L "$BIN" 2>/dev/null | grep -q "/usr/lib/libSystem.B.dylib" || continue
  install_name_tool -change "/usr/lib/libSystem.B.dylib" "$LEGACY" "$BIN"
  # 10.7 locates CoreText inside ApplicationServices.
  install_name_tool -change \
    "/System/Library/Frameworks/CoreText.framework/Versions/A/CoreText" \
    "/System/Library/Frameworks/ApplicationServices.framework/Versions/A/ApplicationServices" \
    "$BIN" 2>/dev/null || true
  codesign --remove-signature "$BIN" 2>/dev/null || true
done

echo "== libSystem rewrites applied:"
n=$(otool -L XUL | grep -c "$LEGACY"); echo "  XUL: $n"
find . -type f -perm +111 -name "*.dylib" | while read -r BIN; do
  otool -L "$BIN" 2>/dev/null | grep -q "$LEGACY" && echo "  $BIN ok"
done
echo "== audit: compat symbols the legacy dylib must cover:"
nm -gU libMacportsLegacySystem.B.dylib | awk '{print $NF}' | sort > /tmp/pf_legacy_exports.txt
for BIN in XUL libmozavutil.dylib libgkcodecs.dylib libmozglue.dylib; do
  [ -f "$BIN" ] || continue
  nm -u "$BIN" | awk '{print $NF}' | grep -E \
    '_clock_gettime|_getentropy|_CCRandomGenerate|_availability_version|_dirfd|_fdopendir|_fstatat|INODE64|___exp10|_mach_continuous|_setattrlist' \
    | while read -r SYM; do
        grep -qx "$SYM" /tmp/pf_legacy_exports.txt || echo "  UNCOVERED in $BIN: $SYM (relies on runtime libSystem)"
      done
done
echo "== done"
