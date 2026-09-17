#!/bin/sh
# Real Mac OS X 10.6 has no /usr/lib/libc++.1.dylib; the images linked
# against the SDK stub must instead load the bundled darwin10 libc++
# (built by build-libcxx.sh) through @rpath.
# Usage: rpath-libcxx-10.6.sh <path/to/Nightly.app> <dist-lib-dir>
set -e
APP="$1"
DIST="${2:-$(CDPATH= cd -- "$(dirname "$0")" && pwd)/dist/lib}"
MACOS="$APP/Contents/MacOS"

[ -d "$MACOS" ] || { echo "usage: $0 <app> [dist-lib]" >&2; exit 1; }
[ -f "$DIST/libc++.1.0.dylib" ] || { echo "libc++.1.0.dylib not found in $DIST" >&2; exit 1; }

cp "$DIST/libc++.1.0.dylib" "$MACOS/libc++.1.dylib"
cp "$DIST/libc++abi.1.0.dylib" "$MACOS/libc++abi.1.dylib"

cd "$MACOS"
find . -type f | while read -r BIN; do
  file -b "$BIN" | grep -q "Mach-O" || continue
  otool -L "$BIN" 2>/dev/null | grep -q "/usr/lib/libc++.1.dylib" || continue
  install_name_tool -change /usr/lib/libc++.1.dylib @rpath/libc++.1.dylib "$BIN"
  install_name_tool -add_rpath @executable_path/. "$BIN" 2>/dev/null || true
  codesign --remove-signature "$BIN" 2>/dev/null || true
done

echo "images still referencing /usr/lib/libc++.1.dylib:"
otool -L XUL 2>/dev/null | grep -c "/usr/lib/libc++.1.dylib" || true
