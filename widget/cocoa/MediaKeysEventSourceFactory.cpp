/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaKeysEventSourceFactory.h"

#include "MediaHardwareKeysEventSourceMac.h"
// Match the moz.build gate for MediaHardwareKeysEventSourceMacMediaCenter.mm
// (deployment target >= 10.12.2), not the SDK's MAX_ALLOWED: with old-SDK
// builds the implementation is not compiled at all.
#if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) && \
    __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101202 && \
    (!defined(MAC_OS_X_VERSION_10_12_2) || \
     MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_12_2)
#  include "MediaHardwareKeysEventSourceMacMediaCenter.h"
#  define HAVE_MEDIACENTER 1
#endif
#include "nsCocoaFeatures.h"

namespace mozilla {
namespace widget {

mozilla::dom::MediaControlKeySource* CreateMediaControlKeySource() {
#if defined(HAVE_MEDIACENTER)
  if (nsCocoaFeatures::IsAtLeastVersion(10, 12, 2)) {
    return new MediaHardwareKeysEventSourceMacMediaCenter();
  }
#endif
  return new MediaHardwareKeysEventSourceMac();
}

}  // namespace widget
}  // namespace mozilla
