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
#include <mach/mach.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <objc/objc.h>
#include <objc/runtime.h>
#include <dlfcn.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <signal.h>
#define _XOPEN_SOURCE
#include <ucontext.h>
#include <sys/mman.h>

// Bring-up progress log: entry marks for every shim function the CDM binds,
// so the last line before a hang names the CDM's last platform call.
#define PFLOG(...) \
  do { fprintf(stderr, "PFSEQ " __VA_ARGS__); fflush(stderr); } while (0)
static unsigned pf_tlv_n = 0;

static char* g_guard_page;
static int g_guard_enabled;
static void pf_guard_handler(int sig, siginfo_t* si, void* ctx);

struct os_log_s { char unused; };
static struct os_log_s dummy_log = {0};
const struct os_log_s* _os_log_default = &dummy_log;

void* os_log_create(const char* subsystem, const char* category) {
  PFLOG("os_log_create(%s)\n", subsystem ? subsystem : "?"); return (void*)&dummy_log; }
void os_log_destroy(void* log) {}
void os_log_with_type(void* log, uint8_t t, const char* f, ...) {}
void os_log_info(void* l, const char* f, ...) {}
void os_log_debug(void* l, const char* f, ...) {}
void os_log_error(void* l, const char* f, ...) {}
void os_log_fault(void* l, const char* f, ...) {}
void PFLOG_os_log_variants_removed(void) {}
void _os_log_impl(void* log, void* buf, uint32_t sz) { static unsigned n = 0; if (n < 2) { PFLOG("_os_log_impl\n"); n++; } }
bool os_log_type_enabled(void* log, uint8_t type) { return false; }
void os_release(void* object) { static unsigned n = 0; if (n < 2) { PFLOG("os_release\n"); n++; } }

uint64_t clock_gettime_nsec_np(uint32_t clk) {
  PFLOG("nsec_np(%u)\n", clk);
  static mach_timebase_info_data_t tb;
  if (tb.denom == 0) mach_timebase_info(&tb);
  return (uint64_t)((__uint128_t)mach_absolute_time() * tb.numer / tb.denom);
}

extern void arc4random_buf(void*, size_t);
int getentropy(void* buf, size_t len) {
  static unsigned n = 0;
  if (n < 3) { PFLOG("getentropy(%zu)\n", len); n++; }
  // arc4random_buf itself is 10.7+; read from the entropy pool instead.
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) return -1;
  ssize_t got = read(fd, buf, len);
  close(fd);
  return got == (ssize_t)len ? 0 : -1;
}

size_t strnlen(const char* s, size_t maxlen) {
  size_t n = 0;
  while (n < maxlen && s[n]) n++;
  return n;
}

void explicit_bzero(void* s, size_t n) {
  memset(s, 0, n);
  __asm__ volatile("" ::: "memory");
}

int timingsafe_bcmp(const void* a, const void* b, size_t n) {
  const unsigned char* x = a;
  const unsigned char* y = b;
  unsigned char d = 0;
  for (size_t i = 0; i < n; i++) d |= x[i] ^ y[i];
  return d != 0;
}

uint32_t arc4random_uniform(uint32_t upper) {
  uint32_t r, min = -upper % upper;
  for (;;) {
    if (getentropy(&r, sizeof r) != 0) return 0;
    if (r >= min) return r % upper;
  }
}

void* aligned_alloc(size_t alignment, size_t size) {
  static unsigned n = 0;
  if (n < 3) { PFLOG("aligned_alloc(%zu)\n", size); n++; }
  void* p = 0;
  if (posix_memalign(&p, alignment, size) == 0) return p;
  return 0;
}

int clock_gettime(clockid_t clk, struct timespec* ts) {
  static unsigned n = 0;
  if (n < 3) { PFLOG("clock_gettime(%d)\n", (int)clk); n++; }
  struct timeval tv;
  gettimeofday(&tv, 0);
  ts->tv_sec = tv.tv_sec;
  ts->tv_nsec = tv.tv_usec * 1000;
  return 0;
}

int pthread_set_qos_class_self_np(qos_class_t cls, int prio) {
  PFLOG("qos(%d)\n", (int)cls);
  return 0;
}
void swap_linkedit_data_command(void) { PFLOG("swap_linkedit\n"); }

int os_signpost_enabled(void* log) { return 0; }

// 10.10-era Mach port APIs; report unsupported so callers fall back.
// Deferred TSD remap: the CDM rewrites its text during module init, so
// patches applied at load get clobbered or trip the page machinery. Defer
// until the first mach_port_construct (CreateCdmInstance, post-mutation).
static void* g_defer_base;
static size_t g_defer_size;
static int g_deferred_done;
int WidevineLegacyShimPatchCdmTsd(void* aBase, size_t aSize);

kern_return_t shim_mach_port_construct(mach_port_t task,
                                        const struct mach_port_options* options,
                                        mach_port_context_t context,
                                        mach_port_t* port)
    __asm("_mach_port_construct");
kern_return_t shim_mach_port_construct(mach_port_t task,
                                        const struct mach_port_options* options,
                                        mach_port_context_t context,
                                        mach_port_t* port) {
  PFLOG("mach_port_construct ENTER flags=%u ctx=%llu out=%p (main_pthread_lock=%p)\n",
        options ? (unsigned)options->flags : 0u,
        (unsigned long long)context, (void*)port,
        (void*)((char*)pthread_self() + 0x10));
  if (g_defer_base && !g_deferred_done && getenv("PF_PATCH")) {
    // Opt-in diagnostic; the wedge is covered by the testcancel neutering.
    g_deferred_done = 1;
    WidevineLegacyShimPatchCdmTsd(g_defer_base, g_defer_size);
    g_defer_base = 0;
  }
  if (g_guard_enabled && g_guard_page) {
    // The CDM re-installs signal handlers during init; re-arm before each
    // construct so the guard owns SIGSEGV/SIGTRAP in the corruption window.
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = pf_guard_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGBUS, &sa, 0);
    sigaction(SIGTRAP, &sa, 0);
    mprotect(g_guard_page, 4096, PROT_READ);
  }
  mach_port_t name;
  kern_return_t kr = mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &name);
  PFLOG("  step allocate kr=%d name=0x%x\n", (int)kr, (unsigned)name);
  if (kr != KERN_SUCCESS) {
    return kr;
  }
  if (options && (options->flags & MPO_INSERT_SEND_RIGHT)) {
    kr = mach_port_insert_right(task, name, name, MACH_MSG_TYPE_MAKE_SEND);
    PFLOG("  step insert_right kr=%d\n", (int)kr);
    if (kr != KERN_SUCCESS) {
      mach_port_destroy(task, name);
      return kr;
    }
  }
  if (context) {
    kr = mach_port_set_context(task, name, context);
    if (kr != KERN_SUCCESS) {
      mach_port_destroy(task, name);
      return kr;
    }
  }
  PFLOG("mach_port_construct EXIT name=0x%x\n", (unsigned)name);
  *port = name;
  return KERN_SUCCESS;
}
kern_return_t shim_mach_port_destruct(mach_port_t task, mach_port_t name,
                                      mach_port_delta_t srdelta,
                                      mach_port_context_t guard)
    __asm("_mach_port_destruct");
kern_return_t shim_mach_port_destruct(mach_port_t task, mach_port_t name,
                                      mach_port_delta_t srdelta,
                                      mach_port_context_t guard) {
  if (srdelta) {
    kern_return_t kr = mach_port_mod_refs(task, name,
                                          MACH_PORT_RIGHT_SEND, srdelta);
    if (kr && kr != KERN_INVALID_RIGHT) {
      return kr;
    }
  }
  return mach_port_destroy(task, name);
}
kern_return_t shim_mach_port_peek(mach_port_t task, mach_port_t name,
                                  void* options, uint64_t wait,
                                  void* ev, void* msg_size, void* msg_id,
                                  void* trailer)
    __asm("_mach_port_peek");
