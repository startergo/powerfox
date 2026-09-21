/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPLoader.h"

#include "GMPLog.h"
#include "gmp-entrypoints.h"
#include "nsExceptionHandler.h"
#include "prenv.h"
#include "prerror.h"
#include "prlink.h"
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
#  include "mozilla/sandboxTarget.h"
#  include "nsWindowsHelpers.h"
#endif
#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
#  include "mozilla/Sandbox.h"
#  include "mozilla/SandboxInfo.h"
#  include "mozilla/SandboxProfilerObserver.h"
#endif

#ifdef XP_WIN
#  include <windows.h>
#endif
#ifdef XP_MACOSX
#  include <dlfcn.h>
#  include <fcntl.h>
#  include <limits.h>
#  include <mach-o/dyld.h>
#  include <mach-o/fat.h>
#  include <mach-o/loader.h>
#  include <mach-o/nlist.h>
#  include <mach/mach.h>
#  include <mach/mach_vm.h>
#  include <objc/objc.h>
#  include <objc/runtime.h>
#  include <string.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace mozilla::gmp {
class PassThroughGMPAdapter : public GMPAdapter {
 public:
  ~PassThroughGMPAdapter() override {
    // Ensure we're always shutdown, even if caller forgets to call
    // GMPShutdown().
    GMPShutdown();
  }

  void SetAdaptee(PRLibrary* aLib) override { mLib = aLib; }

  GMPErr GMPInit(const GMPPlatformAPI* aPlatformAPI) override {
    if (NS_WARN_IF(!mLib)) {
      MOZ_CRASH("Missing library!");
      return GMPGenericErr;
    }
    GMPInitFunc initFunc =
        reinterpret_cast<GMPInitFunc>(PR_FindFunctionSymbol(mLib, "GMPInit"));
    if (!initFunc) {
      MOZ_CRASH("Missing init method!");
      return GMPNotImplementedErr;
    }
    return initFunc(aPlatformAPI);
  }

  GMPErr GMPGetAPI(const char* aAPIName, void* aHostAPI, void** aPluginAPI,
                   const nsACString& /* aKeySystem */) override {
    if (!mLib) {
      return GMPGenericErr;
    }
    GMPGetAPIFunc getapiFunc = reinterpret_cast<GMPGetAPIFunc>(
        PR_FindFunctionSymbol(mLib, "GMPGetAPI"));
    if (!getapiFunc) {
      return GMPNotImplementedErr;
    }
    return getapiFunc(aAPIName, aHostAPI, aPluginAPI);
  }

  void GMPShutdown() override {
    if (mLib) {
      GMPShutdownFunc shutdownFunc = reinterpret_cast<GMPShutdownFunc>(
          PR_FindFunctionSymbol(mLib, "GMPShutdown"));
      if (shutdownFunc) {
        shutdownFunc();
      }
      PR_UnloadLibrary(mLib);
      mLib = nullptr;
    }
  }

 private:
  PRLibrary* mLib = nullptr;
};

#ifdef XP_MACOSX
namespace {

// Widevine CDMs are built for modern macOS. On pre-10.12 systems the plain
// load fails: the CDM hard-imports os_log-era libSystem symbols, and its
// ObjC metadata uses patterns old runtimes don't process. The shim dylib
// next to XUL fills the symbol gaps; loading with dyld's flat namespace
// flipped on makes the CDM bind against it, and the fixups below repair
// its selector references and message-send slots.

void* CDMShim() {
  static void* sShim = nullptr;
  static bool sTried = false;
  if (sTried) {
    return sShim;
  }
  sTried = true;
  Dl_info info;
  if (dladdr((void*)&CDMShim, &info) && info.dli_fname) {
    char path[PATH_MAX];
    size_t len = strlen(info.dli_fname);
    const char* slash = strrchr(info.dli_fname, '/');
    if (slash && len + 32 < sizeof(path)) {
      memcpy(path, info.dli_fname, slash + 1 - info.dli_fname);
      strcpy(path + (slash + 1 - info.dli_fname),
             "libWidevineLegacyShim.dylib");
      sShim = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
      if (sShim) {
        // The shim creates its TLS keys in a constructor; this explicit
        // call covers shim builds predating that.
        typedef void (*InitFn)(void);
        InitFn init = (InitFn)dlsym(sShim, "WidevineLegacyShimInit");
        if (init) {
          init();
        }
      }
    }
  }
  return sShim;
}

typedef void (*DyldSetVarFn)(const char*, const char*);

DyldSetVarFn FindDyldSetVariable() {
  // dyld >= 239.4 (macOS 10.9) has a static _dyld_set_variable(key, value)
  // that re-processes one DYLD_ variable at runtime. Resolve it through the
  // on-disk symbol table, relocated by dyld's runtime base.
  int fd = open("/usr/lib/dyld", O_RDONLY);
  if (fd < 0) {
    return nullptr;
  }
  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size < 0x1000) {
    close(fd);
    return nullptr;
  }
  unsigned char* data = (unsigned char*)malloc(st.st_size);
  if (!data || read(fd, data, st.st_size) != st.st_size) {
    free(data);
    close(fd);
    return nullptr;
  }
  close(fd);

