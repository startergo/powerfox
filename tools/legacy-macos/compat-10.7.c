/* Symbols the 10.7 libSystem lacks, linked into every binary via
   LDFLAGS force-load (see mozconfig-macos107). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/sysctl.h>
#include <unistd.h>

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
      osMinor = 7;
      osPatch = 0;
    }
  }
  *maj = osMajor;
  *min = osMinor;
  *patch = osPatch;
}

/* Called by compiler-rt's ___isPlatformVersionAtLeast (the @available
   machinery) with a list of minimum versions; returns true when the
   running OS satisfies at least one entry. The libSystem implementation
   is a 10.12 addition. compiler-rt packs each entry as two uint32s:
   {platform, (major << 16) | (minor << 8) | subminor}; platform 1 is
   macOS, platform 0 matches any platform. */
typedef struct {
  uint32_t platform;
  uint32_t packed;
} pf_av_t;

uint8_t _availability_version_check(uint32_t count, const pf_av_t* versions) {
  int32_t osMajor, osMinor, osPatch;
  pf_os_version(&osMajor, &osMinor, &osPatch);
  for (uint32_t i = 0; i < count; i++) {
    const pf_av_t* v = &versions[i];
    if (v->platform != 0 && v->platform != 1) continue;
    const uint32_t major = v->packed >> 16;
    const uint32_t minor = (v->packed >> 8) & 0xff;
    const uint32_t subminor = v->packed & 0xff;
    if ((uint32_t)osMajor > major) return 1;
    if ((uint32_t)osMajor == major) {
      if ((uint32_t)osMinor > minor) return 1;
      if ((uint32_t)osMinor == minor && (uint32_t)osPatch >= subminor) {
        return 1;
      }
    }
  }
  return 0;
}

/* CommonCrypto random source used by Rust's std; the public CommonCrypto
   API is a 10.8 addition. Returns 0 on success. */
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

/* getentropy is a 10.12 addition. */
int getentropy(void* buffer, size_t length) {
  arc4random_buf(buffer, length);
  return 0;
}

#include <dirent.h>
/* dirfd is a 10.8 libc addition. */
int (dirfd)(DIR* dirp) { return dirp->__dd_fd; }

#include <time.h>
#include <mach/mach_time.h>
/* clock_gettime_nsec_np is a 10.12 addition; derive it from
   mach_absolute_time (monotonic since boot, excluding suspend — which
   matches CLOCK_UPTIME_RAW and is close enough for CLOCK_MONOTONIC_RAW
   callers). */
uint64_t clock_gettime_nsec_np(clockid_t clk_id) {
  (void)clk_id;
  static mach_timebase_info_data_t tb = {0, 0};
  if (tb.denom == 0) mach_timebase_info(&tb);
  uint64_t ticks = mach_absolute_time();
  if (tb.numer == tb.denom) return ticks;
  return (uint64_t)((__uint128_t)ticks * tb.numer / tb.denom);
}

/* clock_gettime is a 10.12 addition. */
int clock_gettime(clockid_t clk_id, struct timespec* tp) {
  if (!tp) return -1;
  if (clk_id == CLOCK_REALTIME) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return -1;
    tp->tv_sec = tv.tv_sec;
    tp->tv_nsec = tv.tv_usec * 1000;
    return 0;
  }
  static mach_timebase_info_data_t tb = {0, 0};
  if (tb.denom == 0) mach_timebase_info(&tb);
  uint64_t ticks = mach_absolute_time();
  uint64_t ns = (tb.numer == tb.denom)
                    ? ticks
                    : (uint64_t)((__uint128_t)ticks * tb.numer / tb.denom);
  tp->tv_sec = (time_t)(ns / 1000000000ULL);
  tp->tv_nsec = (long)(ns % 1000000000ULL);
  return 0;
}

#include <sys/dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

/* The $INODE64 symbol variants (64-bit-inode struct stat/readdir) are
   10.10 libSystem additions. On 10.9 the default stat family already
   uses 64-bit inodes when built with a modern SDK (_DARWIN_FEATURE_64_
   BIT_INODE), so forward each variant to its plain counterpart. The
   SDK headers #define stat to stat$INODE64 etc., which would mangle
   these definitions; undef first. */
#undef stat
#undef fstat
#undef lstat
#undef fstatat
#undef opendir
#undef fdopendir
#undef readdir
#undef readdir_r

/* The SDK headers attach __asm("_fstat$INODE64")-style names to the
   plain declarations, so redeclaring them here would silently inherit
   the $INODE64 name and recurse. Route through distinct identifiers
   with explicit asm labels bound to the real symbols. */
int pf_stat(const char*, struct stat*) __asm("_stat");
int pf_fstat(int, struct stat*) __asm("_fstat");
int pf_lstat(const char*, struct stat*) __asm("_lstat");
int pf_fstatat(int, const char*, struct stat*, int) __asm("_fstatat");
DIR* pf_opendir(const char*) __asm("_opendir");
DIR* pf_fdopendir(int) __asm("_fdopendir");
struct dirent* pf_readdir(DIR*) __asm("_readdir");
int pf_readdir_r(DIR*, struct dirent*, struct dirent**) __asm("_readdir_r");

int stat$INODE64(const char* path, struct stat* st) {
  return pf_stat(path, st);
}
int fstat$INODE64(int fd, struct stat* st) { return pf_fstat(fd, st); }
int lstat$INODE64(const char* path, struct stat* st) {
  return pf_lstat(path, st);
}
int fstatat$INODE64(int fd, const char* path, struct stat* st, int flag) {
  return pf_fstatat(fd, path, st, flag);
}
DIR* opendir$INODE64(const char* name) { return pf_opendir(name); }
DIR* fdopendir$INODE64(int fd) { return pf_fdopendir(fd); }
struct dirent* readdir$INODE64(DIR* dir) { return pf_readdir(dir); }
int readdir_r$INODE64(DIR* dir, struct dirent* entry, struct dirent** result) {
  return pf_readdir_r(dir, entry, result);
}

#include <dispatch/dispatch.h>
/* dispatch_queue_create_with_target$V2 is a 10.12 variant; the plain
   symbol exists on 10.9. */
dispatch_queue_t dispatch_queue_create_with_target$V2(
    const char* label, dispatch_queue_attr_t attr, dispatch_queue_t target) {
  return dispatch_queue_create_with_target(label, attr, target);
}