// (peek body below logs via PFLOG)
kern_return_t shim_mach_port_peek(mach_port_t task, mach_port_t name,
                                  void* options, uint64_t wait,
                                  void* ev, void* msg_size, void* msg_id,
                                  void* trailer) {
  PFLOG("mach_port_peek name=0x%x\n", (unsigned)name);
  return 7;
}
// Kernel-write APIs the harness's CDM bind patch renames onto these hooks:
// each passes a caller-supplied out-pointer to the kernel, which stores a
// fresh port name through it. A bad pointer writes into foreign memory with
// no user-space store to trace — logging the pointer names the culprit.
kern_return_t pf_mach_port_alloc(mach_port_t task, mach_port_right_t right,
                                 mach_port_t* name) {
  kern_return_t kr = mach_port_allocate(task, right, name);
  PFLOG("pf_mach_port_alloc right=%u out=%p name=0x%x kr=%d\n",
        (unsigned)right, (void*)name, name ? (unsigned)*name : 0, (int)kr);
  return kr;
}
kern_return_t pf_sema_create_x(task_t task, semaphore_t* sem, int policy,
                               int value) {
  kern_return_t kr = semaphore_create(task, sem, policy, value);
  PFLOG("pf_sema_create_x out=%p sem=0x%x kr=%d\n", (void*)sem,
        sem ? (unsigned)*sem : 0, (int)kr);
  return kr;
}
kern_return_t pf_thr_create(thread_act_t thread, thread_act_t* child) {
  kern_return_t kr = thread_create(thread, child);
  PFLOG("pf_thr_create out=%p child=0x%x kr=%d\n", (void*)child,
        child ? (unsigned)*child : 0, (int)kr);
  return kr;
}
struct pf_thr_ctx {
  void* (*start)(void*);
  void* arg;
};
static void* pf_thr_wrap(void* p) {
  struct pf_thr_ctx* c = p;
  PFLOG("WRAP enter th=%p fn=%p\n", (void*)pthread_self(), (void*)c->start);
  void* r = c->start(c->arg);
  PFLOG("WRAP exit th=%p fn=%p ret=%p\n", (void*)pthread_self(),
        (void*)c->start, r);
  free(c);
  return r;
}
int pf_pthread_cre(pthread_t* t, const pthread_attr_t* a, void* (*f)(void*),
                   void* arg) {
  size_t ss = 0;
  int ds = -1;
  if (a) {
    pthread_attr_getstacksize(a, &ss);
    pthread_attr_getdetachstate(a, &ds);
  }
  struct pf_thr_ctx* c = malloc(sizeof *c);
  c->start = f;
  c->arg = arg;
  int r = pthread_create(t, a, pf_thr_wrap, c);
  PFLOG("pf_pthread_cre r=%d ss=%zu ds=%d fn=%p t=0x%x\n", r, ss, ds,
        (void*)f, t ? (unsigned)*t : 0);
  if (r != 0) free(c);
  return r;
}
mach_port_t pf_pthd_mach_thread_np(pthread_t t) {
  mach_port_t p = pthread_mach_thread_np(t);
  PFLOG("pf_mach_thread_np t=%p port=0x%x\n", (void*)t, (unsigned)p);
  return p;
}

void* _os_signpost_emit_with_name_impl = 0;

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

// The ARC entry points (objc_release and friends) only exist from the
// 10.7 libobjc on; on older systems forward them to the matching
// retain/release/autorelease messages. On 10.7+ the real libobjc precedes
// this image in dyld's flat search order, so these are 10.6-only.

static id send0(id obj, const char* sel) {
  return ((id (*)(id, SEL))real_msgSend)(obj, sel_registerName(sel));
}

id objc_retain(id obj) { return obj ? send0(obj, "retain") : 0; }
void objc_release(id obj) {
  if (obj) send0(obj, "release");
}
id objc_autorelease(id obj) { return obj ? send0(obj, "autorelease") : 0; }
id objc_retainAutoreleasedReturnValue(id obj) { return objc_retain(obj); }
id objc_autoreleaseReturnValue(id obj) { return objc_autorelease(obj); }
id objc_retainBlock(id b) { return b ? send0(b, "copy") : 0; }
void objc_storeStrong(id* dest, id val) {
  objc_retain(val);
  id old = *dest;
  *dest = val;
  objc_release(old);
}
void objc_initWeak(id* addr, id val) {
  *addr = 0;
  objc_storeStrong(addr, val);
}
void objc_destroyWeak(id* addr) { objc_storeStrong(addr, 0); }
id objc_loadWeak(id* addr) { return *addr; }
id objc_loadWeakRetained(id* addr) { return objc_retain(*addr); }
void objc_copyWeak(id* dest, id* src) {
  id obj = objc_loadWeakRetained(src);
  objc_initWeak(dest, obj);
  objc_release(obj);
}
void objc_setProperty_nonatomic(id self, SEL sel, id value,
                                ptrdiff_t offset) {
  objc_storeStrong((id*)((char*)self + offset), value);
}

void* objc_autoreleasePoolPush(void) {
  Class pool = objc_getClass("NSAutoreleasePool");
  if (!pool) return 0;
  return send0(send0((id)pool, "alloc"), "init");
}

void objc_autoreleasePoolPop(void* token) {
  if (token) send0((id)token, "release");
}

// Apple's thread-local variable scheme (descriptor thunk resolving to
// __tlv_bootstrap, per-thread block built from the image's __thread_data
// template plus __thread_bss) shipped in 10.7. Recreate it on pthread keys
// for the CDM's __thread variables.

#include <string.h>
#include <mach-o/loader.h>
#include <mach-o/dyld.h>

struct tlv_image_block {
  const void* vars;
  void* block;
  struct tlv_image_block* next;
};

static pthread_key_t g_tlv_key;
static pthread_key_t g_tlv_terms_key;
static pthread_once_t g_tlv_once = PTHREAD_ONCE_INIT;
// Set when pthread keys are exhausted; TLS then resolves to one shared
// block instead of crashing on key 0.
static int g_tlv_shared = 0;
static void* g_tlv_shared_block;
static pthread_mutex_t g_tlv_shared_lock = PTHREAD_MUTEX_INITIALIZER;
// TLS layout of the CDM image, resolved once; a botched section walk can
// yield garbage sizes, so the result is validated and cached.
static struct {
  const void* vars;
  const void* tmpl;
  size_t ts, bs;
  int resolved;
  int valid;
} g_tlv_info;
static pthread_mutex_t g_tlv_info_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_tlv_zero_block[4096];

struct tlv_term {
  void (*func)(void*);
  void* obj;
};
struct tlv_term_list {
  size_t n, cap;
  struct tlv_term* v;
};

static void tlv_terms_free(void* p) {
  struct tlv_term_list* l = p;
  if (!l) return;
  for (size_t i = l->n; i > 0; i--) {
    l->v[i - 1].func(l->v[i - 1].obj);
  }
  free(l->v);
  free(l);
}

static void tlv_thread_free(void* head) {
  struct tlv_image_block* b = head;
  while (b) {
    struct tlv_image_block* n = b->next;
    free(b->block);
    free(b);
    b = n;
  }
}

static void tlv_key_init(void) {
  // Experiment: adopt the 10.7 semantics (full destructors) — the 10.7
  // build's DRM works; bisecting which of the three deltas matters.
  if (pthread_key_create(&g_tlv_terms_key, tlv_terms_free) != 0 ||
      pthread_key_create(&g_tlv_key, tlv_thread_free) != 0) {
    g_tlv_shared = 1;
  }
}

