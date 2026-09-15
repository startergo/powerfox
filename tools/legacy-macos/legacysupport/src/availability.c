/* dyld's _availability_version_check is a 10.12 addition; binaries built
   with newer SDKs call it from ___isPlatformVersionAtLeast (the @available
   machinery). compiler-rt packs each entry as two uint32s:
   {platform, (major << 16) | (minor << 8) | subminor}; platform 1 is
   macOS, platform 0 matches any platform. */
#if defined(__APPLE__) && defined(__MACH__)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/sysctl.h>

static void mpls_os_version(int32_t* maj, int32_t* min, int32_t* patch) {
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

typedef struct {
  uint32_t platform;
  uint32_t packed;
} mpls_av_t;

uint8_t _availability_version_check(uint32_t count, const mpls_av_t* versions) {
  int32_t osMajor, osMinor, osPatch;
  mpls_os_version(&osMajor, &osMinor, &osPatch);
  for (uint32_t i = 0; i < count; i++) {
    const mpls_av_t* v = &versions[i];
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
#endif
