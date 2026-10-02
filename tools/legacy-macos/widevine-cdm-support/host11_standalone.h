/* Standalone copy of the cdm::Host_10/cdm::Host_11 interfaces used by
 * host11.cpp. Mirrors dom/media/gmp/widevine-adapter/
 * content_decryption_module.h (the interface the shipped CDM is built
 * against): Host_11 is a standalone class repeating the Host_10 methods plus
 * ReportMetrics, not a subclass. */
#ifndef WIDEVINE_CDM_SUPPORT_HOST11_STANDALONE_H_
#define WIDEVINE_CDM_SUPPORT_HOST11_STANDALONE_H_

#include <stdint.h>

namespace cdm {

typedef double Time;

enum Status : uint32_t {
  kSuccess,
  kNeedMoreData,
  kNoKey,
  kInitializationError,
  kDecryptError,
  kDecodeError,
  kDeferredInitialization
};

enum Exception : uint32_t {
  kExceptionTypeError,
  kExceptionNotSupportedError,
  kExceptionInvalidStateError,
  kExceptionQuotaExceededError
};

enum MessageType : uint32_t {
  kLicenseRequest = 0,
  kLicenseRenewal = 1,
  kLicenseRelease = 2,
  kIndividualizationRequest = 3
};

enum KeyStatus : uint32_t {
  kUsable = 0,
  kInternalError = 1,
  kExpired = 2,
  kOutputRestricted = 3,
  kOutputDownscaled = 4,
  kStatusPending = 5,
  kReleased = 6
};

enum StreamType : uint32_t { kStreamTypeAudio = 0, kStreamTypeVideo = 1 };

enum MetricName : uint32_t {
  kSdkVersion,
  kCertificateSerialNumber,
  kDecoderBypassBlockCount,
  kDecoderCheck1SuccessCount,
  kDecoderCheck1WarningCount,
  kDecoderCheck1ErrorCount,
  kKeySystemDataTime1,
  kKeySystemDataTime2,
  kKeySystemDataTime3,
  kKeySystemDataBool1,
};

class Buffer {
 public:
  virtual void Destroy() = 0;
  virtual uint32_t Capacity() const = 0;
  virtual uint8_t* Data() = 0;
  virtual void SetSize(uint32_t size) = 0;
  virtual uint32_t Size() const = 0;

 protected:
  Buffer() {}
  virtual ~Buffer() {}

 private:
  Buffer(const Buffer&);
  void operator=(const Buffer&);
};

class FileIO {
 public:
  virtual void Open(const char* file_name, uint32_t file_name_size) = 0;
  virtual void Read() = 0;
  virtual void Write(const uint8_t* data, uint32_t data_size) = 0;
  virtual void Close() = 0;

 protected:
  FileIO() {}
  virtual ~FileIO() {}
};

class FileIOClient {
 public:
  enum class Status : uint32_t { kSuccess = 0, kInUse, kError };
  virtual void OnOpenComplete(Status status) = 0;
  virtual void OnReadComplete(Status status, const uint8_t* data,
                              uint32_t data_size) = 0;
  virtual void OnWriteComplete(Status status) = 0;