// Called by GMPLoader right after dlopening the shim, before any CDM code
// can run: creating the keys lazily from a CDM worker would let racing
// threads see g_tlv_key == 0, which is a valid (foreign) key id, and their
// per-thread lookups would miss forever.
void pf_log_rbp_site(unsigned aSite, uintptr_t aRbp) {
  static unsigned n;
  if (n++ < 500) {
    PFLOG("RBPSITE %x rbp=%llx\n", aSite, (unsigned long long)aRbp);
  }
}

// Log-and-forward interposes for the CDM's OS probes (renamed onto these
// by the loader-side patch). Never break the call — record name + result.
static int (*pf_real_sysctl)(int*, unsigned, void*, size_t*, void*, size_t);
static int (*pf_real_sysctlbyname)(const char*, void*, size_t*, void*, size_t);

int sc_log(int* name, unsigned namelen, void* oldp, size_t* oldlenp,
           void* newp, size_t newlen) {
  if (!pf_real_sysctl) return -1;
  int r = pf_real_sysctl(name, namelen, oldp, oldlenp, newp, newlen);
  char b[256];
  int n = snprintf(b, sizeof b, "SYSL sysctl%d r=%d old=%zu:", (int)namelen,
                   r, oldp && oldlenp ? *oldlenp : 0);
  for (unsigned i = 0; i < namelen && i < 6; i++) {
    n += snprintf(b + n, sizeof b - n, " %d", name[i]);
  }
  if (r == 0 && oldp && oldlenp && *oldlenp && *oldlenp < 64) {
    unsigned char* q = oldp;
    n += snprintf(b + n, sizeof b - n, " ->");
    for (size_t i = 0; i < *oldlenp; i++) {
      n += snprintf(b + n, sizeof b - n, " %02x", q[i]);
    }
  }
  snprintf(b + n, sizeof b - n, "\n");
  PFLOG("%s", b);
  return r;
}

int scbyname_log(const char* name, void* oldp, size_t* oldlenp, void* newp,
                 size_t newlen);
int sclog_byname(const char* name, void* oldp, size_t* oldlenp, void* newp,
                 size_t newlen) {
  return scbyname_log(name, oldp, oldlenp, newp, newlen);
}
int scbyname_log(const char* name, void* oldp, size_t* oldlenp, void* newp,
                 size_t newlen) {
  if (!pf_real_sysctlbyname) return -1;
  int r = pf_real_sysctlbyname(name, oldp, oldlenp, newp, newlen);
  char b[320];
  int n = snprintf(b, sizeof b, "SYSL byname=%s r=%d old=%zu", name ? name : "?",
                   r, oldp && oldlenp ? *oldlenp : 0);
  if (r == 0 && oldp && oldlenp && *oldlenp && *oldlenp < 64) {
    unsigned char* q = oldp;
    n += snprintf(b + n, sizeof b - n, " ->");
    for (size_t i = 0; i < *oldlenp; i++) {
      n += snprintf(b + n, sizeof b - n, " %02x", q[i]);
    }
  }
  snprintf(b + n, sizeof b - n, "\n");
  PFLOG("%s", b);
  return r;
}

// Patience instrument: sample the GMP main thread's (rip, rdi) once a
// minute; a converging workload shows rdi shrinking toward zero at a steady
// rate, an unbounded one cycles with no net progress.
static void* pf_patience(void* a) {
  int fd = open("/tmp/cdmprogress.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  mach_port_t target = (mach_port_t)(uintptr_t)a;
  void* base = 0;
  for (int i = 0; i < 3000; i++) {
    if (!base) {
      for (uint32_t k = 0; k < _dyld_image_count(); k++) {
        const char* nm = _dyld_get_image_name(k);
        if (nm && strstr(nm, "libwidevinecdm")) {
          base = _dyld_get_image_header(k);
          break;
        }
      }
    }
    if (base && i % 60 == 0) {
      if (thread_suspend(target) == KERN_SUCCESS) {
        x86_thread_state64_t st;
        mach_msg_type_number_t cnt = x86_THREAD_STATE64_COUNT;
        if (thread_get_state(target, x86_THREAD_STATE64, (thread_state_t)&st,
                             &cnt) == KERN_SUCCESS) {
          char b[256];
          int n = snprintf(b, sizeof b,
                           "t=%d rip_off=%llx rdi=%llx rbp=%llx ctr=%x", i,
                           (unsigned long long)((char*)st.__rip - (char*)base),
                           (unsigned long long)st.__rdi,
                           (unsigned long long)st.__rbp,
                           *(volatile unsigned*)((char*)base + 0x7614d0));
          // instruction bytes at rip + first fault-retry telltale
          unsigned char* rip8 = (unsigned char*)st.__rip;
          n += snprintf(b + n, sizeof b - n, " insn=");
          for (int k = 0; k < 9; k++) {
            n += snprintf(b + n, sizeof b - n, "%02x", rip8[k]);
          }
          // decode target of a %gs:[disp32] store if that's what it is
          if (rip8[0] == 0x65 && (rip8[1] & 0xf0) == 0x40 &&
              rip8[2] == 0x89 && rip8[3] == 0x04 && rip8[4] == 0x25) {
            int32_t disp;
            memcpy(&disp, rip8 + 5, 4);
            char* self = (char*)pthread_self();
            void* tgt = self + disp;
            vm_region_basic_info_data_64_t bi;
            mach_msg_type_number_t rc = VM_REGION_BASIC_INFO_COUNT_64;
            mach_port_t obj;
            vm_address_t reg = (vm_address_t)tgt & ~4095UL;
            vm_size_t rsz = 4096;
            const char* prot = "?";
            if (mach_vm_region(mach_task_self(), (mach_vm_address_t*)&reg,
                               &rsz, VM_REGION_BASIC_INFO_64,
                               (vm_region_info_t)&bi, &rc, &obj) ==
                KERN_SUCCESS) {
              prot = (bi.protection & 2) ? "rw" : "ro";
            }
            n += snprintf(b + n, sizeof b - n,
                          " GSDISP=%x tgt=%p(%s) val=%llx", disp, tgt, prot,
                          (unsigned long long)st.__rax);
          }
          n += snprintf(b + n, sizeof b - n, "\n");
          write(fd, b, n);
        }
        thread_resume(target);
      }
    }
    sleep(1);
  }
  close(fd);
  return 0;
}

// The CDM's obfuscated runtime spills state through %gs:0x10 — TSD slot 0
// of the modern pthread layout. On 10.6 that word is the per-thread lock,
// and the spill wedges every later lock taker in the commpage spin loop.
// The main thread's pthread struct sits on a dedicated page: keep that page
// read-only and drop the CDM's slot-0 stores in the fault handler, while
// single-stepping legitimate pthread writes through.
static uintptr_t g_guard_cdm_lo, g_guard_cdm_hi;
static volatile int g_guard_step;
static volatile int g_guard_skip_after_step;
static size_t g_guard_lock_off = 0x10;
static unsigned g_guard_drops;
static struct sigaction g_prev_segv, g_prev_bus, g_prev_trap;

static void pf_guard_forward(int sig, siginfo_t* si, void* ctx,
                             struct sigaction* prev) {
  if (prev->sa_flags & SA_SIGINFO) {
    prev->sa_sigaction(sig, si, ctx);
  } else if (prev->sa_handler && prev->sa_handler != SIG_DFL &&
             prev->sa_handler != SIG_IGN) {
    prev->sa_handler(sig);
  } else {
    signal(sig, SIG_DFL);
    raise(sig);
  }
}

static void pf_guard_handler(int sig, siginfo_t* si, void* ctx) {
  ucontext_t* uc = (ucontext_t*)ctx;
  x86_thread_state64_t* st = &uc->uc_mcontext->__ss;
  if (sig == SIGTRAP) {
    if (g_guard_step) {
      if (g_guard_skip_after_step) {
        g_guard_skip_after_step = 0;
        *(volatile uint32_t*)(g_guard_page + g_guard_lock_off) = 0;
      }
      mprotect(g_guard_page, 4096, PROT_READ);
      st->__rflags &= ~0x100ull;
      g_guard_step = 0;
      return;
    }
    pf_guard_forward(sig, si, ctx, &g_prev_trap);
    return;
  }
  uintptr_t fa = (uintptr_t)si->si_addr;
  if (g_guard_page && sig != SIGTRAP && fa >= (uintptr_t)g_guard_page &&
      fa < (uintptr_t)g_guard_page + 4096) {
    uintptr_t rip = st->__rip;
    uintptr_t lockword = (uintptr_t)g_guard_page + g_guard_lock_off;
    int in_comm = rip >= 0x7fffffe00260ull && rip <= 0x7fffffe00296ull;
    if (fa == lockword && !in_comm) {
      // The only legitimate writers of the lock word run from the commpage
      // spin lock; anything else is the CDM's %gs TSD spill — drop it.
      unsigned char code[9];
      vm_size_t got = 0;
      if (vm_read_overwrite(mach_task_self(), (vm_address_t)rip,
                            sizeof code, (vm_address_t)code, &got) ==
              KERN_SUCCESS &&
          got == sizeof code && code[0] == 0x65 && (code[1] & 0xf0) == 0x40 &&
          code[2] == 0x89 && code[3] == 0x04 && code[4] == 0x25 &&
          code[5] == 0x10 && code[6] == 0 && code[7] == 0 && code[8] == 0) {
        if (g_guard_drops++ < 3) {
          PFLOG("guard: dropped %%gs:0x10 store rip=%p val=0x%llx\n",
                (void*)rip, (unsigned long long)st->__rax);
        }
        st->__rip += 9;
        return;
      }
      // Unknown writer to the lock word: drop conservatively anyway; a
      // stale lock value wedges the thread forever, a lost spill does not.
      if (g_guard_drops++ < 3) {
        PFLOG("guard: dropped unknown store rip=%p\n", (void*)rip);
      }
      mprotect(g_guard_page, 4096, PROT_READ | PROT_WRITE);
      st->__rflags |= 0x100ull;
      g_guard_step = 1;
      g_guard_skip_after_step = 1;
      return;
    }
    mprotect(g_guard_page, 4096, PROT_READ | PROT_WRITE);
    st->__rflags |= 0x100ull;
    g_guard_step = 1;
    return;
  }
  pf_guard_forward(sig, si, ctx, sig == SIGBUS ? &g_prev_bus : &g_prev_segv);
}

void WidevineLegacyShimArmGuard(void) {
  // Called AFTER the CDM image is loaded: the CDM installs its own signal
  // handlers for integrity checking, so installing earlier gets overridden.
  if (g_guard_page) {
    g_guard_enabled = 1;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = pf_guard_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, &g_prev_segv);
    sigaction(SIGBUS, &sa, &g_prev_bus);
    sigaction(SIGTRAP, &sa, &g_prev_trap);
    mprotect(g_guard_page, 4096, PROT_READ);
    PFLOG("guard: armed, page %p read-only\n", (void*)g_guard_page);
  }
}

