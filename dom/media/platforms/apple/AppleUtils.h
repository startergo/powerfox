/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Utility functions to help with Apple API calls.

#ifndef mozilla_AppleUtils_h
#define mozilla_AppleUtils_h

#include <CoreFoundation/CFBase.h>      // For CFRelease()
#include <CoreVideo/CVBuffer.h>         // For CVBufferRelease()
#if __has_include(<VideoToolbox/VideoToolbox.h>)
#  include <VideoToolbox/VideoToolbox.h>  // For VTCompressionSessionRef
#  define MOZ_APPLEUTILS_HAVE_VT 1
#endif

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/AutoCFTypeRef.h"

#if TARGET_OS_IPHONE
inline bool OSSupportsSVC() {
  // TODO
  return false;
}
#else
#  include "nsCocoaFeatures.h"
inline bool OSSupportsSVC() {
  return nsCocoaFeatures::IsAtLeastVersion(11, 3, 0);
}
#endif

namespace mozilla {

#ifdef MOZ_APPLEUTILS_HAVE_VT
class MOZ_RAII SessionPropertyManager {
 public:
  explicit SessionPropertyManager(
      const AutoCFTypeRef<VTCompressionSessionRef>& aSession);
  explicit SessionPropertyManager(const VTCompressionSessionRef& aSession);
  ~SessionPropertyManager() = default;

  bool IsSupported(CFStringRef aKey);

  OSStatus Set(CFStringRef aKey, int32_t aValue);
  OSStatus Set(CFStringRef aKey, int64_t aValue);
  OSStatus Set(CFStringRef aKey, float aValue);
  OSStatus Set(CFStringRef aKey, bool aValue);
  OSStatus Set(CFStringRef aKey, CFStringRef value);
  OSStatus Copy(CFStringRef aKey, bool& aValue);

 private:
  template <typename V>
  OSStatus Set(CFStringRef aKey, V aValue, CFNumberType aType);

  AutoCFTypeRef<VTCompressionSessionRef> mSession;
  AutoCFTypeRef<CFDictionaryRef> mSupportedKeys;
};
#endif  // MOZ_APPLEUTILS_HAVE_VT

}  // namespace mozilla

#endif  // mozilla_AppleUtils_h
