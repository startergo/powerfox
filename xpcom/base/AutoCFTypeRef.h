/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_AutoCFTypeRef_h
#define mozilla_AutoCFTypeRef_h

#include <CoreFoundation/CFBase.h>
#include <CoreVideo/CVBuffer.h>  // For CVBufferRetain(), CVBufferRelease()

#include "mozilla/Assertions.h"

namespace mozilla {

template <typename T>
struct AutoTypeRefTraits;

enum class AutoTypePolicy { Retain, NoRetain };
template <typename T, typename Traits = AutoTypeRefTraits<T>>
class AutoTypeRef {
 public:
  explicit AutoTypeRef(T aObj = Traits::InvalidValue(),
                       AutoTypePolicy aPolicy = AutoTypePolicy::NoRetain)
      : mObj(aObj) {
    if (mObj != Traits::InvalidValue()) {
      if (aPolicy == AutoTypePolicy::Retain) {
        mObj = Traits::Retain(mObj);
      }
    }
  }

  ~AutoTypeRef() { ReleaseIfNeeded(); }

  // Copy constructor
  AutoTypeRef(const AutoTypeRef<T, Traits>& aOther) : mObj(aOther.mObj) {
    if (mObj != Traits::InvalidValue()) {
      mObj = Traits::Retain(mObj);
    }
  }

  // Copy assignment
  AutoTypeRef<T, Traits>& operator=(const AutoTypeRef<T, Traits>& aOther) {
    if (this != &aOther) {
      ReleaseIfNeeded();
      mObj = aOther.mObj;
      if (mObj != Traits::InvalidValue()) {
        mObj = Traits::Retain(mObj);
      }
    }
    return *this;
  }

  // Move constructor
  AutoTypeRef(AutoTypeRef<T, Traits>&& aOther) : mObj(aOther.Take()) {}

  // Move assignment
  AutoTypeRef<T, Traits>& operator=(AutoTypeRef<T, Traits>&& aOther) {
    Reset(aOther.Take(), AutoTypePolicy::NoRetain);
    return *this;
  }

  explicit operator bool() const { return mObj != Traits::InvalidValue(); }

  operator T() { return mObj; }

  T& Ref() { return mObj; }

  T* Receive() {
    MOZ_ASSERT(mObj == Traits::InvalidValue(),
               "Receive() should only be called for uninitialized objects");
    return &mObj;
  }

  void Reset(T aObj = Traits::InvalidValue(),
             AutoTypePolicy aPolicy = AutoTypePolicy::NoRetain) {
    ReleaseIfNeeded();
    mObj = aObj;
    if (mObj != Traits::InvalidValue()) {
      if (aPolicy == AutoTypePolicy::Retain) {
        mObj = Traits::Retain(mObj);
      } else {
        mObj = aObj;
      }
    }
  }

 private:
  T Take() {
    T obj = mObj;
    mObj = Traits::InvalidValue();
    return obj;
  }

  void ReleaseIfNeeded() {
    if (mObj != Traits::InvalidValue()) {
      Traits::Release(mObj);
      mObj = Traits::InvalidValue();
    }
  }
  T mObj;
};

template <typename CFT>
struct CFTypeRefTraits {
  static CFT InvalidValue() { return nullptr; }
  static CFT Retain(CFT aObject) {
    CFRetain(aObject);
    return aObject;
  }
  static void Release(CFT aObject) { CFRelease(aObject); }
};

template <typename CVB>
struct CVBufferRefTraits {
  static CVB InvalidValue() { return nullptr; }
  static CVB Retain(CVB aObject) {
    CVBufferRetain(aObject);
    return aObject;
  }
  static void Release(CVB aObject) { CVBufferRelease(aObject); }
};

template <typename CFT>
using AutoCFTypeRef = AutoTypeRef<CFT, CFTypeRefTraits<CFT>>;
template <typename CVB>
using AutoCVBufferRef = AutoTypeRef<CVB, CVBufferRefTraits<CVB>>;

}  // namespace mozilla

#endif  // mozilla_AutoCFTypeRef_h