// The CDM addresses modern-layout TSD slots directly: %gs:(0x10 + 8*n).
// On 10.6 those offsets hit unrelated pthread fields — slot 0 is the
// per-thread lock, and a spill there wedges the thread in the commpage
// spin loop. Remap every %gs:[disp32] access in the decrypted CDM text to
// a real 10.6 TSD slot owned by this shim, so the CDM's per-thread state
// works and the lock word is never touched.
static pthread_key_t pf_reserved_keys[8];
static size_t pf_cdm_slot_target(uint32_t aModernOff, uint32_t* aOut) {
  static pthread_key_t keys[8];
  static size_t offs[8];
  static int init;
  if (!init) {
    init = 1;
    pthread_t self = pthread_self();
    unsigned char* ps = (unsigned char*)self;
    for (int i = 0; i < 8; i++) {
      if (pthread_key_create(&keys[i], 0) != 0) break;
      pthread_setspecific(keys[i], (void*)(uintptr_t)(0x51CA0000ul | i));
      // 10.6 stores TSD inline in the pthread struct (measured: +0x868+8n).
      for (size_t o = 0x10; o < 0x2000; o += 8) {
        uintptr_t v;
        vm_size_t got = 0;
        if (vm_read_overwrite(mach_task_self(), (vm_address_t)(ps + o), 8,
                              (vm_address_t)&v, &got) != KERN_SUCCESS) {
          break;
        }
        if (v == (uintptr_t)(0x51CA0000ul | (unsigned)i)) {
          offs[i] = o;
          break;
        }
      }
      pthread_setspecific(keys[i], 0);
    }
    for (int i = 0; i < 8; i++) pf_reserved_keys[i] = keys[i];
  }
  if (aModernOff < 0x10 || aModernOff > 0x48 || (aModernOff & 7)) return 0;
  size_t idx = (aModernOff - 0x10) / 8;
  if (!offs[idx]) return 0;
  *aOut = (uint32_t)offs[idx];
  return 1;
}