  const unsigned char* mh = data;
  uint32_t magic;
  memcpy(&magic, mh, 4);
  if (OSSwapBigToHostInt32(magic) == FAT_MAGIC) {
    uint32_t nfat;
    memcpy(&nfat, mh + 4, 4);
    nfat = OSSwapBigToHostInt32(nfat);
    const unsigned char* fa = mh + 8;
    for (uint32_t i = 0; i < nfat; i++, fa += 20) {
      int32_t cputype;
      memcpy(&cputype, fa, 4);
      if (OSSwapBigToHostInt32(cputype) == CPU_TYPE_X86_64) {
        uint32_t off;
        memcpy(&off, fa + 8, 4);
        mh = data + OSSwapBigToHostInt32(off);
        break;
      }
    }
  }
  memcpy(&magic, mh, 4);
  if (magic != MH_MAGIC_64) {
    free(data);
    return nullptr;
  }

  uint32_t ncmds;
  memcpy(&ncmds, mh + 16, 4);
  const unsigned char* lc = mh + 32;
  uint64_t textvm = 0;
  uint64_t symval = 0;
  const symtab_command* symtab = nullptr;
  for (uint32_t i = 0; i < ncmds; i++) {
    uint32_t cmd, cmdsize;
    memcpy(&cmd, lc, 4);
    memcpy(&cmdsize, lc + 4, 4);
    if (cmd == LC_SEGMENT_64) {
      char segname[17];
      memcpy(segname, lc + 8, 16);
      segname[16] = 0;
      if (strcmp(segname, SEG_TEXT) == 0) {
        memcpy(&textvm, lc + 24, 8);
      }
    } else if (cmd == LC_SYMTAB) {
      symtab = (const symtab_command*)lc;
    }
    lc += cmdsize;
  }
  if (symtab && textvm) {
    const nlist_64* syms = (const nlist_64*)(mh + symtab->symoff);
    const char* strs = (const char*)(mh + symtab->stroff);
    for (uint32_t k = 0; k < symtab->nsyms; k++) {
      if (syms[k].n_un.n_strx < symtab->strsize &&
          strcmp(strs + syms[k].n_un.n_strx,
                 "__ZL18_dyld_set_variablePKcS0_") == 0) {
        symval = syms[k].n_value;
        break;
      }
    }
  }
  free(data);
  if (!symval || !textvm) {
    return nullptr;
  }

  task_t task = mach_task_self();
  struct task_dyld_info ti;
  mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
  if (task_info(task, TASK_DYLD_INFO, (task_info_t)&ti, &count) !=
      KERN_SUCCESS) {
    return nullptr;
  }
  // struct dyld_all_image_infos: dyldImageLoadAddress sits at offset 0x20.
  uint64_t dyldHeader;
  memcpy(&dyldHeader, (const void*)(ti.all_image_info_addr + 0x20), 8);
  long slide = (long)dyldHeader - (long)textvm;
  if (labs(slide) > 0x10000000 || (long)dyldHeader < 0) {
    return nullptr;
  }
  return (DyldSetVarFn)(uintptr_t)(symval + slide);
}

bool IsObjCSendSlotName(const char* aName) {
  return strcmp(aName, "_objc_msgSend") == 0 ||
         strcmp(aName, "_objc_msgSend_stret") == 0 ||
         strcmp(aName, "_objc_msgSend_fpret") == 0 ||
         strcmp(aName, "_objc_msgSendSuper") == 0;
}

