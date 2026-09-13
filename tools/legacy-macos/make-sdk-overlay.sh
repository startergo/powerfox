#!/bin/bash
# Creates tools/legacy-macos/overlay-sdk with patched copies of 10.6 SDK
# headers that newer clangs reject. Add the overlay dir to CFLAGS/CXXFLAGS
# with -isystem (see mozconfig-macos106) so these win over the SDK's own
# headers.
#
# Usage: tools/legacy-macos/make-sdk-overlay.sh [sdk-path]
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
SDKROOT="${1:-/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.6.sdk}"
OVERLAY="$ROOT/overlay-sdk"

[ -f "$SDKROOT/System/Library/Frameworks/Security.framework/Headers/SecKeychain.h" ] || {
  echo "SecKeychain.h not found in $SDKROOT" >&2
  exit 1
}

mkdir -p "$OVERLAY/Security"

# SecKeychain.h: the little-endian AUTH_TYPE_FIX_ macro shifts multichar
# constants, which newer clangs reject as non-constant. The big-endian
# branch already uses the value directly; make it do the same.
LC_ALL=C sed -e \
  's@((x >> 24) | ((x >> 8) & 0xff00) | ((x << 8) & 0xff0000) | (x & 0xff) << 24)@(x)@' \
  "$SDKROOT/System/Library/Frameworks/Security.framework/Headers/SecKeychain.h" \
  > "$OVERLAY/Security/SecKeychain.h"
echo "Wrote $OVERLAY/Security/SecKeychain.h"

# Security.h: the pre-Lion umbrella does not include SecItem.h or the
# CodeSigning APIs.
LC_ALL=C sed -e 's@#include <Security/SecBase.h>@#include <Security/SecBase.h>\n#include <Security/CSCommon.h>\n#include <Security/CodeSigning.h>\n#include <Security/SecCode.h>\n#include <Security/SecItem.h>@' \
  "$SDKROOT/System/Library/Frameworks/Security.framework/Headers/Security.h" \
  > "$OVERLAY/Security/Security.h"
cat >> "$OVERLAY/Security/Security.h" <<'SECEOF'

#if !defined(MAC_OS_X_VERSION_10_7) || \
    MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_7
// 10.7+ Security symbols absent from the 10.6 SDK.
extern const CFStringRef kSecClassGenericPassword __attribute__((weak_import));
extern const CFStringRef kSecClassCertificate __attribute__((weak_import));
extern CFDictionaryRef SecPolicyCopyProperties(SecPolicyRef policy)
    __attribute__((weak_import));
extern const CFStringRef kSecPolicyOid __attribute__((weak_import));
extern const CFStringRef kSecPolicyAppleSSL __attribute__((weak_import));
#endif
SECEOF
echo "Wrote $OVERLAY/Security/Security.h"

# Accessibility.h: the standalone Accessibility framework does not exist
# pre-Lion; its AX C API lives in the HIServices sub-framework.
mkdir -p "$OVERLAY/Accessibility"
cat > "$OVERLAY/Accessibility/Accessibility.h" <<'EOF'
#ifndef POWERFOX_ACCESSIBILITY_COMPAT_H
#define POWERFOX_ACCESSIBILITY_COMPAT_H
#include <ApplicationServices/ApplicationServices.h>
#endif
EOF
echo "Wrote $OVERLAY/Accessibility/Accessibility.h"

# objc.h: the (BOOL) casts make @YES/@NO unparseable as boxed expressions.
mkdir -p "$OVERLAY/objc"
LC_ALL=C sed -e 's/#define YES             (BOOL)1/#define YES 1/; s/#define NO              (BOOL)0/#define NO 0/' \
  "$SDKROOT/usr/include/objc/objc.h" > "$OVERLAY/objc/objc.h"
# nil expands to __DARWIN_NULL (__null), which is ambiguous for C++ smart
# pointer assignments; modern SDKs use nullptr.
printf '\n#ifdef __cplusplus\n#undef nil\n#define nil nullptr\n#endif\n' >> "$OVERLAY/objc/objc.h"
echo "Wrote $OVERLAY/objc/objc.h"

# string.h: a shim ahead of the SDK's own header, appending declarations
# of libc functions that exist in 10.6's libSystem but are missing from its
# headers (strnlen is 10.7). A distinct guard avoids clashing with the SDK
# header's own _STRING_H_ guard through libc++'s include_next.
cat > "$OVERLAY/string.h" <<'EOF'
#ifndef _POWERFOX_STRING_SHIM_
#define _POWERFOX_STRING_SHIM_
#include_next <string.h>
#include <sys/cdefs.h>
__BEGIN_DECLS
size_t strnlen(const char *__s, size_t __maxlen);
__END_DECLS
#endif
EOF
echo "Wrote $OVERLAY/string.h"

