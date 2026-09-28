/* Standalone 10.6 CDM wedge repro harness: load the shim, then the CDM
 * under flat namespace exactly as GMPLoader does, drive its entry points,
 * and spin-wait. The main thread's pthread lock word is polled for
 * corrupting writes while the CDM runs (no debug-register watchpoint).
 * Build: build.sh (harness.c + host11.cpp)
 * Run:   ./widevine-cdm-harness <shim.dylib> <libwidevinecdm.dylib>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

static volatile int wp_hit = 0;
static uintptr_t wp_val;

#include <sys/mman.h>
static volatile unsigned* wp_word;
static pthread_mutex_t wp_lock = PTHREAD_MUTEX_INITIALIZER;
static void* wp_watcher(void* arg) {
  unsigned prev = *wp_word;
  for (;;) {
    unsigned cur = *wp_word;
    if (cur != prev) {
      pthread_mutex_lock(&wp_lock);
      if (cur != 0 && cur != 0xffffffff && !wp_hit) {
        wp_val = cur;
        wp_hit = 1;
        fprintf(stderr, "H: WORD CHANGED prev=%08x now=%08x\n", prev, cur);
      }
      pthread_mutex_unlock(&wp_lock);
      prev = cur;
    }
    usleep(50);
  }
  return 0;
}
static void arm_word_watcher(void* addr) {
  wp_word = (volatile unsigned*)addr;
  pthread_t w;
  pthread_create(&w, 0, wp_watcher, 0);
  fprintf(stderr, "H: word watcher armed on %p\n", addr);
}


/* Port of GMPLoader's FindDyldSetVariable: 10.9+ dyld exposes a static
 * _dyld_set_variable(key, value) that reprocesses one DYLD_ var at runtime.
 * Resolve it from /usr/lib/dyld's symbol table, slid by the live base. */
#include <fcntl.h>
#include <sys/stat.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <stdlib.h>

static void* find_bytes(const void* h, size_t hl, const void* n, size_t nl) {
  if (!nl || hl < nl) return 0;
  const char* p = h;
  for (size_t i = 0; i + nl <= hl; i++) {
    if (!memcmp(p + i, n, nl)) return (void*)(p + i);
  }
  return 0;
}
typedef void (*DyldSetVarFn)(const char*, const char*);

/* Pre-patch (GMPLoader PatchCDMForTLV): weak the __tlv_bootstrap/__tlv_atexit
 * bind opcodes in a copy of the CDM so the strong TLV bind doesn't fail. */
static char* patch_cdm_for_tlv(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return 0;
  fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
  unsigned char* data = malloc(sz);
  if (fread(data, 1, sz, f) != (size_t)sz) { fclose(f); free(data); return 0; }
  fclose(f);
  static const char* names[2] = {"__tlv_bootstrap", "__tlv_atexit"};
  struct mach_header_64* mh = (struct mach_header_64*)data;
  struct load_command* lc = (struct load_command*)(mh + 1);
  int patched = 0;
  for (uint32_t c = 0; c < mh->ncmds; c++, lc = (struct load_command*)((char*)lc + lc->cmdsize)) {
    if (lc->cmd != LC_DYLD_INFO_ONLY && lc->cmd != LC_DYLD_INFO) continue;
    struct dyld_info_command* di = (struct dyld_info_command*)lc;
    if (!di->bind_off) continue;
    unsigned char* p = data + di->bind_off;
    unsigned char* end = data + di->bind_off + di->bind_size;
    while (p < end) {
      uint8_t op = *p & 0xF0, imm = *p & 0x0F; p++;
      if (imm == 0) { while (p < end && *p) p++; p++; }
      if (op & 0x20 || (op == 0x00 && imm > 0)) { /* ordinal */ }
      if (op == 0x40 || op == 0x50) { p++; }  /* symbol flags byte */
      if (op == 0x60) { /* dylib ordinal varint */ }
      /* symbol name: after a 0x00-imm seg-set or at bind entry start... simplified: scan raw */
      (void)0;
      break;  /* streaming parser too complex for inline; use raw scan below */
    }
    /* raw scan: find name strings in bind blob followed by opcode 0x40 */
    for (int n = 0; n < 2; n++) {
      size_t nl = strlen(names[n]);
      unsigned char* q = data + di->bind_off;
      unsigned char* qe = data + di->bind_off + di->bind_size;
      while (q < qe) {
        unsigned char* hit = find_bytes(q, qe - q, names[n], nl);
        if (!hit || hit[nl] != 0) { if (!hit) break; q = hit + 1; continue; }
        /* the opcode byte directly PRECEDES the name */
        if (hit > data + di->bind_off && (hit[-1] & 0xF0) == 0x40 && (hit[-1] & 0x0F) == 0) {
          hit[-1] = 0x41;
          patched++;
        }
        q = hit + 1;
      }
    }
  }
  fprintf(stderr, "H: TLV patch — %d binds weakened\n", patched);
  if (!patched) { free(data); return 0; }
  char* out = malloc(strlen(path) + 32);
  sprintf(out, "%s.legacy", path);
  FILE* o = fopen(out, "wb");
  fwrite(data, 1, sz, o);
  fclose(o);
  free(data);
  return out;
}


