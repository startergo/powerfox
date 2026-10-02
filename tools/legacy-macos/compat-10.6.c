/* Symbols that modern toolchains emit references to but the 10.6 libSystem
   does not export. Linked into every binary via -lpfcompat106. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <sys/time.h>
#include <mach/mach_time.h>
#include <mach/mach.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/syslimits.h>
#include <sys/stat.h>
#include <math.h>
#include <dlfcn.h>

static void* pf_libsystem(const char* name) {
  static void* handle;
  if (!handle) handle = dlopen("/usr/lib/libSystem.B.dylib", RTLD_LAZY);
  return handle ? dlsym(handle, name) : NULL;
}

static void* pf_libframework(const char* path, const char* name) {
  void* handle = dlopen(path, RTLD_LAZY);
  return handle ? dlsym(handle, name) : NULL;
}

void arc4random_buf(void* buf, size_t n) {
  uint8_t* p = (uint8_t*)buf;
  while (n >= 4) {
    uint32_t r = arc4random();
    memcpy(p, &r, 4);
    p += 4;
    n -= 4;
  }
  if (n) {
    uint32_t r = arc4random();
    memcpy(p, &r, n);
  }
}

/* Matches libSystem's availability_version: 8 bytes, minor/patch are 16-bit. */
typedef struct {
  uint32_t major;
  uint16_t minor;
  uint16_t patch;
} pf_tav_t;


/* Called by clang's __isPlatformVersionAtLeast (used by @available) with a
   list of minimum versions; returns true when the running OS satisfies at
   least one entry. Mirrors the libSystem implementation added in 10.12,
   including its two-argument (count, versions) form. */
/* libc's symbol carries a single leading underscore in its C name. */
static void pf_os_version(int32_t* maj, int32_t* min, int32_t* patch) {
  static int32_t osMajor = -1, osMinor = -1, osPatch = -1;
  if (osMajor < 0) {
    char buf[64];
    size_t sz = sizeof(buf);
    if (sysctlbyname("kern.osproductversion", buf, &sz, NULL, 0) == 0) {
      int a = 0, b = 0, c = 0;
      sscanf(buf, "%d.%d.%d", &a, &b, &c);
      osMajor = a;
      osMinor = b;
      osPatch = c;
    } else {
      struct utsname u;
      if (uname(&u) == 0) {
        int darwin = atoi(u.release);
        if (darwin >= 20) {
          osMajor = darwin - 9;
          osMinor = 0;
        } else {
          osMajor = 10;
          osMinor = darwin - 4;
        }
        osPatch = 0;
      }
    }
    if (osMajor < 0) {
      osMajor = 10;
      osMinor = 6;
      osPatch = 0;
    }
  }
  *maj = osMajor;
  *min = osMinor;
  *patch = osPatch;
}

uint8_t _availability_version_check(uint32_t count, const pf_tav_t* versions) {
  int32_t osMajor, osMinor, osPatch;
  pf_os_version(&osMajor, &osMinor, &osPatch);
  for (uint32_t i = 0; i < count; i++) {
    const pf_tav_t* v = &versions[i];
    if ((uint32_t)osMajor > v->major) return 1;
    if ((uint32_t)osMajor == v->major) {
      if ((uint32_t)osMinor > v->minor) return 1;
      if ((uint32_t)osMinor == v->minor && (uint32_t)osPatch >= v->patch) {
        return 1;
      }
    }
  }
  return 0;
}

/* Strong definition overriding clang's weak emitted helpers: their
   libSystem-dependent fallback returns true on systems without the
   AvailabilityVersionCheck pointer (10.6), making every @available()
   check pass. */
int __isPlatformVersionAtLeast(uint32_t platform, uint32_t major,
                               uint32_t minor, uint32_t patch) {
  int32_t osMajor, osMinor, osPatch;
  pf_os_version(&osMajor, &osMinor, &osPatch);
  if (platform != 1 /* PLATFORM_MACOS */) {
    return 0;
  }
  if (osMajor > major) return 1;
  if (osMajor == major) {
    if (osMinor > minor) return 1;
    if (osMinor == minor && osPatch >= patch) return 1;
  }
  return 0;
}

int pthread_mutexattr_setpolicy_np(pthread_mutexattr_t* attr, int policy) {
  /* The mutex policy is a 10.7 performance hint; ignore it on 10.6. */
  (void)attr;
  (void)policy;
  return 0;
}

/* commonCrypto random source used by Rust's std (10.7+). Returns 0 on
   success. */