int WidevineLegacyShimPatchCdmTsd(void* aBase, size_t aSize) {
  if (!aBase || !aSize) return -1;
  if (!g_deferred_done) {
    g_defer_base = aBase;
    g_defer_size = aSize;
    PFLOG("tsd-remap: deferred to first construct\n");
    return 0;
  }
  unsigned char* p = (unsigned char*)aBase;
  uint32_t mapped0;
  if (!pf_cdm_slot_target(0x10, &mapped0)) {
    PFLOG("tsd-remap: no slot target; not patching\n");
    return -1;
  }
  int patched = 0;
  for (size_t i = 0; i + 9 <= aSize; i++) {
    if (p[i] != 0x65 || (p[i + 1] & 0xf0) != 0x40) continue;
    if (p[i + 2] != 0x89 && p[i + 2] != 0x8b) continue;
    if (p[i + 3] != 0x04 || p[i + 4] != 0x25) continue;
    uint32_t disp;
    memcpy(&disp, p + i + 5, 4);
    uint32_t mapped;
    if (!pf_cdm_slot_target(disp, &mapped)) continue;
    uintptr_t pg = (uintptr_t)(p + i) & ~4095UL;
    mprotect((void*)pg, 8192, PROT_READ | PROT_WRITE | PROT_EXEC);
    memcpy(p + i + 5, &mapped, 4);
    mprotect((void*)pg, 8192, PROT_READ | PROT_EXEC);
    patched++;
    if (patched <= 5) {
      PFLOG("tsd-remap: %%gs:0x%x -> 0x%x at +%zx\n", disp, mapped, i);
    }
  }

  // Indexed array form: mov reg, %gs:[index*8 + 0x10] — remap the base so
  // index 0 lands on the first reserved slot (the keys are consecutive).
  int indexed = 0;
  for (size_t i = 0; i + 10 <= aSize; i++) {
    if (p[i] != 0x65 || (p[i + 1] & 0xf0) != 0x40) continue;
    if (p[i + 2] != 0x8b && p[i + 2] != 0x89) continue;
    if (p[i + 3] != 0x04 || (p[i + 4] & 0x07) != 0x05) continue;
    uint32_t disp;
    memcpy(&disp, p + i + 5, 4);
    if (disp != 0 && disp != 0x10) continue;
    uintptr_t pg = (uintptr_t)(p + i) & ~4095UL;
    mprotect((void*)pg, 8192, PROT_READ | PROT_WRITE | PROT_EXEC);
    memcpy(p + i + 5, &mapped0, 4);
    mprotect((void*)pg, 8192, PROT_READ | PROT_EXEC);
    indexed++;
  }

  // Two instrumented modes for the short (%gs:[rbp+0x10]) sites. The CDM's
  // VM keeps rbp as a dynamic gs-space base: rbp==0 means TSD slot 0 (remap
  // is correct); other values are the VM's own per-thread frame (remap must
  // not touch them). PF_RBP_LOG=1 installs pure observers (log site+rbp,
  // execute the original instruction); PF_RBP_N=k installs remaps for the
  // first k candidates.
  const char* rbpn = getenv("PF_RBP_N");
  const char* rbpl = getenv("PF_RBP_LOG");
  int want = rbpn ? atoi(rbpn) : 0;
  int logmode = rbpl != 0;
  if (want > 0 || logmode) {
    unsigned char* tpage = 0;
    size_t tused = 0;
    int done = 0;
    for (size_t i = 0; i + 9 <= aSize; i++) {
      if (p[i] != 0x65 || p[i + 1] != 0x48) continue;
      if (p[i + 2] != 0x89 && p[i + 2] != 0x8b) continue;
      if (p[i + 3] != 0x0c && p[i + 3] != 0x14) continue;
      if (p[i + 4] != 0x25 || p[i + 5] != 0x10) continue;
      if (!logmode && done >= want) break;
      if (!tpage) {
        for (int tryn = 0; tryn < 300 && !tpage; tryn++) {
          mach_vm_address_t at = 0;
          if (mach_vm_allocate(mach_task_self(), &at, 65536,
                               VM_FLAGS_ANYWHERE) == KERN_SUCCESS) {
            mach_vm_size_t dist = at > (mach_vm_address_t)aBase
                                      ? at - (mach_vm_address_t)aBase
                                      : (mach_vm_address_t)aBase - at;
            if (dist < 0x40000000ull) {
              tpage = (unsigned char*)at;
            } else {
              mach_vm_deallocate(mach_task_self(), at, 65536);
            }
          }
        }
        if (!tpage) {
          PFLOG("tsd-remap: trampoline alloc failed\n");
          break;
        }
        mprotect(tpage, 65536, PROT_READ | PROT_WRITE | PROT_EXEC);
      }
      if (tused + 96 > 65536) break;
      unsigned char* tr = tpage + tused;
      unsigned regfield = (p[i + 3] >> 3) & 7;
      size_t o = 0;
      if (logmode) {
        static const unsigned char pushes[] = {
            0x55, 0x50, 0x51, 0x52, 0x56, 0x57,
            0x41, 0x50, 0x41, 0x51, 0x41, 0x52};
        memcpy(tr + o, pushes, sizeof pushes);
        o += sizeof pushes;
        tr[o++] = 0x48; tr[o++] = 0xc7; tr[o++] = 0xc7;  // mov rdi, imm32
        uint32_t sidx = (uint32_t)(i & 0xffffff);
        memcpy(tr + o, &sidx, 4);
        o += 4;
        tr[o++] = 0x48; tr[o++] = 0x89; tr[o++] = 0xee;  // mov rsi, rbp
        tr[o++] = 0x48; tr[o++] = 0xb8;                  // mov rax, imm64
        uint64_t fn = (uint64_t)&pf_log_rbp_site;
        memcpy(tr + o, &fn, 8);
        o += 8;
        tr[o++] = 0xff; tr[o++] = 0xd0;                  // call rax
        static const unsigned char pops[] = {
            0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, 0x5f, 0x5e, 0x5a, 0x59,
            0x58, 0x5d};
        memcpy(tr + o, pops, sizeof pops);
        o += sizeof pops;
        memcpy(tr + o, p + i, 9);                        // original insn
        o += 9;
        tr[o++] = 0xe9;
        int32_t back = (int32_t)((uintptr_t)(p + i + 9) - (uintptr_t)(tr + o + 4));
        memcpy(tr + o, &back, 4);
        o += 4;
      } else {
        tr[o++] = 0x50;                                  // push rax
        tr[o++] = 0x48; tr[o++] = 0x8d; tr[o++] = 0x85;  // lea rax,[rbp+m0]
        memcpy(tr + o, &mapped0, 4);
        o += 4;
        tr[o++] = 0x65; tr[o++] = 0x48; tr[o++] = p[i + 2];
        tr[o++] = (unsigned char)((regfield << 3) | 0x00);
        tr[o++] = 0x58;                                  // pop rax
        tr[o++] = 0xe9;
        int32_t back = (int32_t)((uintptr_t)(p + i + 9) - (uintptr_t)(tr + o + 4));
        memcpy(tr + o, &back, 4);
        o += 4;
      }
      tused += (o + 15) & ~(size_t)15;
      uintptr_t pg = (uintptr_t)(p + i) & ~4095UL;
      mprotect((void*)pg, 8192, PROT_READ | PROT_WRITE | PROT_EXEC);
      p[i + 0] = 0xe9;
      int32_t fwd = (int32_t)((uintptr_t)tr - (uintptr_t)(p + i + 5));
      memcpy(p + i + 1, &fwd, 4);
      memset(p + i + 5, 0x90, 4);
      mprotect((void*)pg, 8192, PROT_READ | PROT_EXEC);
      done++;
    }
    PFLOG("tsd-remap: %d rbp %s installed\n", done,
          logmode ? "observers" : "trampolines");
  }
  // Modern dyld stamps each descriptor's key at load; nothing does on 10.6.
  // Point them at reserved slot 1, where __tlv_bootstrap publishes the block.
  {
    unsigned char* tv = (unsigned char*)aBase + 0x154fc20;
    uintptr_t one = 1;
    for (int d = 0; d < 11; d++) {
      memcpy(tv + d * 24 + 8, &one, 8);
    }
    PFLOG("tsd-remap: 11 descriptor keys stamped\n");
  }
  PFLOG("tsd-remap: %d abs, %d indexed sites patched\n", patched, indexed);
  return patched + indexed;
}

