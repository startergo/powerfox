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