static DyldSetVarFn find_dyld_set_variable(void) {
  /* 10.6 dyld is not slid and carries __ZL18_dyld_set_variablePKcS0_ as a
   * local text symbol at a fixed address; nm reports the runtime address. */
  FILE* fp = popen("nm /usr/lib/dyld 2>/dev/null | grep dyld_set_variable"
                   " | awk '{print $1}'", "r");
  if (!fp) return 0;
  char hex[32] = {0};
  if (!fgets(hex, sizeof hex, fp)) { pclose(fp); return 0; }
  pclose(fp);
  uint64_t addr = strtoull(hex, 0, 16);
  if (addr < 0x7fff00000000ull) return 0;
  fprintf(stderr, "H: setvar nm addr=%llx\n", (unsigned long long)addr);
  return (DyldSetVarFn)(uintptr_t)addr;
}


/* Port of GMPLoader FixupCDMImage: canonicalize selector references, rebind
 * the objc_msgSend slots to the shim's canonicalizing hooks, and install the
 * shim's TLV bootstrap into descriptors the weak bind left zeroed. */
#include <objc/objc.h>
#include <objc/runtime.h>
static int is_objc_send_slot(const char* name) {
  return !strcmp(name, "_objc_msgSend") ||
         !strcmp(name, "_objc_msgSend_stret") ||
         !strcmp(name, "_objc_msgSend_fpret") ||
         !strcmp(name, "_objc_msgSendSuper");
}
static void fixup_cdm_objc(void* aShim, void* cdmBase) {
  struct mach_header_64* mh = (struct mach_header_64*)cdmBase;
  /* compute slide: first section address vs file vmaddr */
  intptr_t slide = 0;
  {
    struct load_command* lc0 = (struct load_command*)(mh + 1);
    uintptr_t firstVm = 0;
    for (uint32_t c = 0; c < mh->ncmds; c++, lc0 = (struct load_command*)((char*)lc0 + lc0->cmdsize)) {
      if (lc0->cmd != LC_SEGMENT_64) continue;
      struct segment_command_64* sg = (struct segment_command_64*)lc0;
      if (!strcmp(sg->segname, "__TEXT")) { firstVm = sg->vmaddr; break; }
    }
    slide = (intptr_t)mh - (intptr_t)firstVm;
  }
  struct load_command* lc = (struct load_command*)(mh + 1);
  struct symtab_command* symtab = 0;
  struct dysymtab_command* dysym = 0;
  uintptr_t linkeditRuntime = 0;
  for (uint32_t c = 0; c < mh->ncmds; c++, lc = (struct load_command*)((char*)lc + lc->cmdsize)) {
    if (lc->cmd == LC_SYMTAB) {
      symtab = (struct symtab_command*)lc;
    } else if (lc->cmd == LC_DYSYMTAB) {
      dysym = (struct dysymtab_command*)lc;
    } else if (lc->cmd == LC_SEGMENT_64) {
      struct segment_command_64* sg = (struct segment_command_64*)lc;
      if (!strcmp(sg->segname, SEG_LINKEDIT)) {
        linkeditRuntime = (uintptr_t)sg->vmaddr + slide - sg->fileoff;
      }
    }
  }
  if (!symtab || !linkeditRuntime) return;
  struct nlist_64* syms = (struct nlist_64*)(linkeditRuntime + symtab->symoff);
  const char* strs = (const char*)(linkeditRuntime + symtab->stroff);
  uint32_t* indirect =
      dysym ? (uint32_t*)(linkeditRuntime + dysym->indirectsymoff) : 0;
  void* tlvBootstrap = dlsym(aShim, "__tlv_bootstrap");
  int nsel = 0, nsend = 0, ntlv = 0;
  lc = (struct load_command*)(mh + 1);
  fprintf(stderr, "H: objc fixup base=%p slide=%ld\n", cdmBase, (long)slide);
  for (uint32_t c = 0; c < mh->ncmds; c++, lc = (struct load_command*)((char*)lc + lc->cmdsize)) {
    if (lc->cmd != LC_SEGMENT_64) continue;
    struct segment_command_64* sg = (struct segment_command_64*)lc;
    struct section_64* sec = (struct section_64*)((char*)sg + sizeof(struct segment_command_64));
    for (uint32_t k = 0; k < sg->nsects; k++, sec++) {
      uintptr_t addr = (uintptr_t)(sec->addr + slide);
      size_t count = sec->size / sizeof(void*);
      if (!strcmp(sec->sectname, "__objc_selrefs")) {
        mprotect((void*)(addr & ~4095UL), (sec->size + 4095) & ~4095UL, PROT_READ | PROT_WRITE);
        SEL* refs = (SEL*)addr;
        for (size_t j = 0; j < count; j++) {
          if (refs[j]) refs[j] = sel_registerName((const char*)refs[j]);
        }
        nsel += (int)count;
      } else if (!strcmp(sec->sectname, "__thread_vars")) {
        if (!tlvBootstrap) continue;
        mprotect((void*)(addr & ~4095UL), (sec->size + 4095) & ~4095UL, PROT_READ | PROT_WRITE);
        void** thunks = (void**)addr;
        for (size_t j = 0; j < count / 3; j++) {
          if (!thunks[j * 3]) {
            thunks[j * 3] = tlvBootstrap;
            ntlv++;
          }
        }
      } else {
        uint32_t type = sec->flags & SECTION_TYPE;
        if (!indirect ||
            (type != S_LAZY_SYMBOL_POINTERS && type != S_NON_LAZY_SYMBOL_POINTERS)) {
          continue;
        }
        mprotect((void*)(addr & ~4095UL), (sec->size + 4095) & ~4095UL, PROT_READ | PROT_WRITE);
        void** ptrs = (void**)addr;
        for (size_t j = 0; j < count; j++) {
          uint32_t idx = indirect[sec->reserved1 + j];
          if (idx == INDIRECT_SYMBOL_ABS || idx == INDIRECT_SYMBOL_LOCAL ||
              idx >= symtab->nsyms) {
            continue;
          }
          const char* name = strs + syms[idx].n_un.n_strx;
          if (is_objc_send_slot(name)) {
            void* hook = dlsym(aShim, name + 1);
            if (hook) {
              ptrs[j] = hook;
              nsend++;
            }
          }
        }
      }
    }
  }
  fprintf(stderr, "H: canonicalized %d selrefs, rebound %d send slots, %d TLV thunks\n",
          nsel, nsend, ntlv);
}


