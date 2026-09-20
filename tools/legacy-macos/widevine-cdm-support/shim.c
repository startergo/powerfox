/* Support dylib for loading the Widevine CDM on pre-10.12 macOS.
 *
 * The CDM is built against a modern macOS: it hard-imports libSystem
 * symbols that don't exist before 10.12 (the os_log family, clock_gettime
 * variants, ...) and calls objc_msgSend with selectors taken from embedded
 * C strings, which old libobjc runtimes don't canonicalize.
 *
 * This dylib reexports the real libSystem and fills those gaps; when dyld's
 * flat namespace is flipped on for the CDM load (done by GMPLoader), the
 * CDM's undefined symbols resolve against this image. It also exports
 * canonicalizing objc_msgSend hooks that GMPLoader rebinds the CDM's
 * message-send slots to.
 */

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <mach/mach_time.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

struct os_log_s { char unused; };
static struct os_log_s dummy_log = {0};
const struct os_log_s* _os_log_default = &dummy_log;

void* os_log_create(const char* subsystem, const char* category) { return (void*)&dummy_log; }
void os_log_destroy(void* log) {}
void os_log_with_type(void* log, uint8_t type, const char* format, ...) {}
void os_log_info(void* log, const char* format, ...) {}
void os_log_debug(void* log, const char* format, ...) {}
void os_log_error(void* log, const char* format, ...) {}
void os_log_fault(void* log, const char* format, ...) {}
void _os_log_impl(void* log, void* buf, uint32_t sz) {}
bool os_log_type_enabled(void* log, uint8_t type) { return false; }
void os_release(void* object) {}

uint64_t clock_gettime_nsec_np(uint32_t clk) {
  static mach_timebase_info_data_t tb;
  if (tb.denom == 0) mach_timebase_info(&tb);
  return mach_absolute_time() * tb.numer / tb.denom;
}

extern void arc4random_buf(void*, size_t);
int getentropy(void* buf, size_t len) {
  arc4random_buf(buf, len);
  return 0;
}

void* aligned_alloc(size_t alignment, size_t size) {
  void* p = 0;
  if (posix_memalign(&p, alignment, size) == 0) return p;
  return 0;
}

int clock_gettime(clockid_t clk, struct timespec* ts) {
  struct timeval tv;
  gettimeofday(&tv, 0);
  ts->tv_sec = tv.tv_sec;
  ts->tv_nsec = tv.tv_usec * 1000;
  return 0;
}

int pthread_set_qos_class_self_np(void) { return 0; }
void swap_linkedit_data_command(void) {}

int os_signpost_enabled(void* log) { return 0; }
void* _os_signpost_emit_with_name_impl = 0;

#include <objc/objc.h>
#include <objc/runtime.h>
#include <dlfcn.h>
#include <stdio.h>

void* real_msgSend;
void* real_msgSend_stret;
void* real_msgSend_fpret;
void* real_msgSendSuper;

SEL canon_sel_c(SEL cmd) {
  if (!cmd) return cmd;
  return sel_registerName((const char*)cmd);
}

__attribute__((constructor)) static void init_hooks(void) {
  void* h = dlopen("/usr/lib/libobjc.A.dylib", RTLD_LAZY);
  if (h) {
    real_msgSend = dlsym(h, "objc_msgSend");
    real_msgSend_stret = dlsym(h, "objc_msgSend_stret");
    real_msgSend_fpret = dlsym(h, "objc_msgSend_fpret");
    real_msgSendSuper = dlsym(h, "objc_msgSendSuper");
  }
}