# assert.h: a shim ahead of the SDK's own header, back-porting the C11
# static_assert macro (a plain keyword only since C23).
cat > "$OVERLAY/assert.h" <<'EOF'
#ifndef _POWERFOX_ASSERT_SHIM_
#define _POWERFOX_ASSERT_SHIM_
#include_next <assert.h>
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && \
    !defined(__cplusplus) && !defined(static_assert)
#define static_assert(cond, msg) _Static_assert(cond, msg)
#endif
#endif
EOF
echo "Wrote $OVERLAY/assert.h"

# cups/cups.h: a shim ahead of the SDK's header, declaring the CUPS 1.6
# destination API missing from pre-10.8 SDKs. The functions are resolved
# at runtime by nsCUPSShim and are absent on those systems.
mkdir -p "$OVERLAY/cups"
cat > "$OVERLAY/cups/cups.h" <<'EOF'
#ifndef _POWERFOX_CUPS_SHIM_
#define _POWERFOX_CUPS_SHIM_
#include_next <cups/cups.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cups_dinfo_s cups_dinfo_t;
typedef struct cups_size_s {
  char media[128];
  char source[64];
  char type[64];
  int width;
  int length;
  int bottom;
  int left;
  int right;
  int top;
} cups_size_t;
typedef int (*cups_dest_cb_t)(void* user_data, unsigned flags, cups_dest_t* dest);
enum { CUPS_DEST_FLAGS_NONE = 0, CUPS_DEST_FLAGS_MORE = 1,
       CUPS_DEST_FLAGS_ERROR = 2, CUPS_DEST_FLAGS_REMOVED = 4,
       CUPS_DEST_FLAGS_DISCONNECTED = 8 };
enum { CUPS_MEDIA_FLAGS_DEFAULT = 0, CUPS_MEDIA_FLAGS_BORDERLESS = 1,
       CUPS_MEDIA_FLAGS_DUPLEX = 2, CUPS_MEDIA_FLAGS_EXACT = 4 };
#define CUPS_MEDIA_READY "media-ready"
#define CUPS_PRINT_COLOR_MODE "print-color-mode"
#define CUPS_PRINT_COLOR_MODE_AUTO "auto"
#define CUPS_PRINT_COLOR_MODE_COLOR "color"
#define CUPS_PRINT_COLOR_MODE_MONOCHROME "monochrome"
#define CUPS_SIDES "sides"
#define CUPS_SIDES_ONE_SIDED "one-sided"
#define CUPS_SIDES_TWO_SIDED_PORTRAIT "two-sided-long-edge"
#define CUPS_SIDES_TWO_SIDED_LANDSCAPE "two-sided-short-edge"
#define CUPS_FINISHINGS "finishings"
#define CUPS_JOB_HOLD_UNTIL "job-hold-until"
#define CUPS_JOB_NAME "job-name"
#define CUPS_JOB_PRIORITY "job-priority"
#define CUPS_MEDIA "media"
#define CUPS_MEDIA_SOURCE "media-source"
#define CUPS_NUMBER_UP "number-up"
#define CUPS_ORIENTATION "orientation-requested"
#define CUPS_PRINT_QUALITY "print-quality"
#define CUPS_RESOLUTION "printer-resolution"
enum { IPP_OP_GET_PRINTER_ATTRIBUTES = 0x000B };
extern int cupsCheckDestSupported(http_t* http, cups_dest_t* dest,
                                  cups_dinfo_t* info, const char* option,
                                  const char* value);
extern http_t* cupsConnectDest(cups_dest_t* dest, unsigned flags,
                               int msec, int* cancel, char* resource,
                               size_t resourcesize, cups_dest_cb_t cb,
                               void* user_data);
extern int cupsCopyDest(cups_dest_t* dest, int num_dests,
                          cups_dest_t** dests);
extern cups_dinfo_t* cupsCopyDestInfo(http_t* http, cups_dest_t* dest);
extern void cupsFreeDestInfo(cups_dinfo_t* info);
extern int cupsGetDestMediaByName(http_t* http, cups_dest_t* dest,
                                  cups_dinfo_t* info, const char* name,
                                  unsigned flags, cups_size_t* size);
extern int cupsGetDestMediaDefault(http_t* http, cups_dest_t* dest,
                                   cups_dinfo_t* info, unsigned flags,
                                   cups_size_t* size);