void FixupCDMImage(void* aShim, const char* aLibPath) {
  const char* base = strrchr(aLibPath, '/');
  base = base ? base + 1 : aLibPath;
  size_t baseLen = strlen(base);
  const mach_header_64* hdr = nullptr;
  intptr_t slide = 0;
  for (uint32_t i = 0; i < _dyld_image_count(); i++) {
    const char* name = _dyld_get_image_name(i);
    if (!name) {
      continue;
    }
    size_t nameLen = strlen(name);
    // An empty basename (trailing '/') would suffix-match every image, so
    // require one and anchor the suffix to a path separator.
    bool match = strcmp(name, aLibPath) == 0;
    if (!match && baseLen > 0 && nameLen > baseLen + 1 &&
        name[nameLen - baseLen - 1] == '/' &&
        strcmp(name + nameLen - baseLen, base) == 0) {
      match = true;
    }
    if (match) {
      hdr = (const mach_header_64*)_dyld_get_image_header(i);
      slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!hdr) {
    return;
  }

  const load_command* lc = (const load_command*)(hdr + 1);
  const symtab_command* symtab = nullptr;
  const dysymtab_command* dysym = nullptr;
  uint64_t linkeditRuntime = 0;
  for (uint32_t i = 0; i < hdr->ncmds;
       i++, lc = (const load_command*)((const char*)lc + lc->cmdsize)) {
    if (lc->cmd == LC_SYMTAB) {
      symtab = (const symtab_command*)lc;
    } else if (lc->cmd == LC_DYSYMTAB) {
      dysym = (const dysymtab_command*)lc;
    } else if (lc->cmd == LC_SEGMENT_64) {
      const segment_command_64* sg = (const segment_command_64*)lc;
      if (strncmp(sg->segname, SEG_LINKEDIT, 16) == 0) {
        linkeditRuntime = sg->vmaddr + slide - sg->fileoff;
      }
    }
  }
  if (!symtab || !linkeditRuntime) {
    return;
  }
  const nlist_64* syms = (const nlist_64*)(linkeditRuntime + symtab->symoff);
  const char* strs = (const char*)(linkeditRuntime + symtab->stroff);
  const uint32_t* indirect =
      dysym ? (const uint32_t*)(linkeditRuntime + dysym->indirectsymoff)
            : nullptr;

  lc = (const load_command*)(hdr + 1);
  for (uint32_t i = 0; i < hdr->ncmds;
       i++, lc = (const load_command*)((const char*)lc + lc->cmdsize)) {
    if (lc->cmd != LC_SEGMENT_64) {
      continue;
    }
    const segment_command_64* sg = (const segment_command_64*)lc;
    if (strncmp(sg->segname, "__DATA", 6) != 0) {
      continue;
    }
    const section_64* sec = (const section_64*)((const char*)sg + sizeof(*sg));
    for (uint32_t s = 0; s < sg->nsects; s++, sec++) {
      void* slots = (void*)(uintptr_t)(sec->addr + slide);
      size_t count = sec->size / sizeof(void*);
      if (strcmp(sec->sectname, "__thread_vars") == 0) {
        // Where the weak-patched TLV binds zeroed the descriptor thunks
        // (10.6), install the shim's bootstrap; on 10.7+ dyld bound the
        // system TLV runtime and the thunks stay untouched.
        void** thunks = (void**)slots;
        void* tlvBootstrap = dlsym(aShim, "__tlv_bootstrap");
        mprotect(slots, (sec->size + 4095) & ~4095UL, PROT_READ | PROT_WRITE);
        for (size_t k = 0; k < count / 3 && tlvBootstrap; k++) {
          if (!thunks[k * 3]) {
            thunks[k * 3] = tlvBootstrap;
          }
        }
        continue;
      }
      if (strcmp(sec->sectname, "__objc_selrefs") == 0) {
        // The old ObjC runtime never canonicalized this image's selector
        // references; the CDM also passes embedded name strings directly.
        mprotect(slots, (sec->size + 4095) & ~4095UL, PROT_READ | PROT_WRITE);
        SEL* refs = (SEL*)slots;
        for (size_t k = 0; k < count; k++) {
          if (refs[k]) {
            refs[k] = sel_registerName((const char*)refs[k]);
          }
        }
        continue;
      }
      uint32_t type = sec->flags & SECTION_TYPE;
      if (!indirect || (type != S_LAZY_SYMBOL_POINTERS &&
                        type != S_NON_LAZY_SYMBOL_POINTERS)) {
        continue;
      }
      mprotect(slots, (sec->size + 4095) & ~4095UL, PROT_READ | PROT_WRITE);
      void** ptrs = (void**)slots;
      for (size_t k = 0; k < count; k++) {
        uint32_t idx = indirect[sec->reserved1 + k];
        if (idx == INDIRECT_SYMBOL_ABS || idx == INDIRECT_SYMBOL_LOCAL) {
          continue;
        }
        if (idx >= symtab->nsyms) {
          continue;
        }
        const char* name = strs + syms[idx].n_un.n_strx;
        if (IsObjCSendSlotName(name)) {
          void* hook = dlsym(aShim, name + 1);
          if (hook) {
            ptrs[k] = hook;
          }
        }
      }
    }
  }
}

// dyld on 10.6 cannot bind the CDM's thread-local descriptor thunks (the
// bind throws even when the symbol is available; 10.7+ binds them against
// libSystem's TLV runtime). Marking those two bind entries weak makes the
// bind silently zero them; FixupCDMImage installs our thunk afterwards.
// Writing the one-byte flags edits into a sibling copy keeps the CDM file
// itself untouched. Returns null when no patch is needed or it fails; the
// caller then loads the original path.
const unsigned char* FindBytes(const unsigned char* aHaystack, size_t aHayLen,
                               const char* aNeedle, size_t aNeedleLen) {
  if (aNeedleLen == 0 || aHayLen < aNeedleLen) {
    return nullptr;
  }
  for (size_t i = 0; i + aNeedleLen <= aHayLen; i++) {
    if (memcmp(aHaystack + i, aNeedle, aNeedleLen) == 0) {
      return aHaystack + i;
    }
  }
  return nullptr;
}

char* PatchCDMForTLV(const char* aLibPath) {
  static const char* kNames[2] = {"__tlv_bootstrap", "__tlv_atexit"};
  int fd = open(aLibPath, O_RDONLY);
  if (fd < 0) {
    return nullptr;
  }
  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size < 0x1000 || st.st_size > 0x8000000) {
    close(fd);
    return nullptr;
  }
  size_t size = st.st_size;
  unsigned char* data = (unsigned char*)malloc(size);
  if (!data || read(fd, data, size) != (ssize_t)size) {
    free(data);
    close(fd);
    return nullptr;
  }
  close(fd);

  int patched = 0;
  for (size_t n = 0; n < sizeof(kNames) / sizeof(kNames[0]); n++) {
    size_t len = strlen(kNames[n]);
    const unsigned char* p = data;
    const unsigned char* end = data + size;
    while ((p = FindBytes(p, end - p, kNames[n], len)) && p + len < end &&
           p[len] == 0) {
      // The SET_SYMBOL_TRAILING_FLAGS opcode (0x40 | flags) directly
      // precedes the name; flags 0x01 is a weak import.
      if (p > data && (p[-1] & 0xf0) == 0x40 && (p[-1] & 0x0f) == 0) {
        ((unsigned char*)p)[-1] = 0x41;
        patched++;
      }
      p += len;
    }
  }
  if (!patched) {
    free(data);
    return nullptr;
  }

  size_t outLen = strlen(aLibPath) + 32;
  char* out = (char*)malloc(outLen);
  if (!out) {
    free(data);
    return nullptr;
  }
  snprintf(out, outLen, "%s.legacy", aLibPath);
  int outFd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (outFd < 0 || write(outFd, data, size) != (ssize_t)size) {
    close(outFd);
    free(data);
    free(out);
    return nullptr;
  }
  close(outFd);
  free(data);
  return out;
}

