#!/bin/sh
# Mac OS X 10.7/10.8 ship a libc++ far too old for this tree; the images
# linked against the SDK stub should instead load the bundled 10.7-floor
# libc++ (built by build-libcxx.sh) through @rpath.
# Usage: rpath-libcxx-10.7.sh <path/to/Nightly.app> <dist-lib-dir>
set -e
APP="$1"
DIST="${2:-$(CDPATH= cd -- "$(dirname "$0")" && pwd)/dist-10.7/lib}"
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
  case "$BIN" in
    *.app/Contents/MacOS/*)
      # Nested helper apps resolve @rpath against their own MacOS
      # directory; point them back at the top-level one where the
      # runtimes live.
      RP="@executable_path/$(echo "$BIN" | awk -F/ '{ofs=""; for (i = 2; i < NF; i++) ofs = ofs "../"; print ofs}')"
      ;;
    *)
      RP="@executable_path/."
      ;;
  esac
  if ! otool -l "$BIN" | grep -q "path $RP "; then
    install_name_tool -add_rpath "$RP" "$BIN"
  fi
  codesign --remove-signature "$BIN" 2>/dev/null || true
done

echo "images still referencing /usr/lib/libc++.1.dylib:"
otool -L XUL 2>/dev/null | grep -c "/usr/lib/libc++.1.dylib" || true