extern int cupsGetDestMediaCount(http_t* http, cups_dest_t* dest,
                                 cups_dinfo_t* info, unsigned flags);
extern int cupsGetDestMediaByIndex(http_t* http, cups_dest_t* dest,
                                   cups_dinfo_t* info, int index,
                                   unsigned flags, cups_size_t* size);
extern cups_dest_t* cupsFindDest(http_t* http, cups_dest_t* dests,
                                 int num_dests, const char* name,
                                 const char* instance);
extern cups_dest_t* cupsFindDestDefault(http_t* http, cups_dest_t* dests,
                                        int num_dests);
extern const char* cupsLocalizeDestMedia(http_t* http, cups_dest_t* dest,
                                         cups_dinfo_t* info, unsigned flags,
                                         cups_size_t* size, char* buffer,
                                         size_t bufsize);
extern int httpAddrPort(const http_addr_t* addr);
extern http_addr_t* httpGetAddress(http_t* http);
extern int ippGetCount(ipp_attribute_t* attr);
extern const char* ippGetString(ipp_attribute_t* attr, int element,
                                const char* language);
extern int cupsEnumDests(unsigned mgmt, int msec, int* cancel,
                         cups_ptype_t type, cups_ptype_t mask,
                         cups_dest_cb_t cb, void* user_data);
extern int cupsGetDestCount(cups_dest_t* dests);
extern cups_dest_t* cupsGetNamedDest(http_t* http, const char* name,
                                     const char* instance);

#ifdef __cplusplus
}
#endif
#endif
EOF
echo "Wrote $OVERLAY/cups/cups.h"

# CoreText: copy the header set into a framework layout (so framework-style
# includes resolve here before the SDK's sub-framework) and append weak
# declarations of post-10.6 APIs.
CTDIR="$SDKROOT/System/Library/Frameworks/ApplicationServices.framework/Frameworks/CoreText.framework/Headers"
CTFW="$OVERLAY/frameworks/CoreText.framework/Headers"
mkdir -p "$CTFW"
for h in "$CTDIR"/*.h; do cp "$h" "$CTFW/"; done
cat >> "$CTFW/CTFont.h" <<'EOF'

/* PowerFox compat: post-10.6 additions, weak-imported. */
#include <CoreGraphics/CoreGraphics.h>
#if defined(__cplusplus)
extern "C" {
#endif
extern const CFStringRef kCTFontVariationAxesAttribute
    __attribute__((weak_import));
CTFontRef CTFontCreateForStringWithLanguage(CTFontRef iFont, CFStringRef iString,
                                            CFRange iRange, CFStringRef iLanguage)
    __attribute__((weak_import));
void CTFontDrawGlyphs(CTFontRef font, const CGGlyph glyphs[],
                      const CGPoint positions[], size_t count, CGContextRef context)
    __attribute__((weak_import));
#if defined(__cplusplus)
}
#endif
EOF
cat >> "$CTFW/CTFontManager.h" <<'EOF'

/* PowerFox compat: requires macOS 10.13, weak-imported. */
#if defined(__cplusplus)
extern "C" {
#endif
CTFontDescriptorRef CTFontManagerCreateFontDescriptorFromData(CFDataRef)
    __attribute__((weak_import));
#if defined(__cplusplus)
}
#endif
EOF
cat >> "$CTFW/SFNTLayoutTypes.h" <<'EOF2'

/* PowerFox compat: requires macOS 10.8. */
enum {
  kLowerCaseType = 37,
  kLowerCaseSmallCapsSelector = 1,
};
EOF2

echo "Wrote $CTFW"

# os/availability.h: the os headers do not exist pre-Lion; the annotations
# are advisory, so empty macros suffice.
mkdir -p "$OVERLAY/os"
cat > "$OVERLAY/os/availability.h" <<'EOF'
#ifndef POWERFOX_OS_AVAILABILITY_COMPAT_H
#define POWERFOX_OS_AVAILABILITY_COMPAT_H
#ifndef API_AVAILABLE
#define API_AVAILABLE(...)
#endif
#ifndef API_DEPRECATED
#define API_DEPRECATED(...)
#endif
#ifndef API_UNAVAILABLE
#define API_UNAVAILABLE(...)
#endif
#ifndef API_obsoleted
#define API_obsoleted(...)
#endif
#endif
EOF
echo "Wrote $OVERLAY/os/availability.h"