int CCRandomGenerateBytes(void* bytes, size_t count) {
  uint8_t* p = (uint8_t*)bytes;
  while (count >= 4) {
    uint32_t r = arc4random();
    memcpy(p, &r, 4);
    p += 4;
    count -= 4;
  }
  if (count) {
    uint32_t r = arc4random();
    memcpy(p, &r, count);
  }
  return 0;
}

/* TLS destructor registration (10.7+); leak the destructor instead. */
int _tlv_atexit(void (*destructor)(void*), void* obj) {
  (void)destructor;
  (void)obj;
  return 0;
}

int getentropy(void* buffer, size_t length) {
  arc4random_buf(buffer, length);
  return 0;
}

/* The 10.6 headers define dirfd as a macro, so no libc symbol exists for
   code that declares it as an extern function (e.g. Rust's std). */
int (dirfd)(DIR* dirp) { return dirp->__dd_fd; }

/* clock_gettime family (10.12+). Clock id values from modern _time.h.
   MONOTONIC approximates with mach_absolute_time (excludes sleep); the
   cputime clocks use task/thread_info. */
static void pf_mach_to_timespec(uint64_t mach, struct timespec* ts) {
  static mach_timebase_info_data_t tb;
  if (tb.denom == 0) mach_timebase_info(&tb);
  uint64_t ns = (uint64_t)((__uint128_t)mach * tb.numer / tb.denom);
  ts->tv_sec = (time_t)(ns / 1000000000ull);
  ts->tv_nsec = (long)(ns % 1000000000ull);
}

static void pf_timeval_sum_to_timespec(time_value_t user, time_value_t system,
                                       struct timespec* ts) {
  int64_t usec = (int64_t)user.microseconds + system.microseconds;
  ts->tv_sec = user.seconds + system.seconds + usec / 1000000;
  ts->tv_nsec = (long)(usec % 1000000) * 1000;
}

int clock_gettime(clockid_t clk, struct timespec* ts) {
  switch (clk) {
    case 0: { /* CLOCK_REALTIME */
      struct timeval tv;
      gettimeofday(&tv, NULL);
      ts->tv_sec = tv.tv_sec;
      ts->tv_nsec = tv.tv_usec * 1000;
      return 0;
    }
    case 12: { /* CLOCK_PROCESS_CPUTIME_ID */
      struct task_basic_info info;
      mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
      if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info,
                    &count) != KERN_SUCCESS) {
        errno = EINVAL;
        return -1;
      }
      pf_timeval_sum_to_timespec(info.user_time, info.system_time, ts);
      return 0;
    }
    case 16: { /* CLOCK_THREAD_CPUTIME_ID */
      struct thread_basic_info info;
      mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
      if (thread_info(mach_thread_self(), THREAD_BASIC_INFO,
                      (thread_info_t)&info, &count) != KERN_SUCCESS) {
        errno = EINVAL;
        return -1;
      }
      pf_timeval_sum_to_timespec(info.user_time, info.system_time, ts);
      return 0;
    }
    case 4: /* CLOCK_MONOTONIC_RAW */
    case 5: /* CLOCK_MONOTONIC_RAW_APPROX */
    case 6: /* CLOCK_MONOTONIC */
    case 8: /* CLOCK_UPTIME_RAW */
    case 9: /* CLOCK_UPTIME_RAW_APPROX */
      pf_mach_to_timespec(mach_absolute_time(), ts);
      return 0;
    default:
      errno = ENOSYS;
      return -1;
  }
}

uint64_t clock_gettime_nsec_np(clockid_t clk) {
  struct timespec ts;
  if (clock_gettime(clk, &ts) != 0) return 0;
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* openat/unlinkat (10.10+), resolved through F_GETPATH since the 10.6
   kernel has no *at() syscalls. */
static int pf_resolve_at(int dirfd, const char* path, char* out,
                         size_t outsz) {
  if (path[0] == '/' || dirfd == -2 /* AT_FDCWD */) {
    snprintf(out, outsz, "%s", path);
    return 0;
  }
  char dir[MAXPATHLEN];
  if (fcntl(dirfd, F_GETPATH, dir) != 0) return -1;
  snprintf(out, outsz, "%s/%s", dir, path);
  return 0;
}

int openat(int dirfd, const char* path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = (mode_t)va_arg(ap, int);
    va_end(ap);
  }
  char full[MAXPATHLEN * 2];
  if (pf_resolve_at(dirfd, path, full, sizeof(full)) != 0) return -1;
  return open(full, flags, mode);
}