PRLibrary* LoadCDMWithLegacySupport(const PRLibSpec& aSpec,
                                    const char* aLibPath) {
  void* shim = CDMShim();
  if (!shim) {
    return nullptr;
  }
  DyldSetVarFn setvar = FindDyldSetVariable();
  if (!setvar) {
    return nullptr;
  }
  // The CDM hard-depends on LocalAuthentication, CryptoTokenKit,
  // libpmenergy and libpmsample, which only exist from 10.10 on. Stub
  // copies ship in the bundle; dyld's fallback search paths (settable
  // through the same dyld helper) resolve the dependencies from there on
  // older systems.
  Dl_info info;
  if (dladdr((void*)&LoadCDMWithLegacySupport, &info) && info.dli_fname) {
    char libDir[PATH_MAX], fwDir[PATH_MAX], fwFile[PATH_MAX];
    size_t len = strlen(info.dli_fname);
    const char* slash = strrchr(info.dli_fname, '/');
    if (slash && slash + 2 < info.dli_fname + len &&
        len + 64 < sizeof(libDir)) {
      memcpy(libDir, info.dli_fname, slash - info.dli_fname);
      libDir[slash - info.dli_fname] = 0;
      snprintf(fwFile, sizeof(fwFile),
               "%s/../Frameworks/"
               "LocalAuthentication.framework",
               libDir);
      struct stat fwst;
      if (stat(fwFile, &fwst) == 0) {
        snprintf(fwDir, sizeof(fwDir), "%s/../Frameworks", libDir);
        setvar("DYLD_FALLBACK_FRAMEWORK_PATH", fwDir);
        setvar("DYLD_FALLBACK_LIBRARY_PATH", libDir);
      }
    }
  }
  char* patchedPath = PatchCDMForTLV(aLibPath);
  const char* loadPath = patchedPath ? patchedPath : aLibPath;
  PRLibSpec spec = aSpec;
  spec.value.pathname = loadPath;

  setvar("DYLD_FORCE_FLAT_NAMESPACE", "1");
  PRLibrary* lib = PR_LoadLibraryWithFlags(spec, PR_LD_NOW);
  setvar("DYLD_FORCE_FLAT_NAMESPACE", "0");
  if (lib) {
    FixupCDMImage(shim, loadPath);
  }
  free(patchedPath);
  return lib;
}

}  // namespace
#endif  // XP_MACOSX

