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
  return (uint64_t)((__uint128_t)mach_absolute_time() * tb.numer / tb.denom);
}

extern void arc4random_buf(void*, size_t);
int getentropy(void* buf, size_t len) {
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

int pthread_set_qos_class_self_np(qos_class_t cls, int prio) { return 0; }
void swap_linkedit_data_command(void) {}

int os_signpost_enabled(void* log) { return 0; }

// 10.10-era Mach port APIs; report unsupported so callers fall back.
kern_return_t shim_mach_port_construct(mach_port_t task,
                                        const struct mach_port_options* options,
                                        mach_port_context_t context,
                                        mach_port_t* port)
    __asm("_mach_port_construct");
kern_return_t shim_mach_port_construct(mach_port_t task,
                                        const struct mach_port_options* options,
                                        mach_port_context_t context,
                                        mach_port_t* port) {
  mach_port_t name;
  kern_return_t kr = mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &name);
  if (kr != KERN_SUCCESS) {
    return kr;
  }
  if (options && (options->flags & MPO_INSERT_SEND_RIGHT)) {
    kr = mach_port_insert_right(task, name, name, MACH_MSG_TYPE_MAKE_SEND);
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
  return 7;
}
kern_return_t shim_mach_port_peek(mach_port_t task, mach_port_t name,
                                  void* options, uint64_t wait,
                                  void* ev, void* msg_size, void* msg_id,
                                  void* trailer)
    __asm("_mach_port_peek");
kern_return_t shim_mach_port_peek(mach_port_t task, mach_port_t name,
                                  void* options, uint64_t wait,
                                  void* ev, void* msg_size, void* msg_id,
                                  void* trailer) {
  return 7;
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
  // 10.6 pthread runs key destructors while holding the global pthread
  // lock; running CDM destructors there can orphan that lock and wedge
  // the process. Accept the bounded per-thread leak instead: keys with
  // no destructor, terminators never invoked.
  if (pthread_key_create(&g_tlv_terms_key, NULL) != 0 ||
      pthread_key_create(&g_tlv_key, NULL) != 0) {
    g_tlv_shared = 1;
  }
}

// Called by GMPLoader right after dlopening the shim, before any CDM code
// can run: creating the keys lazily from a CDM worker would let racing
// threads see g_tlv_key == 0, which is a valid (foreign) key id, and their
// per-thread lookups would miss forever.
void WidevineLegacyShimInit(void) {
  pthread_once(&g_tlv_once, tlv_key_init);
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
  for (;;) {
    if (cas16((struct __atomic16*)p, &e, e)) return e;
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
                          int order) __asm("___atomic_exchange");
void shim_atomic_exchange(uint64_t size, void* ptr, const void* val, void* ret,
                          int order) {
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
bool shim_atomic_is_lock_free(uint64_t size, const void* ptr) { return size <= 16; }

// Runs at dlopen, before any CDM thread exists: creating the TLS keys
// lazily from a racing worker lets threads see g_tlv_key == 0 (a valid
// foreign id) and miss their per-thread block forever.
__attribute__((constructor)) static void init_tlv_keys(void) {
  pthread_once(&g_tlv_once, tlv_key_init);
}