int unlinkat(int dirfd, const char* path, int flags) {
  char full[MAXPATHLEN * 2];
  if (pf_resolve_at(dirfd, path, full, sizeof(full)) != 0) return -1;
  if (flags & 0x0080 /* AT_REMOVEDIR */) return rmdir(full);
  return unlink(full);
}

/* fclonefileat (10.12+); Rust's std falls back to a read/write copy. */
int fclonefileat(int src, int dst_dirfd, const char* dst, uint32_t flags) {
  (void)src;
  (void)dst_dirfd;
  (void)dst;
  (void)flags;
  errno = ENOSYS;
  return -1;
}

/* fstatat/fdopendir (10.10+), $INODE64 variants (a no-op on x86_64 where
   stat is already 64-bit). fdopendir consumes the fd like the real one,
   so the caller-supplied fd is closed once the DIR owns its own. */
int fstatat$INODE64(int dirfd, const char* path, struct stat* st, int flags) {
  char full[MAXPATHLEN * 2];
  if (pf_resolve_at(dirfd, path, full, sizeof(full)) != 0) return -1;
  return (flags & 0x0020 /* AT_SYMLINK_NOFOLLOW */) ? lstat(full, st)
                                                    : stat(full, st);
}

DIR* fdopendir$INODE64(int fd) {
  char path[MAXPATHLEN];
  if (fcntl(fd, F_GETPATH, path) != 0) return NULL;
  DIR* dir = opendir(path);
  if (dir) close(fd);
  return dir;
}

/* ARC entry points (10.7+) over plain message sends, which is all the
   pre-ARC runtime offers. Weak references are emulated with a strong
   retain: 10.6 has no weak runtime, and the only user (Rust's objc crate)
   needs them for lifetime safety rather than zeroing. */
typedef void* pf_id;
extern pf_id objc_msgSend(pf_id, void*, ...);
extern void* sel_registerName(const char*);
extern pf_id objc_getClass(const char*);

pf_id objc_retain(pf_id obj) {
  return obj ? (pf_id)objc_msgSend(obj, sel_registerName("retain")) : obj;
}

void objc_release(pf_id obj) {
  if (obj) objc_msgSend(obj, sel_registerName("release"));
}

pf_id objc_autorelease(pf_id obj) {
  return obj ? (pf_id)objc_msgSend(obj, sel_registerName("autorelease"))
             : obj;
}

void* objc_autoreleasePoolPush(void) {
  pf_id cls = objc_getClass("NSAutoreleasePool");
  pf_id pool = (pf_id)objc_msgSend(cls, sel_registerName("alloc"));
  return (void*)objc_msgSend(pool, sel_registerName("init"));
}

void objc_autoreleasePoolPop(void* pool) {
  if (pool) objc_msgSend((pf_id)pool, sel_registerName("drain"));
}

pf_id objc_initWeak(pf_id* location, pf_id value) {
  *location = objc_retain(value);
  return value;
}

void objc_destroyWeak(pf_id* location) {
  objc_release(*location);
  *location = NULL;
}

pf_id objc_loadWeakRetained(pf_id* location) {
  return objc_retain(*location);
}

pf_id objc_alloc(pf_id cls) {
  return (pf_id)objc_msgSend(cls, sel_registerName("alloc"));
}

pf_id objc_retainAutoreleasedReturnValue(pf_id obj) {
  return objc_retain(obj);
}

/* Newer libm/libc entry points used by Rust's std and skia. */
size_t strnlen(const char* s, size_t maxlen) {
  const char* p = (const char*)memchr(s, 0, maxlen);
  return p ? (size_t)(p - s) : maxlen;
}

double __exp10(double x) { return pow(10.0, x); }

/* math.h leaves these incomplete in plain C. */
struct __float2 { float __sinval; float __cosval; };
struct __double2 { double __sinval; double __cosval; };

struct __double2 __sincos_stret(double x) {
  struct __double2 r;
  r.__sinval = sin(x);
  r.__cosval = cos(x);
  return r;
}

struct __float2 __sincosf_stret(float x) {
  struct __float2 r;
  r.__sinval = sinf(x);
  r.__cosval = cosf(x);
  return r;
}

/* CoreText pieces (10.7+) needed by skia and the font list, implemented
   over APIs the 10.6 CoreText (in ApplicationServices) does export. */
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>

/* CTFont.h only; CTFontManager.h is avoided because it declares a
   different (three-argument) CTFontManagerRegisterFontURLs than the
   callers here. */
#include <CoreText/CTFont.h>
extern int CTFontManagerRegisterFontsForURL(const void* url, int scope, void** error);