// 10.6's __pthread_testcancel acquires the per-thread lock at pthread+0x10
// on every syscall-boundary cancellation check — the exact word where the
// CDM's %gs:0x10 spill parks its port names. Gecko never uses pthread
// cancellation, so neuter the check entirely: the lock is never taken and
// the spill never collides.
static void pf_neuter_testcancel(void) {
  void* fn = 0;
  // Private symbol: find the real libSystem image by path (the shim's
  // reexports shadow every dlsym), then apply the measured offset from
  // __pthread_testcancel's disassembly.
  for (uint32_t i = 0; i < _dyld_image_count(); i++) {
    const char* nm = _dyld_get_image_name(i);
    if (nm && strstr(nm, "/usr/lib/libSystem.B.dylib")) {
      fn = (char*)_dyld_get_image_header(i) + 0x3b9d9;
      break;
    }
  }
  if (!fn) {
    return;
  }
  uintptr_t pg = (uintptr_t)fn & ~4095UL;
  if (mprotect((void*)pg, 4096, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
    *(unsigned char*)fn = 0xc3;  // ret
    mprotect((void*)pg, 4096, PROT_READ | PROT_EXEC);
    PFLOG("testcancel neutered at %p\n", fn);
  }
}

void WidevineLegacyShimInit(void) {
  pthread_once(&g_tlv_once, tlv_key_init);
  pf_neuter_testcancel();
  if (getenv("PF_PATIENCE")) {
    pthread_t pt;
    pthread_create(&pt, 0, pf_patience,
                   (void*)(uintptr_t)mach_thread_self());
  }
  char* self = (char*)pthread_self();
  if (*(uint32_t*)self != 0x54485244u) {
    // Unexpected pthread layout; guard nothing rather than trap wrong.
    return;
  }
  g_guard_page = (char*)((uintptr_t)self & ~4095UL);
}

static void tlv_locate(const struct tlv_descriptor* aDesc, const void** aTmpl,
                       size_t* aTmplSize, size_t* aBssSize,
                       const void** aVars) {
  Dl_info di;
  if (!dladdr((void*)aDesc, &di) || !di.dli_fbase) {
    return;
  }
  const struct mach_header_64* hdr = (const struct mach_header_64*)di.dli_fbase;
  const struct load_command* lc = (const struct load_command*)(hdr + 1);
  // Position-independent images carry a zero __TEXT vmaddr, so a found
  // flag replaces a nonzero check for the slide base.
  uintptr_t textvm = 0;
  int textFound = 0;
  for (uint32_t i = 0; i < hdr->ncmds;
       i++, lc = (const struct load_command*)((const char*)lc + lc->cmdsize)) {
    if (lc->cmd != LC_SEGMENT_64) {
      continue;
    }
    const struct segment_command_64* sg = (const struct segment_command_64*)lc;
    if (!strcmp(sg->segname, SEG_TEXT)) {
      textvm = sg->vmaddr;
      textFound = 1;
      break;
    }
  }
  if (!textFound) {
    return;
  }
  intptr_t slide = (uintptr_t)hdr - (intptr_t)textvm;
  lc = (const struct load_command*)(hdr + 1);
  for (uint32_t i = 0; i < hdr->ncmds;
       i++, lc = (const struct load_command*)((const char*)lc + lc->cmdsize)) {
    if (lc->cmd != LC_SEGMENT_64) {
      continue;
    }
    const struct segment_command_64* sg = (const struct segment_command_64*)lc;
    const struct section_64* sec =
        (const struct section_64*)((const char*)sg + sizeof(*sg));
    for (uint32_t s = 0; s < sg->nsects; s++, sec++) {
      uintptr_t addr = (uintptr_t)(sec->addr + slide);
      size_t size = sec->size;
      if (!strcmp(sec->sectname, "__thread_vars") && !strcmp(sec->segname, "__DATA")) {
        *aVars = (const void*)addr;
      } else if (!strcmp(sec->sectname, "__thread_data")) {
        *aTmpl = (const void*)addr;
        *aTmplSize = size;
      } else if (!strcmp(sec->sectname, "__thread_bss")) {
        *aBssSize = size;
      }
    }
  }
}

static uintptr_t tlv_image_extent(const void* aAddr) {
  Dl_info di;
  if (!dladdr((void*)aAddr, &di) || !di.dli_fbase) {
    return 0;
  }
  const struct mach_header_64* hdr = (const struct mach_header_64*)di.dli_fbase;
  intptr_t slide = 0;
  uintptr_t end = 0;
  const struct load_command* lc = (const struct load_command*)(hdr + 1);
  for (uint32_t i = 0; i < hdr->ncmds;
       i++, lc = (const struct load_command*)((const char*)lc + lc->cmdsize)) {
    if (lc->cmd != LC_SEGMENT_64) {
      continue;
    }
    const struct segment_command_64* sg = (const struct segment_command_64*)lc;
    if (!strcmp(sg->segname, SEG_TEXT)) {
      slide = (uintptr_t)hdr - (intptr_t)sg->vmaddr;
    }
  }
  lc = (const struct load_command*)(hdr + 1);
  for (uint32_t i = 0; i < hdr->ncmds;
       i++, lc = (const struct load_command*)((const char*)lc + lc->cmdsize)) {
    if (lc->cmd != LC_SEGMENT_64) {
      continue;
    }
    const struct segment_command_64* sg = (const struct segment_command_64*)lc;
    uintptr_t segEnd = (uintptr_t)(sg->vmaddr + sg->vmsize + slide);
    if (segEnd > end) {
      end = segEnd;
    }
  }
  return end;
}

static void tlv_resolve(const struct tlv_descriptor* d) {
  if (!g_guard_cdm_lo) {
    Dl_info dig;
    if (dladdr((void*)d, &dig) && dig.dli_fbase) {
      uintptr_t ext = tlv_image_extent(d);
      if (ext) {
        g_guard_cdm_lo = (uintptr_t)dig.dli_fbase;
        g_guard_cdm_hi = ext;
      }
    }
  }
  pthread_mutex_lock(&g_tlv_info_lock);
  if (!g_tlv_info.resolved) {
    Dl_info di3;
    const char* di_f =
        dladdr((void*)d, &di3) && di3.dli_fname ? di3.dli_fname : NULL;
    tlv_locate(d, &g_tlv_info.tmpl, &g_tlv_info.ts, &g_tlv_info.bs,
               &g_tlv_info.vars);
    size_t total = g_tlv_info.ts + g_tlv_info.bs;
    uintptr_t base = (uintptr_t)d;
    g_tlv_info.valid = 0;
    if (g_tlv_info.vars && total > 0 && total <= 4096 &&
        (g_tlv_info.ts == 0 || g_tlv_info.tmpl)) {
      uintptr_t lo = (uintptr_t)0, hi = tlv_image_extent(d);
      Dl_info di2;
      if (hi && dladdr((void*)d, &di2) && di2.dli_fbase) {
        lo = (uintptr_t)di2.dli_fbase;
        int vars_ok = (uintptr_t)g_tlv_info.vars >= lo &&
                      (uintptr_t)g_tlv_info.vars < hi;
        int tmpl_ok = g_tlv_info.ts == 0 ||
                      ((uintptr_t)g_tlv_info.tmpl >= lo &&
                       (uintptr_t)g_tlv_info.tmpl < hi);
        g_tlv_info.valid = vars_ok && tmpl_ok;
      }
    }
    g_tlv_info.resolved = 1;
    if (!g_tlv_info.valid) {
      fprintf(stderr,
              "tlv: invalid layout desc=%p image=%s vars=%p tmpl=%p ts=%zu "
              "bs=%zu\n",
              (void*)d, di_f ? di_f : "?", g_tlv_info.vars, g_tlv_info.tmpl,
              g_tlv_info.ts, g_tlv_info.bs);
    }
  }
  pthread_mutex_unlock(&g_tlv_info_lock);
}

void* __tlv_bootstrap(struct tlv_descriptor* d) {
  if (pf_tlv_n < 60) {
    PFLOG("tlv_bootstrap[%u] off=0x%lx th=%p\n", pf_tlv_n++,
          (unsigned long)d->offset, (void*)pthread_self());
  }
  pthread_once(&g_tlv_once, tlv_key_init);
  tlv_resolve(d);
  if (!g_tlv_info.valid || d->offset >= sizeof(g_tlv_zero_block)) {
    // Degraded path (validated to never trigger on a loadable CDM):
    // keep distinct variables at distinct addresses so they don't alias.
    return g_tlv_zero_block +
           (d->offset < sizeof(g_tlv_zero_block) ? d->offset : 0);
  }
  if (g_tlv_shared) {
    pthread_mutex_lock(&g_tlv_shared_lock);
    if (!g_tlv_shared_block) {
      g_tlv_shared_block = calloc(1, g_tlv_info.ts + g_tlv_info.bs);
      if (g_tlv_info.tmpl && g_tlv_info.ts) {
        memcpy(g_tlv_shared_block, g_tlv_info.tmpl, g_tlv_info.ts);
      }
    }
    void* block = g_tlv_shared_block;
    pthread_mutex_unlock(&g_tlv_shared_lock);
    return (char*)block + d->offset;
  }
  struct tlv_image_block* head = pthread_getspecific(g_tlv_key);

  const void* tmpl = g_tlv_info.tmpl;
  const void* vars = g_tlv_info.vars;
  size_t tmplSize = g_tlv_info.ts, bssSize = g_tlv_info.bs;
  for (struct tlv_image_block* b = head; b; b = b->next) {
    if (b->vars == vars) {
      return (char*)b->block + d->offset;
    }
  }
  size_t total = tmplSize + bssSize;
  pthread_mutex_lock(&g_tlv_shared_lock);
  void* block = calloc(1, total ? total : 1);
  struct tlv_image_block* b = malloc(sizeof(*b));
  if (!block || !b) {
    free(block);
    free(b);
    pthread_mutex_unlock(&g_tlv_shared_lock);
    return (char*)g_tlv_zero_block + (d->offset < sizeof(g_tlv_zero_block)
                                           ? d->offset
                                           : 0);
  }
  if (tmpl && tmplSize) {
    memcpy(block, tmpl, tmplSize);
  }
  b->vars = vars;
  b->block = block;
  b->next = head;
  if (pthread_setspecific(g_tlv_key, b) != 0) {
    // Per-thread storage unavailable; latch shared mode so later accesses
    // take the shared path instead of re-allocating and discarding.
    if (!g_tlv_shared_block) {
      g_tlv_shared_block = block;
    } else {
      free(block);
    }
    free(b);
    block = g_tlv_shared_block;
    g_tlv_shared = 1;
  }
  pthread_mutex_unlock(&g_tlv_shared_lock);
  if (pf_reserved_keys[1]) {
    pthread_setspecific(pf_reserved_keys[1], block);
  }
  return (char*)block + d->offset;
}

void __tlv_atexit(void (*func)(void*), void* obj) {
  pthread_once(&g_tlv_once, tlv_key_init);
  if (g_tlv_shared) return;
  struct tlv_term_list* l = pthread_getspecific(g_tlv_terms_key);
  if (!l) {
    l = calloc(1, sizeof(*l));
    if (!l) return;
    pthread_setspecific(g_tlv_terms_key, l);
  }
  if (l->n == l->cap) {
    size_t cap = l->cap ? l->cap * 2 : 4;
    struct tlv_term* v = realloc(l->v, cap * sizeof(*v));
    if (!v) return;
    l->v = v;
    l->cap = cap;
  }
  l->v[l->n].func = func;
  l->v[l->n].obj = obj;
  l->n++;
}


// The __atomic_* libcall family ships in libSystem only from ~10.12 (and in
// libclang_rt on newer systems); forward every variant to the compiler's
// inline builtins. Sequentially consistent everywhere, which is a safe
// superset of the ordering arguments the callers pass.

#define ATOMIC_N(N, T)                                                        \
  T __atomic_load_##N(const T* p) { return __atomic_load_n(p, 5); }          \
  void __atomic_store_##N(T* p, T v) { __atomic_store_n(p, v, 5); }          \
  T __atomic_exchange_##N(T* p, T v) { return __atomic_exchange_n(p, v, 5); } \
  T __atomic_fetch_add_##N(T* p, T v) { return __sync_fetch_and_add(p, v); } \
  T __atomic_fetch_sub_##N(T* p, T v) { return __sync_fetch_and_sub(p, v); } \
  T __atomic_fetch_and_##N(T* p, T v) { return __sync_fetch_and_and(p, v); } \
  T __atomic_fetch_or_##N(T* p, T v) { return __sync_fetch_and_or(p, v); } \
  T __atomic_fetch_xor_##N(T* p, T v) { return __sync_fetch_and_xor(p, v); } \
  T __atomic_fetch_nand_##N(T* p, T v) {                                      \
    T o = *p;                                                                 \
    do {                                                                      \
      T d = ~(o & v);                                                         \
      if (__atomic_compare_exchange_n(p, &o, d, 0, 5, 5)) return o;           \
    } while (1);                                                              \
  } \
  bool __atomic_compare_exchange_##N(T* p, T* e, T d) {                      \
    return __atomic_compare_exchange_n(p, e, d, 0, 5, 5);                       \
  }