 protected:
  FileIOClient() {}
  virtual ~FileIOClient() {}
};

struct KeyInformation {
  const uint8_t* key_id;
  uint32_t key_id_size;
  KeyStatus status;
  uint32_t system_code;
};

class Host_10 {
 public:
  static const int kVersion = 10;
  virtual Buffer* Allocate(uint32_t capacity) = 0;
  virtual void SetTimer(int64_t delay_ms, void* context) = 0;
  virtual Time GetCurrentWallTime() = 0;
  virtual void OnInitialized(bool success) = 0;
  virtual void OnResolveKeyStatusPromise(uint32_t promise_id,
                                         KeyStatus key_status) = 0;
  virtual void OnResolveNewSessionPromise(uint32_t promise_id,
                                          const char* session_id,
                                          uint32_t session_id_size) = 0;
  virtual void OnResolvePromise(uint32_t promise_id) = 0;
  virtual void OnRejectPromise(uint32_t promise_id, Exception exception,
                               uint32_t system_code,
                               const char* error_message,
                               uint32_t error_message_size) = 0;
  virtual void OnSessionMessage(const char* session_id,
                                uint32_t session_id_size,
                                MessageType message_type, const char* message,
                                uint32_t message_size) = 0;
  virtual void OnSessionKeysChange(const char* session_id,
                                   uint32_t session_id_size,
                                   bool has_additional_usable_key,
                                   const KeyInformation* keys_info,
                                   uint32_t keys_info_count) = 0;
  virtual void OnExpirationChange(const char* session_id,
                                  uint32_t session_id_size,
                                  Time new_expiry_time) = 0;
  virtual void OnSessionClosed(const char* session_id,
                               uint32_t session_id_size) = 0;
  virtual void SendPlatformChallenge(const char* service_id,
                                     uint32_t service_id_size,
                                     const char* challenge,
                                     uint32_t challenge_size) = 0;
  virtual void EnableOutputProtection(
      uint32_t desired_protection_mask) = 0;
  virtual void QueryOutputProtectionStatus() = 0;
  virtual void OnDeferredInitializationDone(StreamType stream_type,
                                            Status decoder_status) = 0;
  virtual FileIO* CreateFileIO(FileIOClient* client) = 0;
  virtual void RequestStorageId(uint32_t version) = 0;

 protected:
  Host_10() {}
  virtual ~Host_10() {}
};

class Host_11 {
 public:
  static const int kVersion = 11;
  virtual Buffer* Allocate(uint32_t capacity) = 0;
  virtual void SetTimer(int64_t delay_ms, void* context) = 0;
  virtual Time GetCurrentWallTime() = 0;
  virtual void OnInitialized(bool success) = 0;
  virtual void OnResolveKeyStatusPromise(uint32_t promise_id,
                                         KeyStatus key_status) = 0;
  virtual void OnResolveNewSessionPromise(uint32_t promise_id,
                                          const char* session_id,
                                          uint32_t session_id_size) = 0;
  virtual void OnResolvePromise(uint32_t promise_id) = 0;
  virtual void OnRejectPromise(uint32_t promise_id, Exception exception,
                               uint32_t system_code,
                               const char* error_message,
                               uint32_t error_message_size) = 0;
  virtual void OnSessionMessage(const char* session_id,
                                uint32_t session_id_size,
                                MessageType message_type, const char* message,
                                uint32_t message_size) = 0;
  virtual void OnSessionKeysChange(const char* session_id,
                                   uint32_t session_id_size,
                                   bool has_additional_usable_key,
                                   const KeyInformation* keys_info,
                                   uint32_t keys_info_count) = 0;
  virtual void OnExpirationChange(const char* session_id,
                                  uint32_t session_id_size,
                                  Time new_expiry_time) = 0;
  virtual void OnSessionClosed(const char* session_id,
                               uint32_t session_id_size) = 0;
  virtual void SendPlatformChallenge(const char* service_id,
                                     uint32_t service_id_size,
                                     const char* challenge,
                                     uint32_t challenge_size) = 0;
  virtual void EnableOutputProtection(
      uint32_t desired_protection_mask) = 0;
  virtual void QueryOutputProtectionStatus() = 0;
  virtual void OnDeferredInitializationDone(StreamType stream_type,
                                            Status decoder_status) = 0;
  virtual FileIO* CreateFileIO(FileIOClient* client) = 0;
  virtual void RequestStorageId(uint32_t version) = 0;
  virtual void ReportMetrics(MetricName metric_name, uint64_t value) = 0;

 protected:
  Host_11() {}
  virtual ~Host_11() {}
};

}  // namespace cdm

#endif