void CTFontDrawGlyphs(CTFontRef font, const CGGlyph glyphs[],
                      const CGPoint positions[], size_t count,
                      CGContextRef context) {
  CGFontRef cgFont = CTFontCopyGraphicsFont(font, NULL);
  if (!cgFont) return;
  CGContextSetFont(context, cgFont);
  CGContextSetFontSize(context, CTFontGetSize(font));
  CGContextShowGlyphsAtPositions(context, glyphs, positions, count);
  CGFontRelease(cgFont);
}

CTFontDescriptorRef CTFontManagerCreateFontDescriptorFromData(
    CFDataRef data) {
  CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
  if (!provider) return NULL;
  CGFontRef cgFont = CGFontCreateWithDataProvider(provider);
  CGDataProviderRelease(provider);
  if (!cgFont) return NULL;
  CTFontRef font = CTFontCreateWithGraphicsFont(cgFont, 0.0, NULL, NULL);
  CGFontRelease(cgFont);
  if (!font) return NULL;
  CTFontDescriptorRef desc = CTFontCopyFontDescriptor(font);
  CFRelease(font);
  return desc;
}

/* Signature matches the local declaration in CoreTextFontList.cpp. */
void CTFontManagerRegisterFontURLs(CFArrayRef fontURLs, int scope,
                                   Boolean autoRegistration, void* observer) {
  (void)autoRegistration;
  (void)observer;
  CFIndex count = fontURLs ? CFArrayGetCount(fontURLs) : 0;
  for (CFIndex i = 0; i < count; i++) {
    CFURLRef url = (CFURLRef)CFArrayGetValueAtIndex(fontURLs, i);
    CFErrorRef error = NULL;
    if (!CTFontManagerRegisterFontsForURL(url, scope, (void**)&error)) {
      if (error) CFRelease(error);
    }
  }
}

/* LaunchServices (10.10+), over the FSRef-based API the 10.6 SDK has. */
#include <CoreServices/CoreServices.h>

static const char* PF_CS =
    "/System/Library/Frameworks/CoreServices.framework/CoreServices";

CFURLRef LSCopyDefaultApplicationURLForURL(CFURLRef inURL,
                                           LSRolesMask inRoleMask,
                                           CFErrorRef* outError) {
  static CFURLRef (*real)(CFURLRef, LSRolesMask, CFErrorRef*);
  static OSStatus (*legacy)(CFURLRef, LSRolesMask, void*);
  static CFURLRef (*fromFSRef)(CFAllocatorRef, const void*);
  if (!real)
    real = (void*)pf_libframework(PF_CS, "LSCopyDefaultApplicationURLForURL");
  if (real) return real(inURL, inRoleMask, outError);
  if (!legacy) legacy = (void*)pf_libframework(PF_CS, "LSCopyApplicationForURL");
  if (!fromFSRef)
    fromFSRef = (void*)pf_libframework(PF_CS, "CFURLCreateFromFSRef");
  if (legacy && fromFSRef) {
    /* FSRef is an opaque 80-byte struct in CoreServices. */
    unsigned char appRef[80];
    if (legacy(inURL, inRoleMask, appRef) == noErr) {
      return fromFSRef(kCFAllocatorDefault, appRef);
    }
  }
  return NULL;
}

/* Post-10.6 entry points that Rust code links directly. The forwarding
   wrappers keep the real implementations on systems that have them. */
#include <sys/socket.h>
#include <CoreAudio/CoreAudioTypes.h>

ssize_t sendmsg_x(int s, const struct msghdr* mp, size_t buflen, u_int flags,
                  size_t* sentlen) {
  static ssize_t (*real)(int, const struct msghdr*, size_t, u_int, size_t*);
  if (!real) real = (void*)pf_libsystem("sendmsg_x");
  if (real) return real(s, mp, buflen, flags, sentlen);
  errno = ENOSYS;
  return -1;
}

ssize_t recvmsg_x(int s, struct msghdr* mp, size_t buflen, u_int flags,
                  size_t* recvlen) {
  static ssize_t (*real)(int, struct msghdr*, size_t, u_int, size_t*);
  if (!real) real = (void*)pf_libsystem("recvmsg_x");
  if (real) return real(s, mp, buflen, flags, recvlen);
  errno = ENOSYS;
  return -1;
}

