#!/bin/bash
# Builds a darwin10 (Mac OS X 10.6) C++ runtime for bundling into PowerFox:
# libc++ 5.0.1, libc++abi 5.0.1 and headers, with install names under @rpath
# so the app's Contents/MacOS/ directory (see browser/installer/Makefile.in)
# satisfies them.
#
# libc++abi is built without HAVE___CXA_THREAD_ATEXIT_IMPL where possible:
# it then weak-checks the libc symbol and falls back to its own pthread-key
# destructor list where libSystem lacks it (pre-10.10), so thread_local
# destructors actually run. The 10.6 target rejects the fallback's __thread
# bookkeeping (no TLS support), so there the stub stays and thread_local
# destructors are dropped. The math shim declares llround/llrint, which
# exist in 10.6's libSystem but are missing from the 10.6 SDK's math.h.
#
# Usage: tools/legacy-macos/build-libcxx.sh [sdk-path] [macos-version]
# Output: tools/legacy-macos/dist[-<version>]/lib/{libc++.1.0.dylib,libc++abi.1.0.dylib}
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
MACOS_VERSION="${2:-10.6}"
SDKROOT="${1:-/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.6.sdk}"
DOWNLOADS="$ROOT/downloads"
if [ "$MACOS_VERSION" = "10.6" ]; then
  DIST="$ROOT/dist"
else
  DIST="$ROOT/dist-${MACOS_VERSION}"
fi
CXX="${CXX:-clang++}"
CC="${CC:-clang}"

LLVM_TAG="llvmorg-5.0.1"
LIBCXX_SRC="$DOWNLOADS/libcxx-5.0.1.src"
LIBCXXABI_SRC="$DOWNLOADS/libcxxabi-5.0.1.src"


[ -d "$SDKROOT" ] || { echo "10.6 SDK not found at $SDKROOT" >&2; exit 1; }

mkdir -p "$DOWNLOADS" "$DIST/lib" "$DIST/include"

fetch() {
  local name="$1"
  if [ ! -d "$DOWNLOADS/$name" ]; then
    echo "Downloading $name..."
    curl -fsSL "https://releases.llvm.org/5.0.1/$name.tar.xz" | tar -xJ -C "$DOWNLOADS"
  fi
}
fetch libcxx-5.0.1.src
fetch libcxxabi-5.0.1.src

TARGET_FLAGS="-target x86_64-apple-macos${MACOS_VERSION} -isysroot $SDKROOT"

# libc++abi's internal thread_atexit fallback does not compile for these
# targets (its __thread bookkeeping); define HAVE so its wrapper calls the
# implementation in _stub.c below.
ABI_THREAD_DEFS="-DHAVE___CXA_THREAD_ATEXIT_IMPL"

cat > "$DOWNLOADS/math_shim.h" <<'EOF'
#ifndef POWERFOX_MATH_SHIM_H
#define POWERFOX_MATH_SHIM_H
#ifdef __cplusplus
extern "C" {
#endif
long long llrintf(float);
long long llrintl(long double);
long long llrint(double);
long long llroundf(float);
long long llroundl(long double);
long long llround(double);
#ifdef __cplusplus
}
#endif
#endif
EOF