ATOMIC_N(1, uint8_t)
ATOMIC_N(2, uint16_t)
ATOMIC_N(4, uint32_t)
ATOMIC_N(8, uint64_t)


// PTHREAD-SURFACE COUNTERS (in-process, no live attach): wrap the lock/
// wait entry points the CDM binds under flat namespace. First-call and
// every-5000th logged; counts readable from stderr tail.
#include <dlfcn.h>
static void* pf_real_mutex_lock;
static void* pf_real_mutex_unlock;
static void* pf_real_cond_wait;
static void* pf_real_sem_create;
static void* pf_real_setspec;
static void* pf_real_key_create;
static unsigned pf_ml_n, pf_mu_n, pf_cw_n;
__attribute__((constructor)) static void pf_pthread_hook_init(void) {
  pf_real_mutex_lock = dlsym(RTLD_NEXT, "pthread_mutex_lock");
  pf_real_mutex_unlock = dlsym(RTLD_NEXT, "pthread_mutex_unlock");
  pf_real_cond_wait = dlsym(RTLD_NEXT, "pthread_cond_wait");
  // Resolve before the flat-namespace flip: RTLD_NEXT under flipped flat
  // lookup can return NULL, and a call forwarded through it jumps to zero.
  pf_real_sem_create = dlsym(RTLD_NEXT, "semaphore_create");
  pf_real_setspec = dlsym(RTLD_NEXT, "pthread_setspecific");
  pf_real_key_create = dlsym(RTLD_NEXT, "pthread_key_create");
  pf_real_sysctl = (int (*)(int*, unsigned, void*, size_t*, void*, size_t))dlsym(
      RTLD_NEXT, "sysctl");
  pf_real_sysctlbyname =
      (int (*)(const char*, void*, size_t*, void*, size_t))dlsym(
          RTLD_NEXT, "sysctlbyname");
}
int pthread_mutex_lock(pthread_mutex_t* m) {
  if ((++pf_ml_n <= 5) || (pf_ml_n % 5000 == 0)) {
    fprintf(stderr, "PFPTH ml n=%u m=%p\n", pf_ml_n, (void*)m);
  }
  return ((int (*)(pthread_mutex_t*))pf_real_mutex_lock)(m);
}
int pthread_mutex_unlock(pthread_mutex_t* m) {
  ++pf_mu_n;
  return ((int (*)(pthread_mutex_t*))pf_real_mutex_unlock)(m);
}
static void* pf_real_key_create;
int pthread_key_create(pthread_key_t* key,
                       void (*destructor)(void*)) {
  if (!pf_real_key_create) {
    return -1;
  }
  if (destructor) {
    fprintf(stderr, "PFKEY dtor=%p — forcing NULL\n", (void*)destructor);
    destructor = NULL;
  }
  return ((int (*)(pthread_key_t*, void (*)(void*)))pf_real_key_create)(
      key, destructor);
}
static unsigned pf_sc_n;
kern_return_t semaphore_create(task_t task, semaphore_t* sem, int policy,
                               int value) {
  if (!pf_real_sem_create) {
    return KERN_FAILURE;
  }
  kern_return_t kr =
      ((kern_return_t(*)(task_t, semaphore_t*, int, int))pf_real_sem_create)(
          task, sem, policy, value);
  if (pf_sc_n++ < 20) {
    PFLOG("INT semaphore_create out=%p sem=0x%x kr=%d (pth+0x10=%p)\n",
          (void*)sem, sem ? (unsigned)*sem : 0, (int)kr,
          (void*)((char*)pthread_self() + 0x10));
  }
  return kr;
}
static unsigned pf_ss_n;
int pthread_setspecific(pthread_key_t key, const void* value) {
  if (!pf_real_setspec) {
    return 0;
  }
  if (pf_ss_n++ < 40) {
    PFLOG("INT setspecific key=%u val=%p th=%p\n", (unsigned)key, value,
          (void*)pthread_self());
  }
  return ((int (*)(pthread_key_t, const void*))pf_real_setspec)(key, value);
}
int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m) {
  if ((++pf_cw_n <= 5) || (pf_cw_n % 5000 == 0)) {
    fprintf(stderr, "PFPTH cw n=%u c=%p m=%p\n", pf_cw_n, (void*)c, (void*)m);
  }
  return ((int (*)(pthread_cond_t*, pthread_mutex_t*))pf_real_cond_wait)(c, m);
}

