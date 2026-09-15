![PowerFox](https://github.com/user-attachments/assets/35623fd8-1887-4976-ae16-cf85ca800a8c)

<img src="https://powerfox.jazzzny.me/assets/img/2026-09-01-new-powerfox.png" alt="PowerFox screenshot" style="max-width: 400px;">

PowerFox is a fast, reliable and private web browser based on Firefox 153 ESR for Mac OS X 10.7 Lion - macOS 10.14 Mojave.

For more information, visit the [PowerFox website](https://powerfox.jazzzny.me).

## Looking for PowerFox Classic?
PowerFox Classic was formerly named PowerFox. PowerFox Classic, which supports Intel and PowerPC Macs on Mac OS X 10.3-10.6, is available from https://github.com/Jazzzny/powerfox-classic.

PowerFox Classic will continue to receive regular updates.

## Building for Mac OS X 10.6

The tree can be built against the 10.6 SDK with a 10.6 deployment target.
Configure expects an SDK inside Xcode (or pass your own path):

    /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.6.sdk

Build the header overlay and the bundled C++ runtime once (10.6 has no system
libc++; the overlay patches the SDK's `SecKeychain.h` multichar constants
that newer clangs reject), plus an emulated-TLS Rust std (needs
`rustup component add rust-src`):

    tools/legacy-macos/make-sdk-overlay.sh
    tools/legacy-macos/build-libcxx.sh
    tools/legacy-macos/build-rust-std.sh

Then configure with Xcode's clang and build:

    cp mozconfig-macos106 .mozconfig   # or: export MOZCONFIG=$PWD/mozconfig-macos106
    ./mach build && ./mach package

Notes on how this works:

- No clang accepts C++ `thread_local` below a 10.7 deployment target, and
  10.6's dyld has no TLS support. The build therefore compiles at a 10.7
  floor with `-femulated-tls` (no native TLS relocations are emitted) and
  stamps the real 10.6 minimum into the linked images. The emutls runtime
  and the post-10.6 libSystem/libobjc symbols it pulls in live in
  `tools/legacy-macos/compat-10.6.c`, force-loaded into every link.
- 10.7-only AppKit APIs are guarded at runtime and declared in
  `widget/cocoa/SDKDeclarations.h` for pre-Lion SDKs; drag & drop uses the
  pre-10.7 `dragImage:` path.
- The distributed Rust std uses native TLS, so `build-rust-std.sh` builds an
  emutls std from `rust-src` into `tools/legacy-macos/dist/rust-sysroot`,
  which the `rustc-emutls` wrapper passes to every cross-target rustc
  invocation via `--sysroot` (along with a 10.6 link-arg override, since
  rustc clamps its deployment target to 10.12). Changing the sysroot
  requires removing the cargo target dirs under the objdir so every crate
  rebuilds against it.

## Credits
PowerFox would not have been possible without i3roly for the Firefox Dynasty 149 patchset and aobaharuki2005 for inspiration on the toolchain. Thank you!