echo "=== Building libc++abi ==="
cd "$LIBCXXABI_SRC/lib"
rm -f *.o _stub.o
for FILE in ../src/*.cpp; do
  base="$(basename "$FILE")"
  # cxa_demangle.cpp depends on libc++ (circular); cxa_noexception.cpp
  # duplicates cxa_exception.cpp symbols.
  [ "$base" = "cxa_demangle.cpp" ] && continue
  [ "$base" = "cxa_noexception.cpp" ] && continue
  $CXX -c -O2 $TARGET_FLAGS -std=c++11 \
    -nostdinc++ -isystem "$LIBCXX_SRC/include" -I../include \
    -DNDEBUG $ABI_THREAD_DEFS -D_LIBCPP_DISABLE_AVAILABILITY \
    -Wno-sign-conversion -Wno-shadow -Wno-conversion -Wno-shorten-64-to-32 \
    "$FILE"
done
printf '%s\n' \
  'char* __cxa_demangle(const char* mangled, char* buf, unsigned long* n, int* status) { return 0; }' \
  > _stub.c
# A real __cxa_thread_atexit_impl: the previous stub returned -1, which
# made libc++abi drop every thread_local destructor registration. This is
# the pthread-key registry libSystem itself uses; destructors run in
# reverse registration order at thread exit. dso_handle is not tracked, so
# registrations of a library that is later dlclose'd are not purged.
cat >> _stub.c <<'STUB'
#include <pthread.h>
#include <stdlib.h>
struct pf_dtor_entry {
  void (*dtor)(void*);
  void* obj;
  struct pf_dtor_entry* next;
};
static pthread_key_t pf_thread_dtor_key;
static pthread_once_t pf_thread_dtor_once = PTHREAD_ONCE_INIT;
static void pf_run_thread_dtors(void* p) {
  struct pf_dtor_entry* e = p;
  while (e) {
    struct pf_dtor_entry* n = e->next;
    e->dtor(e->obj);
    free(e);
    e = n;
  }
}
static void pf_make_thread_dtor_key(void) {
  pthread_key_create(&pf_thread_dtor_key, pf_run_thread_dtors);
}
int __cxa_thread_atexit_impl(void (*dtor)(void*), void* obj,
                             void* dso_handle) {
  (void)dso_handle;
  if (pthread_once(&pf_thread_dtor_once, pf_make_thread_dtor_key) != 0) {
    return -1;
  }
  struct pf_dtor_entry* e = malloc(sizeof *e);
  if (!e) {
    return -1;
  }
  e->dtor = dtor;
  e->obj = obj;
  e->next = pthread_getspecific(pf_thread_dtor_key);
  if (pthread_setspecific(pf_thread_dtor_key, e) != 0) {
    free(e);
    return -1;
  }
  return 0;
}
STUB
$CC -c -O2 $TARGET_FLAGS -DNDEBUG _stub.c -o _stub.o
$CC $TARGET_FLAGS -o "$DIST/lib/libc++abi.1.0.dylib" \
  -dynamiclib -nodefaultlibs \
  -current_version 5.0.1 -compatibility_version 1 \
  -install_name @rpath/libc++abi.1.dylib \
  -lSystem *.o

echo "=== Building libc++ ==="
cd "$LIBCXX_SRC/src"
rm -f *.o
for FILE in *.cpp; do
  # debug.cpp only builds the _LIBCPP_DEBUG-mode abort/throw helpers; its
  # __libcpp_debug_exception is not defined in this configuration.
  [ "$(basename "$FILE")" = "debug.cpp" ] && continue
  $CXX -c -O2 $TARGET_FLAGS -std=c++11 \
    -nostdinc++ -isystem "$LIBCXX_SRC/include" \
    -I"$LIBCXXABI_SRC/include" -include "$DOWNLOADS/math_shim.h" \
    -DLIBCXX_BUILDING_LIBCXXABI -DNDEBUG -D_LIBCPP_DISABLE_AVAILABILITY \
    -Wno-sign-conversion -Wno-shadow -Wno-conversion -Wno-shorten-64-to-32 \
    "$FILE"
done
$CXX $TARGET_FLAGS -o "$DIST/lib/libc++.1.0.dylib" \
  -dynamiclib -nodefaultlibs \
  -current_version 1.0.5 -compatibility_version 1 \
  -install_name @rpath/libc++.1.dylib \
  -Wl,-reexport_library,"$DIST/lib/libc++abi.1.0.dylib" \
  -lSystem *.o

echo "=== Installing headers ==="
cp -R "$LIBCXX_SRC/include/" "$DIST/include/"
cp "$LIBCXXABI_SRC/include/cxxabi.h" "$DIST/include/"
cp "$LIBCXXABI_SRC/include/__cxxabi_config.h" "$DIST/include/"

echo "=== Done ==="
ls -lh "$DIST/lib/"