# os/log.h: requires macOS 10.10; provide no-op shims.
cat > "$OVERLAY/os/log.h" <<'EOF'
#ifndef POWERFOX_OS_LOG_COMPAT_H
#define POWERFOX_OS_LOG_COMPAT_H
#include <stdint.h>
typedef struct { void* dummy; }* os_log_t;
typedef uint8_t os_log_type_t;
#define OS_LOG_DEFAULT ((os_log_t)0)
#define OS_LOG_TYPE_DEFAULT 0x00
#define OS_LOG_TYPE_INFO 0x01
#define OS_LOG_TYPE_DEBUG 0x02
#define OS_LOG_TYPE_ERROR 0x10
#define OS_LOG_TYPE_FAULT 0x11
#define os_log_create(subsystem, category) ((os_log_t)0)
#define os_log(log, format, ...)
#define os_log_with_type(log, type, format, ...)
#define os_log_info(log, format, ...)
#define os_log_debug(log, format, ...)
#define os_log_error(log, format, ...)
#define os_log_fault(log, format, ...)
#endif
EOF
echo "Wrote $OVERLAY/os/log.h"

# IOSurface: the modern IOSurfaceRef.h split does not exist pre-Lion.
mkdir -p "$OVERLAY/IOSurface"
cat > "$OVERLAY/IOSurface/IOSurfaceRef.h" <<'EOF'
#ifndef POWERFOX_IOSURFACE_COMPAT_H
#define POWERFOX_IOSURFACE_COMPAT_H
#include <IOSurface/IOSurfaceAPI.h>
#endif
EOF
echo "Wrote $OVERLAY/IOSurface/IOSurfaceRef.h"

# Compat library for symbols missing from the 10.6 libSystem.
DIST="$ROOT/dist/lib"
mkdir -p "$DIST"
CC="${CC:-/usr/bin/clang}"
"$CC" -c "$ROOT/compat-10.6.c" -o "$DIST/compat-10.6.o" -target x86_64-apple-macosx10.6
"$CC" -c "$ROOT/arc-shim.m" -o "$DIST/arc-shim.o" -target x86_64-apple-macosx10.6
"$CC" -c "$ROOT/emutls.c" -o "$DIST/emutls.o" -target x86_64-apple-macosx10.6
# Fold the emutls runtime into the object the mozconfig force-loads into
# every link (rustc -Ztls-model=emulated and clang -femulated-tls reference
# ___emutls_get_address).
LDC=/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld-classic
"$LDC" -r -arch x86_64 -o "$DIST/compat-10.6-full.o" \
  "$DIST/compat-10.6.o" "$DIST/emutls.o"
mv "$DIST/compat-10.6-full.o" "$DIST/compat-10.6.o"
libtool -static -o "$DIST/libpfcompat106.a" "$DIST/compat-10.6.o" \
  "$DIST/arc-shim.o"
echo "Wrote $DIST/libpfcompat106.a"

# Link-time-only stub frameworks for frameworks the 10.6 SDK lacks.
# ld_classic requires -weak_framework targets to exist at link time. They
# carry no headers (nothing may #include them) and no symbols; references to
# their symbols are weak imports that resolve at runtime. The install names
# point at the real system frameworks so dyld loads the genuine framework
# wherever it exists and simply nulls the weak references on 10.6.
stub_framework() {
  local name="$1" install_name="$2"
  local fw="$OVERLAY/frameworks/$name.framework"
  mkdir -p "$fw"
  printf '' | "$CC" -dynamiclib -x objective-c -target x86_64-apple-macosx10.6 \
    -install_name "$install_name" -o "$fw/$name" -
  echo "Wrote $fw"
}
stub_framework LocalAuthentication \
  /System/Library/Frameworks/LocalAuthentication.framework/Versions/A/LocalAuthentication
stub_framework Accessibility \
  /System/Library/Frameworks/Accessibility.framework/Versions/A/Accessibility
stub_framework AVFoundation \
  /System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation
stub_framework CoreMedia \
  /System/Library/Frameworks/CoreMedia.framework/Versions/A/CoreMedia
stub_framework MediaPlayer \
  /System/Library/Frameworks/MediaPlayer.framework/Versions/A/MediaPlayer
stub_framework Metal \
  /System/Library/Frameworks/Metal.framework/Versions/A/Metal
stub_framework ScreenCaptureKit \
  /System/Library/Frameworks/ScreenCaptureKit.framework/Versions/A/ScreenCaptureKit
stub_framework CoreUI \
  /System/Library/PrivateFrameworks/CoreUI.framework/CoreUI
stub_framework CoreSymbolication \
  /System/Library/PrivateFrameworks/CoreSymbolication.framework/CoreSymbolication

