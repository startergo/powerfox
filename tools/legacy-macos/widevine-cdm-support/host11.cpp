/* Real C++ Host_11 for the harness: derives the tree's own interface so the
 * vtable is genuine (preamble + full slot coverage). Every method logs its
 * slot and returns a type-plausible value. Build alongside harness.c:
 * cc -x86_64 -c host11.cpp && link both into /tmp/harness. */
#include "/tmp/host11_standalone.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LOG() fprintf(stderr, "H: HOST %s\n", __func__)

namespace cdm {

class HarnessHost : public Host_11 {
 public:
  Buffer* Allocate(uint32_t capacity) {
    LOG();
    struct Buf : Buffer {
      uint8_t* data; uint32_t cap;
      void Destroy() { free(data); delete this; }
      uint32_t Capacity() const { return cap; }
      uint8_t* Data() { return data; }
      void SetSize(uint32_t s) { size_ = s; }
      uint32_t Size() const { return size_; }
      uint32_t size_;
    };
    Buf* b = new Buf; b->size_ = 0;
    b->data = (uint8_t*)calloc(1, capacity ? capacity : 1);
    b->cap = capacity;
    return b;
  }
  void SetTimer(int64_t delay, void* ctx) { LOG(); }
  Time GetCurrentWallTime() {
    fprintf(stderr, "H: HOST GetCurrentWallTime\n");
    return (Time)time(0);
  }
  void OnInitialized(bool success) { LOG(); }
  void OnResolveKeyStatusPromise(uint32_t id, KeyStatus s) { LOG(); }
  void OnResolveNewSessionPromise(uint32_t id, const char* sid, uint32_t n) { LOG(); }
  void OnResolvePromise(uint32_t id) { LOG(); }
  void OnRejectPromise(uint32_t id, Exception e, uint32_t sys, const char* m, uint32_t n) { LOG(); }
  void OnSessionMessage(const char* sid, uint32_t n, MessageType t, const char* m, uint32_t mn) {
    fprintf(stderr, "H: HOST OnSessionMessage type=%d len=%u\n", (int)t, mn);
  }
  void OnSessionKeysChange(const char* sid, uint32_t n, bool has, const KeyInformation* keys, uint32_t kn) { LOG(); }
  void OnExpirationChange(const char* sid, uint32_t n, Time t) { LOG(); }
  void OnSessionClosed(const char* sid, uint32_t n) { LOG(); }
  void SendPlatformChallenge(const char* s, uint32_t sn, const char* sid, uint32_t idn) { LOG(); }
  void EnableOutputProtection(uint32_t m) { LOG(); }
  void QueryOutputProtectionStatus() { LOG(); }
  void OnDeferredInitializationDone(StreamType t, Status s) { LOG(); }
  FileIO* CreateFileIO(FileIOClient* c) { LOG(); return 0; }
  void RequestStorageId(uint32_t v) { LOG(); }
  std::vector<VideoDecoderFunction> RequestVideoDecoderFunctions() {
    LOG();
    return std::vector<VideoDecoderFunction>();
  }
  void SetMiniumOutputProtectionCallback(OutputProtectionCallback cb) { LOG(); }
  void RequestPlatformVerification(const char* p, uint32_t pn, const char* ch, uint32_t cn, const char* sid, uint32_t sn) { LOG(); }
  void ReportMetrics(MetricName n, double v) { LOG(); }
  void GetStorageId(uint32_t v, const uint8_t** d, uint32_t* n) { LOG(); *d = 0; *n = 0; }
};

}  // namespace cdm

extern "C" void* harness_get_host(int version) {
  fprintf(stderr, "H: host_provider(%d) [C++]\n", version);
  static cdm::HarnessHost host;
  return version >= 11 ? (void*)static_cast<cdm::Host_11*>(&host)
                       : (void*)static_cast<cdm::Host_10*>(&host);
}
