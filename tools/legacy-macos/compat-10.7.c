/* Symbols the 10.7 libSystem lacks, linked into every binary via
   LDFLAGS force-load (see mozconfig-macos107). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/sysctl.h>
#include <unistd.h>

/* Matches libSystem's availability_version: 8 bytes, minor/patch are
   16-bit. */
typedef struct {
  uint32_t major;
  uint16_t minor;
  uint16_t patch;
} pf_tav_t;

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

/* Called by clang's __isPlatformVersionAtLeast (used by @available) with a
   list of minimum versions; returns true when the running OS satisfies at
   least one entry. The libSystem implementation is a 10.12 addition; the
   symbol's C name carries a single leading underscore. */
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
