/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_gfx_layers_AVFoundationCompat_h
#define mozilla_gfx_layers_AVFoundationCompat_h

// Declarations for AVFoundation / CoreMedia APIs that the pre-Lion SDK does
// not have. Everything is annotated 10.8 so the references weak-link and
// resolve to nil / NULL at runtime on older systems; the callers already
// handle nil layers and failed sample-buffer creation.
#if !defined(MAC_OS_X_VERSION_10_8) || \
    MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_8

#import <QuartzCore/QuartzCore.h>

#include <CoreFoundation/CoreFoundation.h>

#ifndef NS_AVAILABLE_MAC
#define NS_AVAILABLE_MAC(m) __attribute__((availability(macos, introduced=m)))
#endif
#ifndef NS_CLASS_AVAILABLE_MAC
#define NS_CLASS_AVAILABLE_MAC(m) NS_AVAILABLE_MAC(m)
#endif

typedef struct CMTimebase* CMTimebaseRef;
typedef struct CMTimebase* CMClockRef;
typedef struct CMFormatDescription* CMVideoFormatDescriptionRef;
typedef struct CMSampleBuffer* CMSampleBufferRef;

extern const CFStringRef kCMSampleAttachmentKey_DisplayImmediately
    __attribute__((weak_import));

typedef struct {
  int64_t value;
  int32_t timescale;
  uint32_t flags;
  uint32_t epoch;
} CMTime;

typedef struct {
  int64_t duration;
  CMTime presentationTimeStamp;
  CMTime decodeTimeStamp;
} CMSampleTimingInfo;

enum {
  AVQueuedSampleBufferRenderingStatusRendering = 2,
};

#define kCMTimingInfoInvalid \
  { 0, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } }

typedef unsigned long long CACornerMask;

enum : CACornerMask {
  kCALayerMinXMinYCorner = 1U << 0,
  kCALayerMaxXMinYCorner = 1U << 1,
  kCALayerMinXMaxYCorner = 1U << 2,
  kCALayerMaxXMaxYCorner = 1U << 3,
};

@interface CALayer (PowerFoxPreLion)
@property CGFloat contentsScale;
@property BOOL preventsCapture;
@property CACornerMask maskedCorners;
@end

NS_CLASS_AVAILABLE_MAC(10_8)
@interface AVSampleBufferDisplayLayer : CALayer
@property(readonly) NSInteger status;
@property BOOL requiresFlushToResumeDecoding;
@property BOOL preventsCapture;
@property(readonly, getter=isReadyForMoreMediaData) BOOL readyForMoreMediaData;
@property(assign) CMTimebaseRef controlTimebase;
- (void)flush;
- (void)enqueueSampleBuffer:(CMSampleBufferRef)sampleBuffer;
@end

extern "C" {
CFDictionaryRef IOSurfaceCopyAllValues(void* surface)
    __attribute__((availability(macos, introduced = 10.8)));
CMClockRef CMClockGetHostTimeClock(void)
    __attribute__((availability(macos, introduced = 10.8)));
int CMTimebaseCreateWithMasterClock(CFAllocatorRef allocator,
                                     CMClockRef masterClock,
                                     CMTimebaseRef* timebaseOut)
    __attribute__((availability(macos, introduced = 10.8)));
int CMTimebaseCreateWithSourceClock(CFAllocatorRef allocator,
                                     CMClockRef sourceClock,
                                     CMTimebaseRef* timebaseOut)
    __attribute__((availability(macos, introduced = 10.8)));
CMTime CMTimebaseGetTime(CMTimebaseRef timebase)
    __attribute__((availability(macos, introduced = 10.8)));
void CMTimebaseSetRate(CMTimebaseRef timebase, double rate)
    __attribute__((availability(macos, introduced = 10.8)));
OSStatus CMVideoFormatDescriptionCreateForImageBuffer(
    CFAllocatorRef allocator, CVImageBufferRef imageBuffer,
    CMVideoFormatDescriptionRef* formatDescriptionOut)
    __attribute__((availability(macos, introduced = 10.8)));
FourCharCode CMFormatDescriptionGetMediaSubType(
    CMVideoFormatDescriptionRef formatDescription)
    __attribute__((availability(macos, introduced = 10.8)));
CFDictionaryRef CMFormatDescriptionGetExtensions(
    CMVideoFormatDescriptionRef formatDescription)
    __attribute__((availability(macos, introduced = 10.8)));
OSStatus CMSampleBufferCreateReadyWithImageBuffer(
    CFAllocatorRef allocator, CVImageBufferRef imageBuffer,
    CMVideoFormatDescriptionRef formatDescription,
    const CMSampleTimingInfo* sampleTimingInfo,
    CMSampleBufferRef* sampleBufferOut)
    __attribute__((availability(macos, introduced = 10.8)));
CFArrayRef CMSampleBufferGetSampleAttachmentsArray(
    CMSampleBufferRef sampleBuffer, Boolean createIfNecessary)
    __attribute__((availability(macos, introduced = 10.8)));
}

#endif

#endif  // mozilla_gfx_layers_AVFoundationCompat_h