#include <stdio.h>
static volatile int g_atom_dbg;
#define ATOMDBG(tag, sz) do {                                  \
    int _n = __sync_add_and_fetch(&g_atom_dbg, 1);             \
    if (_n <= 200 || (_n % 1000) == 0)                         \
      fprintf(stderr, "ATOM %s sz=%llu n=%d\n",               \
              (tag), (unsigned long long)(sz), _n);            \
  } while (0)

static pthread_mutex_t g_big_atomic_lock = PTHREAD_MUTEX_INITIALIZER;

struct __atomic16 { unsigned long long a, b; };

static bool cas16(struct __atomic16* p, struct __atomic16* e, struct __atomic16 d) {
  unsigned char ok;
  unsigned long long oa = e->a, ob = e->b;
  __asm__ volatile("lock cmpxchg16b %0\n\tsetz %b1"
                   : "+m"(*p), "=q"(ok), "=a"(e->a), "=d"(e->b)
                   : "b"(d.a), "c"(d.b), "a"(oa), "d"(ob)
                   : "memory", "cc");
  return ok;
}

struct __atomic16 __atomic_load_16(const struct __atomic16* p) {
  struct __atomic16 e = {0, 0};
  unsigned long spins = 0;
  for (;;) {
    if (cas16((struct __atomic16*)p, &e, e)) return e;
    if (++spins == 100000UL) {
      fprintf(stderr, "LOAD16 SPIN p=%p val=%llx:%llx\n",
              (void*)p, e.a, e.b);
    }
  }
}
void __atomic_store_16(struct __atomic16* p, struct __atomic16 v) {
  struct __atomic16 e = __atomic_load_16(p);
  while (!cas16(p, &e, v)) e = __atomic_load_16(p);
}
struct __atomic16 __atomic_exchange_16(struct __atomic16* p, struct __atomic16 v) {
  struct __atomic16 e = __atomic_load_16(p);
  while (!cas16(p, &e, v)) e = __atomic_load_16(p);
  return e;
}
bool __atomic_compare_exchange_16(struct __atomic16* p, struct __atomic16* e,
                                  struct __atomic16 d) {
  return cas16(p, e, d);
}

void shim_atomic_load(uint64_t size, const void* ptr, void* ret, int order)
    __asm("___atomic_load");
void shim_atomic_load(uint64_t size, const void* ptr, void* ret, int order) {
  ATOMDBG("load", size);
  if (size <= 1) *(uint8_t*)ret = __atomic_load_1((const uint8_t*)ptr);
  else if (size == 2) *(uint16_t*)ret = __atomic_load_2((const uint16_t*)ptr);
  else if (size <= 4) *(uint32_t*)ret = __atomic_load_4((const uint32_t*)ptr);
  else if (size <= 8) *(uint64_t*)ret = __atomic_load_8((const uint64_t*)ptr);
  else if (size == 16) {
    struct __atomic16 r = __atomic_load_16((const struct __atomic16*)ptr);
    memcpy(ret, &r, 16);
  } else {
    // Sizes the lock-free paths can't serve exactly; a lock is the only
    // correct emulation.
    pthread_mutex_lock(&g_big_atomic_lock);
    memcpy(ret, ptr, size);
    pthread_mutex_unlock(&g_big_atomic_lock);
  }
}
void shim_atomic_store(uint64_t size, void* ptr, const void* val, int order)
    __asm("___atomic_store");
void shim_atomic_store(uint64_t size, void* ptr, const void* val, int order) {
  ATOMDBG("store", size);
  if (size <= 1) __atomic_store_1((uint8_t*)ptr, *(const uint8_t*)val);
  else if (size == 2) __atomic_store_2((uint16_t*)ptr, *(const uint16_t*)val);
  else if (size <= 4) __atomic_store_4((uint32_t*)ptr, *(const uint32_t*)val);
  else if (size <= 8) __atomic_store_8((uint64_t*)ptr, *(const uint64_t*)val);
  else if (size == 16) __atomic_store_16((void*)ptr, *(const struct __atomic16*)val);
  else {
    pthread_mutex_lock(&g_big_atomic_lock);
    memcpy(ptr, val, size);
    pthread_mutex_unlock(&g_big_atomic_lock);
  }
}
void shim_atomic_exchange(uint64_t size, void* ptr, const void* val, void* ret,
                          int order)
    __asm("___atomic_exchange");
void shim_atomic_exchange(uint64_t size, void* ptr, const void* val, void* ret,
                          int order) {
  ATOMDBG("xchg", size);
  if (size <= 8) {
    if (size <= 1) *(uint8_t*)ret = __atomic_exchange_1((uint8_t*)ptr, *(const uint8_t*)val);
    else if (size == 2) *(uint16_t*)ret = __atomic_exchange_2((uint16_t*)ptr, *(const uint16_t*)val);
    else if (size <= 4) *(uint32_t*)ret = __atomic_exchange_4((uint32_t*)ptr, *(const uint32_t*)val);
    else *(uint64_t*)ret = __atomic_exchange_8((uint64_t*)ptr, *(const uint64_t*)val);
  } else if (size == 16) {
    struct __atomic16 r = __atomic_exchange_16((void*)ptr, *(const struct __atomic16*)val);
    memcpy(ret, &r, 16);
  } else {
    pthread_mutex_lock(&g_big_atomic_lock);
    memcpy(ret, ptr, size);
    memcpy((void*)ptr, val, size);
    pthread_mutex_unlock(&g_big_atomic_lock);
  }
}
bool shim_atomic_cas(uint64_t size, void* ptr, void* expected,
                     const void* desired, int success, int failure)
    __asm("___atomic_compare_exchange");
bool shim_atomic_cas(uint64_t size, void* ptr, void* expected,
                     const void* desired, int success, int failure) {
  ATOMDBG("cas", size);
  if (size <= 8) {
    if (size <= 1) return __atomic_compare_exchange_1((uint8_t*)ptr, (uint8_t*)expected, *(const uint8_t*)desired);
    if (size == 2) return __atomic_compare_exchange_2((uint16_t*)ptr, (uint16_t*)expected, *(const uint16_t*)desired);
    if (size <= 4) return __atomic_compare_exchange_4((uint32_t*)ptr, (uint32_t*)expected, *(const uint32_t*)desired);
    return __atomic_compare_exchange_8((uint64_t*)ptr, (uint64_t*)expected, *(const uint64_t*)desired);
  }
  if (size == 16) {
    return cas16((void*)ptr, (void*)expected, *(const struct __atomic16*)desired);
  }
  pthread_mutex_lock(&g_big_atomic_lock);
  bool eq = memcmp(ptr, expected, size) == 0;
  if (eq) memcpy(ptr, desired, size);
  else memcpy(expected, ptr, size);
  pthread_mutex_unlock(&g_big_atomic_lock);
  return eq;
}

void shim_atomic_thread_fence(int order) __asm("___atomic_thread_fence");
void shim_atomic_thread_fence(int order) { __sync_synchronize(); }
void shim_atomic_signal_fence(int order) __asm("___atomic_signal_fence");
void shim_atomic_signal_fence(int order) { __asm__ volatile("" ::: "memory"); }
bool shim_atomic_is_lock_free(uint64_t size, const void* ptr)
    __asm("___atomic_is_lock_free");
bool shim_atomic_is_lock_free(uint64_t size, const void* ptr) {
  ATOMDBG("islf", size); return size <= 16; }

// Runs at dlopen, before any CDM thread exists: creating the TLS keys
// lazily from a racing worker lets threads see g_tlv_key == 0 (a valid
// foreign id) and miss their per-thread block forever.
__attribute__((constructor)) static void init_tlv_keys(void) {
  pthread_once(&g_tlv_once, tlv_key_init);
}