#ifdef XP_MACOSX
// The x86 Widevine CDM reports "x86-64" as its license requests'
// architecture_name: a compiled-in constant decrypted into a heap table at
// first use, and the only machine identity the request carries (its bytes are
// otherwise identical across OS versions). Services that have dropped x86 Mac
// clients reject the license on it. Rewrite the decrypted string to the arm64
// value before the CDM builds and signs its next request; hits are scoped by
// requiring the table's sibling client_info strings on the same page so no
// unrelated "x86-64" bytes are touched. The table persists, so the first
// request of a session may race the rewrite while later ones cannot.
void PatchWidevineArchIdentity() {
  mach_vm_address_t addr = 0;
  mach_vm_size_t size = 0;
  for (;;) {
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object;
    if (mach_vm_region(mach_task_self(), &addr, &size, VM_REGION_BASIC_INFO_64,
                       (vm_region_info_t)&info, &count,
                       &object) != KERN_SUCCESS) {
      return;
    }
    if ((info.protection & VM_PROT_WRITE) && size < 0x8000000) {
      unsigned char* base = (unsigned char*)addr;
      for (size_t o = 0; o + 6 <= size; o++) {
        if (memcmp(base + o, "x86-64", 6) != 0) {
          continue;
        }
        uintptr_t pageStart = (uintptr_t)(base + o) & ~(uintptr_t)4095;
        size_t pageAvail = addr + size - (mach_vm_address_t)pageStart;
        size_t pageLen = pageAvail < 4096 ? pageAvail : 4096;
        if (!FindBytes((const unsigned char*)pageStart, pageLen, "MacOSX", 6) &&
            !FindBytes((const unsigned char*)pageStart, pageLen, "ChromeCDM",
                       9)) {
          continue;
        }
        base[o] = 'a';
        base[o + 1] = 'r';
        base[o + 2] = 'm';
        base[o + 3] = '6';
        base[o + 4] = '4';
        base[o + 5] = 0;
      }
    }
    addr += size;
  }
}
#endif  // XP_MACOSX

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
// This performs the same checks for an AppLocker policy that are performed in
// SaferpIsV2PolicyPresent from ntdll.dll, they are used to decide whether an
// AppLocker ioctl call is made.
static bool IsAppLockerPolicyPresent() {
  // RuleCount check for policy configured via Local Security Policy.
  DWORD ruleCount = 0;
  DWORD ruleCountSize = sizeof(ruleCount);
  if (RegGetValueW(HKEY_LOCAL_MACHINE,
                   LR"(SYSTEM\CurrentControlSet\Control\Srp\GP)", L"RuleCount",
                   RRF_RT_REG_DWORD, nullptr, &ruleCount,
                   &ruleCountSize) == ERROR_SUCCESS &&
      ruleCount != 0) {
    return true;
  }

  // Directory check for policy configured via Mobile Device Management.
  static constexpr wchar_t appLockerMDMPath[] = LR"(\System32\AppLocker\MDM)";
  wchar_t path[MAX_PATH + sizeof(appLockerMDMPath) / sizeof(wchar_t)];
  UINT len = GetSystemWindowsDirectoryW(path, MAX_PATH);
  if (len != 0 && len < MAX_PATH) {
    wcscpy(path + len, appLockerMDMPath);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
  }
  return false;
}