#include <dispatch/data.h>
dispatch_data_t dispatch_data_create(const void* buffer, size_t size,
                                     dispatch_queue_t queue,
                                     dispatch_block_t destructor) {
  static dispatch_data_t (*real)(const void*, size_t, dispatch_queue_t,
                                 dispatch_block_t);
  if (!real)
    real = (void*)pf_libframework("/usr/lib/system/libdispatch.dylib",
                                  "dispatch_data_create");
  if (real) return real(buffer, size, queue, destructor);
  return NULL;
}

int sandbox_init_with_parameters(const char* profile, uint64_t flags,
                                 const char* const parameters[],
                                 char** errorbuf) {
  static int (*real)(const char*, uint64_t, const char* const[], char**);
  if (!real) real = (void*)pf_libsystem("sandbox_init_with_parameters");
  if (real) return real(profile, flags, parameters, errorbuf);
  /* 10.6 has only sandbox_init; the modern profiles may not compile there.
     Firefox never sandboxed on this OS - treat failure as "no sandbox"
     rather than killing every child process. */
  static int (*legacy)(const char*, uint64_t, char**);
  if (!legacy) legacy = (void*)pf_libsystem("sandbox_init");
  if (legacy && legacy(profile, flags, errorbuf) == 0) return 0;
  if (errorbuf) *errorbuf = NULL;
  return 0;
}

#include <Security/Security.h>
OSStatus SecTrustSetNetworkFetchAllowed(SecTrustRef trust, Boolean allow) {
  static OSStatus (*real)(SecTrustRef, Boolean);
  if (!real)
    real = (void*)pf_libframework(
        "/System/Library/Frameworks/Security.framework/Security",
        "SecTrustSetNetworkFetchAllowed");
  if (real) return real(trust, allow);
  return 0;
}

OSStatus AudioDeviceDuck(uint32_t inDevice, float inDuckLevel,
                         const void* inTimeStamp, uint32_t inRampDuration,
                         uint32_t inFlags) {
  static OSStatus (*real)(uint32_t, float, const void*, uint32_t, uint32_t);
  if (!real)
    real = (void*)pf_libsystem("AudioDeviceDuck");
  if (real) return real(inDevice, inDuckLevel, inTimeStamp, inRampDuration,
                        inFlags);
  return 0;
}

void* MTLCopyAllDevices(void) {
  static void* (*real)(void);
  if (!real)
    real = (void*)pf_libframework(
        "/System/Library/Frameworks/Metal.framework/Metal",
        "MTLCopyAllDevices");
  return real ? real() : NULL;
}

/* QoS classes are 10.10+; a no-op keeps the thread-pool callers happy. */
int pthread_set_qos_class_self_np(qos_class_t qos_class, int priority) {
  (void)qos_class;
  (void)priority;
  return 0;
}

int pthread_get_qos_class_np(pthread_t thread, qos_class_t* qos_class,
                                int* priority) {
  (void)thread;
  if (qos_class) *qos_class = 0; /* QOS_CLASS_UNSPECIFIED */
  if (priority) *priority = 0;
  return 0;
}

/* posix_spawn file-actions additions (10.10+/10.15+). Returning 0 leaves
   the attribute unset; callers that need chdir behavior use the cwd. */
#include <spawn.h>
int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t* actions,
                                         const char* path) {
  (void)actions;
  (void)path;
  return 0;
}

int posix_spawn_file_actions_addinherit_np(posix_spawn_file_actions_t* actions,
                                           int fd) {
  (void)actions;
  (void)fd;
  return 0;
}

/* CoreMIDI block-based port creation (10.5-era API existed; the block
   variant did not). Fail so the caller falls back or reports no MIDI. */
int MIDIInputPortCreateWithBlock(void* client, const void* name, void* port,
                                 void* block) {
  (void)client;
  (void)name;
  (void)port;
  (void)block;
  return -10844; /* kMIDIInvalidClient */
}

int pthread_chdir_np(const char* path) {
  static int (*real)(const char*);
  if (!real) real = (void*)pf_libsystem("pthread_chdir_np");
  if (real) return real(path);
  errno = ENOSYS;
  return -1;
}

int pthread_fchdir_np(int fd) {
  static int (*real)(int);
  if (!real) real = (void*)pf_libsystem("pthread_fchdir_np");
  if (real) return real(fd);
  errno = ENOSYS;
  return -1;
}

int responsibility_spawnattrs_setdisclaim(void* attrs, int disclaim) {
  static int (*real)(void*, int);
  if (!real)
    real = (void*)pf_libframework(
        "/usr/lib/system/libsystem_responsibility.dylib",
        "responsibility_spawnattrs_setdisclaim");
  if (real) return real(attrs, disclaim);
  return 0;
}
