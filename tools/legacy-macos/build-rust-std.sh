#!/bin/bash
# Builds a Rust std for x86_64-apple-darwin with emulated TLS (-Ztls-model=
# emulated), because 10.6 has no dyld TLS support and the distributed std
# uses native TLS. The result is installed as a custom sysroot that
# rustc-emutls passes to rustc via --sysroot, so every crate in the build
# compiles and links against the same emutls std.
#
# Usage: tools/legacy-macos/build-rust-std.sh
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
SYSROOT="$ROOT/dist/rust-sysroot"
TOOLCHAIN="$HOME/.rustup/toolchains/stable-aarch64-apple-darwin"

[ -x "$TOOLCHAIN/bin/rustc" ] || {
  echo "toolchain not found: $TOOLCHAIN" >&2
  exit 1
}
[ -d "$TOOLCHAIN/lib/rustlib/src/rust/library" ] || {
  echo "rust-src component not installed (rustup component add rust-src)" >&2
  exit 1
}

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rust-std-106.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src"
cat > "$WORK/Cargo.toml" <<EOF
[package]
name = "rust-std-host"
version = "0.0.0"
edition = "2021"

[workspace]
EOF
# A lib, so nothing links an executable (the link would fail without the
# emutls runtime, which only the Firefox build provides).
echo 'pub fn host() {}' > "$WORK/src/lib.rs"

mkdir -p "$SYSROOT/lib/rustlib"
# The x86_64 std is absent until `rustup target add x86_64-apple-darwin`;
# -Zbuild-std replaces its contents below anyway, so an empty skeleton
# suffices when the toolchain does not ship it.
for d in aarch64-apple-darwin x86_64-apple-darwin; do
  rm -rf "$SYSROOT/lib/rustlib/$d"
  if [ -d "$TOOLCHAIN/lib/rustlib/$d" ]; then
    cp -R "$TOOLCHAIN/lib/rustlib/$d" "$SYSROOT/lib/rustlib/$d"
  else
    mkdir -p "$SYSROOT/lib/rustlib/$d/lib"
  fi
done

cd "$WORK"
# embed-bitcode: Firefox links gkrust with LTO, which needs bitcode in the
# sysroot rlibs.
STD_FLAGS="-Ztls-model=emulated -Cembed-bitcode=yes"

# Pass 1 (default unwind profile): std and panic_unwind with the unwind
# strategy, for rustc's default -Cpanic=unwind builds.
RUSTC_BOOTSTRAP=1 RUSTFLAGS="$STD_FLAGS" \
  "$TOOLCHAIN/bin/cargo" build \
  --release -Z build-std=std,panic_abort,panic_unwind \
  --target x86_64-apple-darwin

# The Firefox build compiles with -Cpanic=abort, which needs a panic_abort
# crate carrying that strategy. Compiled manually against this sysroot's
# core so the crate hashes stay consistent.
SYSROOT_LIB="$SYSROOT/lib/rustlib/x86_64-apple-darwin/lib"
rm -rf "$SYSROOT_LIB"
mkdir -p "$SYSROOT_LIB"
cp "$WORK/target/x86_64-apple-darwin/release/deps/"lib*.rlib "$SYSROOT_LIB/"
CORE_RLIB="$(ls "$SYSROOT_LIB"/libcore-*.rlib | head -1)"
rm -f "$SYSROOT_LIB"/libpanic_abort-*.rlib
RUSTC_BOOTSTRAP=1 "$ROOT/rustc-emutls" --crate-name panic_abort \
  --crate-type rlib -Cpanic=abort -Copt-level=2 -Ztls-model=emulated \
  -Cembed-bitcode=yes --target=x86_64-apple-darwin \
  --extern core="$CORE_RLIB" \
  "$TOOLCHAIN/lib/rustlib/src/rust/library/panic_abort/src/lib.rs" \
  -o "$SYSROOT_LIB/libpanic_abort-abort.rlib"

rm -f "$SYSROOT/lib/rustlib/x86_64-apple-darwin/lib/librust_std_host"*.rlib
echo "Wrote $SYSROOT/lib/rustlib/x86_64-apple-darwin/lib:"
ls "$SYSROOT/lib/rustlib/x86_64-apple-darwin/lib/"