static void EnsureAppLockerCacheIsWarm(const wchar_t* aWidePath) {
  // IOCTL to \Device\SrpDevice (\\.\SrpDevice via DosDevices) that triggers
  // AppLocker to cache the allow/deny decision for the DLL, warming the NTFS
  // EA cache before the sandbox starts.
  static constexpr DWORD IOCTL_SRP_VERIFY_DLL = 0x225804;
  static constexpr wchar_t kSrpDevicePath[] = LR"(\\.\SrpDevice)";

  // Buffer layout: [HANDLE as 8 bytes][USHORT pathBytes][WCHAR path...]
  // The handle field is always 8 bytes. On x86 the handle is zero-extended.
  struct SrpIoctlBuffer {
    uint64_t handle;
    USHORT pathBytes;
    WCHAR path[1];
  };
  static constexpr DWORD kSrpHeaderSize = offsetof(SrpIoctlBuffer, path);

  UniquePtr<HANDLE, CloseHandleDeleter> fileHandle(CreateFileW(
      aWidePath, FILE_READ_DATA | FILE_EXECUTE | SYNCHRONIZE,
      FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr));
  if (fileHandle.get() == INVALID_HANDLE_VALUE) {
    GMP_LOG_WARNING("EnsureAppLockerCacheIsWarm: CreateFileW failed ({})",
                    GetLastError());
    return;
  }

  NtPathFromDosPath ntPath(aWidePath);
  if (!ntPath.IsValid()) {
    return;
  }

  DWORD ioctlSize = kSrpHeaderSize + ntPath.LengthInBytes();
  auto buf = MakeUnique<uint8_t[]>(ioctlSize);
  auto* srp = reinterpret_cast<SrpIoctlBuffer*>(buf.get());

  // ULONG_PTR is pointer-sized (4 bytes on x86, 8 on x64). Casting to uint64_t
  // zero-extends on x86, matching the cdq zero-extension in x86 ntdll.
  srp->handle =
      static_cast<uint64_t>(reinterpret_cast<ULONG_PTR>(fileHandle.get()));
  srp->pathBytes = ntPath.LengthInBytes();
  if (!ntPath.CopyTo(
          mozilla::Span(srp->path, ntPath.LengthInBytes() / sizeof(WCHAR)))) {
    MOZ_DIAGNOSTIC_ASSERT(false, "CopyTo failed: buffer too small");
    return;
  }

  UniquePtr<HANDLE, CloseHandleDeleter> srpDevice(
      CreateFileW(kSrpDevicePath, FILE_READ_DATA,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, 0, nullptr));
  if (srpDevice.get() == INVALID_HANDLE_VALUE) {
    GMP_LOG_WARNING("EnsureAppLockerCacheIsWarm: opening SrpDevice failed ({})",
                    GetLastError());
    return;
  }

  DWORD outBuf = 0;
  DWORD bytesReturned = 0;
  if (!DeviceIoControl(srpDevice.get(), IOCTL_SRP_VERIFY_DLL, srp, ioctlSize,
                       &outBuf, sizeof(outBuf), &bytesReturned, nullptr)) {
    GMP_LOG_DEBUG(
        "EnsureAppLockerCacheIsWarm: DeviceIoControl failed ({}), "
        "AppLocker may not be enabled",
        GetLastError());
  }
}
#endif