/* The real C++ Host_10/Host_11 lives in host11.cpp; the CDM validates the
 * host through its vtable before instance creation, so a genuine object
 * (not a stub slot table) must come back from the provider. */
extern void* harness_get_host(int);

static pthread_key_t g_worker_key;
static void worker_key_dtor(void* v) {
  fprintf(stderr, "H: worker key dtor ran\n");
}
static void* worker(void* arg) {
  /* CDM-style transient worker: real per-thread storage via a pthread key
   * (the 10.6 target has no TLV), released by the key destructor at thread
   * exit while main spins. */
  char tls_pad[256];
  tls_pad[0] = 1;
  pthread_setspecific(g_worker_key, tls_pad);
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <shim> <cdm>\n", argv[0]);
    return 1;
  }
  pthread_key_create(&g_worker_key, worker_key_dtor);
  pthread_t t;
  pthread_create(&t, 0, worker, 0);
  pthread_join(t, 0);  /* short-lived worker: replicate flicker-thread shape */

  /* Foundation must initialize before the flat flip: the CDM touches
   * NSThread during init and under flat binding its class machinery breaks
   * if Foundation initializes lazily against the shim's reexports. */
  void* foundation = dlopen(
      "/System/Library/Frameworks/Foundation.framework/Foundation",
      RTLD_NOW | RTLD_GLOBAL);
  fprintf(stderr, "H: foundation pre-load=%p\n", foundation);

  void* shim = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
  if (!shim) {
    fprintf(stderr, "H: shim load failed: %s\n", dlerror());
    return 1;
  }
  void (*shim_init)(void) = dlsym(shim, "WidevineLegacyShimInit");
  if (shim_init) shim_init();
  fprintf(stderr, "H: shim loaded+init\n");

  /* Poll THIS thread's pthread lock word for corrupting writes before CDM. */
  pthread_t self = pthread_self();
  unsigned* wp = (unsigned*)((char*)self + 0x10);
  arm_word_watcher(wp);

  /* Flat-namespace CDM load via dyld's runtime variable setter, exactly
   * as GMPLoader's fallback (setenv after start does nothing). */
  {
    DyldSetVarFn setvar = find_dyld_set_variable();
    fprintf(stderr, "H: dyld setvar=%p\n", (void*)setvar);
    if (setvar) {
      setvar("DYLD_FORCE_FLAT_NAMESPACE", "1");
      fprintf(stderr, "H: flat namespace flipped ON\n");
    }
  }
  char* patchedPath = patch_cdm_for_tlv(argv[2]);
  const char* loadPath = patchedPath ? patchedPath : argv[2];
  fprintf(stderr, "H: loading %s\n", loadPath);
  void* cdm = dlopen(loadPath, RTLD_NOW | RTLD_LOCAL);
  if (!cdm) {
    fprintf(stderr, "H: CDM load failed: %s\n", dlerror());
    return 1;
  }
  fprintf(stderr, "H: CDM loaded flat\n");
  {
    Dl_info di;
    if (dladdr(dlsym(cdm, "InitializeCdmModule_4"), &di) && di.dli_fbase) {
      fixup_cdm_objc(shim, di.dli_fbase);
    }
  }

  void (*init_mod)(void) = dlsym(cdm, "InitializeCdmModule_4");
  if (init_mod) {
    fprintf(stderr, "H: InitializeCdmModule_4...\n");
    init_mod();
    fprintf(stderr, "H: module init done\n");
  }
  typedef void* (*CreateFn)(int, const char*, uint32_t, void*, void*);
  CreateFn create = (CreateFn)dlsym(cdm, "CreateCdmInstance");
  if (create) {
    fprintf(stderr, "H: CreateCdmInstance...\n");
    void* inst = create(11, "com.widevine.alpha", 16, harness_get_host, 0);
    fprintf(stderr, "H: instance=%p\n", inst);
  }
  fprintf(stderr, "H: sequence complete — watching\n");
  for (int i = 0; i < 120 && !wp_hit; i++) {
    usleep(500000);
  }
  if (wp_hit) {
    fprintf(stderr, "H: word watcher: lock-word change caught val=%lx\n", wp_val);
  } else {
    fprintf(stderr, "H: no lock-word change observed in 60s\n");
  }
  return 0;
}