bool GMPLoader::Load(const char* aUTF8LibPath, uint32_t aUTF8LibPathLen,
                     const GMPPlatformAPI* aPlatformAPI, GMPAdapter* aAdapter) {
  CrashReporter::AutoRecordAnnotation autoLibPath(
      CrashReporter::Annotation::GMPLibraryPath,
      nsDependentCString(aUTF8LibPath));

  // Load the GMP.
  PRLibSpec libSpec;
#ifdef XP_WIN
  int pathLen = MultiByteToWideChar(CP_UTF8, 0, aUTF8LibPath, -1, nullptr, 0);
  if (pathLen == 0) {
    MOZ_CRASH("Cannot get path length as wide char!");
    return false;
  }

  auto widePath = MakeUnique<wchar_t[]>(pathLen);
  if (MultiByteToWideChar(CP_UTF8, 0, aUTF8LibPath, -1, widePath.get(),
                          pathLen) == 0) {
    MOZ_CRASH("Cannot convert path to wide char!");
    return false;
  }

#  if defined(MOZ_SANDBOX)
  if (IsAppLockerPolicyPresent()) {
    EnsureAppLockerCacheIsWarm(widePath.get());
  }
#  endif
#endif

  if (!getenv("MOZ_DISABLE_GMP_SANDBOX") && mSandboxStarter &&
      !mSandboxStarter->Start(aUTF8LibPath)) {
    MOZ_CRASH("Cannot start sandbox!");
    return false;
  }

#ifdef XP_WIN
  libSpec.value.pathname_u = widePath.get();
  libSpec.type = PR_LibSpec_PathnameU;
#else
  libSpec.value.pathname = aUTF8LibPath;
  libSpec.type = PR_LibSpec_Pathname;
#endif
  PRLibrary* lib = PR_LoadLibraryWithFlags(libSpec, 0);
  if (!lib) {
#ifdef XP_MACOSX
    lib = LoadCDMWithLegacySupport(libSpec, aUTF8LibPath);
#endif
  }
  if (!lib) {
    MOZ_CRASH_UNSAFE_PRINTF("Cannot load plugin as library %d %d",
                            PR_GetError(), PR_GetOSError());
    return false;
  }

  mAdapter.reset((!aAdapter) ? new PassThroughGMPAdapter() : aAdapter);
  mAdapter->SetAdaptee(lib);

  if (mAdapter->GMPInit(aPlatformAPI) != GMPNoErr) {
    MOZ_CRASH("Cannot initialize plugin adapter!");
    return false;
  }

  return true;
}

GMPErr GMPLoader::GetAPI(const char* aAPIName, void* aHostAPI,
                         void** aPluginAPI, const nsACString& aKeySystem) {
  return mAdapter->GMPGetAPI(aAPIName, aHostAPI, aPluginAPI, aKeySystem);
}

void GMPLoader::Shutdown() {
  if (mAdapter) {
    mAdapter->GMPShutdown();
  }
}

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
class WinSandboxStarter : public mozilla::gmp::SandboxStarter {
 public:
  bool Start(const char* aLibPath) override {
    // Cause advapi32 to load before the sandbox is turned on, as
    // Widevine version 970 and later require it and the sandbox
    // blocks it on Win7.
    unsigned int dummy_rand;
    rand_s(&dummy_rand);

    mozilla::SandboxTarget::Instance()->StartSandbox();
    return true;
  }
};
#endif

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
namespace {
class LinuxSandboxStarter : public mozilla::gmp::SandboxStarter {
 private:
  LinuxSandboxStarter() = default;
  friend std::unique_ptr<LinuxSandboxStarter>
  std::make_unique<LinuxSandboxStarter>();

 public:
  static UniquePtr<SandboxStarter> Make() {
    if (mozilla::SandboxInfo::Get().CanSandboxMedia()) {
      return MakeUnique<LinuxSandboxStarter>();
    }
    // Sandboxing isn't possible, but the parent has already
    // checked that this plugin doesn't require it.  (Bug 1074561)
    return nullptr;
  }
  bool Start(const char* aLibPath) override {
    RegisterProfilerObserversForSandboxProfiler();
    mozilla::SetMediaPluginSandbox(aLibPath);
    return true;
  }
};
}  // anonymous namespace
#endif  // XP_LINUX && MOZ_SANDBOX

static UniquePtr<SandboxStarter> MakeSandboxStarter() {
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
  return mozilla::MakeUnique<WinSandboxStarter>();
#elif defined(XP_LINUX) && defined(MOZ_SANDBOX)
  return LinuxSandboxStarter::Make();
#else
  return nullptr;
#endif
}

GMPLoader::GMPLoader() : mSandboxStarter(MakeSandboxStarter()) {}

bool GMPLoader::CanSandbox() const { return !!mSandboxStarter; }

}  // namespace mozilla::gmp
