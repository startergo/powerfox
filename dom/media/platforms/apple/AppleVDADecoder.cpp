/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AppleVDADecoder.h"

#include "AOMDecoder.h"
#include "AppleDecoderModule.h"
#include "AppleVDALinker.h"
#include "AppleUtils.h"
#include "CallbackThreadRegistry.h"
#include "MediaData.h"
#include "MediaInfo.h"
#include "MP4Decoder.h"
#include "PlatformDecoderModule.h"
#include "VideoUtils.h"
#include "gfxMacUtils.h"
#include "gfxPlatform.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/Logging.h"
#include "mozilla/SyncRunnable.h"
#include "nsThreadUtils.h"

#include <algorithm>

#ifndef MOZ_WIDGET_UIKIT
#include "MacIOSurfaceImage.h"
#endif

#if defined(XP_MACOSX) && defined(MOZ_LEGACY_MACOS_TARGET)
#  include "libyuv/convert_argb.h"
#  include "mozilla/gfx/MacIOSurface.h"
#endif

#define LOG(...) MOZ_LOG(sPDMLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#define LOGEX(_this, ...) \
  DDMOZ_LOGEX(_this, sPDMLog, mozilla::LogLevel::Debug, __VA_ARGS__)

//#define LOG_MEDIA_SHA1

namespace mozilla {

AppleVDADecoder::AppleVDADecoder(const VideoInfo& aConfig,
                               layers::ImageContainer* aImageContainer,
                               const CreateDecoderParams::OptionSet& aOptions,
                               layers::KnowsCompositor* aKnowsCompositor,
                               Maybe<TrackingId> aTrackingId)
    : mExtraData(aConfig.mExtraData),
      mPictureWidth(aConfig.mImage.width),
      mPictureHeight(aConfig.mImage.height),
      mDisplayWidth(aConfig.mDisplay.width),
      mDisplayHeight(aConfig.mDisplay.height),
      mColorSpace(aConfig.mColorSpace
                      ? *aConfig.mColorSpace
                      : DefaultColorSpace({mPictureWidth, mPictureHeight})),
      mColorPrimaries(aConfig.mColorPrimaries ? *aConfig.mColorPrimaries
                                              : gfx::ColorSpace2::BT709),
      mTransferFunction(aConfig.mTransferFunction
                            ? *aConfig.mTransferFunction
                            : gfx::TransferFunction::BT709),
      mColorRange(aConfig.mColorRange),
      mColorDepth(aConfig.mColorDepth),
      mStreamType(MP4Decoder::IsH264(aConfig.mMimeType)  ? StreamType::H264
                                                         : StreamType::Unknown),
      mTaskQueue(TaskQueue::Create(
          GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER),
          "AppleVDADecoder")),
      mDecoder(nullptr),
      mMaxRefFrames(
          mStreamType != StreamType::H264 ||
                  aOptions.contains(CreateDecoderParams::Option::LowLatency)
              ? 0
              : H264::ComputeMaxRefFrames(aConfig.mExtraData)),
      mImageContainer(aImageContainer),
      mKnowsCompositor(aKnowsCompositor)
#ifdef MOZ_WIDGET_UIKIT
      ,
      mUseSoftwareImages(true)
#else
      ,
      mUseSoftwareImages(aKnowsCompositor &&
                         aKnowsCompositor->GetWebRenderCompositorType() ==
                             layers::WebRenderCompositor::SOFTWARE)
#endif
      ,
      mTrackingId(aTrackingId),
      mIsFlushing(false),
      mCallbackThreadId(),
      mMonitor("AppleVDADecoder"),
      mPromise(&mMonitor),  // To ensure our PromiseHolder is only ever accessed
                            // with the monitor held.
      mFormat(nullptr),
      mSession(nullptr),
      mIsHardwareAccelerated(false) {
      MOZ_COUNT_CTOR(AppleVDADecoder);
      MOZ_ASSERT(mStreamType != StreamType::Unknown);
      // TODO: Verify aConfig.mime_type.
      LOG("Creating AppleVDADecoder for %dx%d %s video", mDisplayWidth,
      mDisplayHeight, EnumValueToString(mStreamType));
}

AppleVDADecoder::~AppleVDADecoder()
{
  MOZ_COUNT_DTOR(AppleVDADecoder);
}

RefPtr<MediaDataDecoder::InitPromise>
AppleVDADecoder::Init()
{
  MediaResult rv = NS_OK;
  if (!mDecoder) {
    // The module may have probed the session at creation time already.
    rv = InitializeSession();
  }

  if (NS_SUCCEEDED(rv)) {
    return InitPromise::CreateAndResolve(TrackType::kVideoTrack, __func__);
  }
  return InitPromise::CreateAndReject(rv, __func__);
}

RefPtr<MediaDataDecoder::FlushPromise> AppleVDADecoder::Flush() {
  mIsFlushing = true;
  return InvokeAsync(mTaskQueue, this, __func__, &AppleVDADecoder::ProcessFlush);
}

RefPtr<MediaDataDecoder::DecodePromise> AppleVDADecoder::Drain() {
  return InvokeAsync(mTaskQueue, this, __func__, &AppleVDADecoder::ProcessDrain);
}

RefPtr<ShutdownPromise>
AppleVDADecoder::Shutdown()
{
  RefPtr<AppleVDADecoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self]() {
    self->ProcessShutdown();
    return self->mTaskQueue->BeginShutdown();
  });
}

void
AppleVDADecoder::ProcessShutdown()
{
  if (mDecoder) {
    LOG("%s: cleaning up decoder %p", __func__, mDecoder);
    VDADecoderDestroy(mDecoder);
    mDecoder = nullptr;
  }
}

RefPtr<MediaDataDecoder::FlushPromise> AppleVDADecoder::ProcessFlush() {
  AssertOnTaskQueue();
  OSStatus rv = VDADecoderFlush(mDecoder, 0 /*dont emit*/);
  if (rv != noErr) {
    LOG("AppleVDADecoder::Flush failed waiting for platform decoder "
        "with error:%d.", rv);
  }
  MonitorAutoLock mon(mMonitor);
  mPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  while (!mReorderQueue.IsEmpty()) {
    mReorderQueue.Pop();
  }
  mIsFlushing = false;
  return FlushPromise::CreateAndResolve(true, __func__);
}

RefPtr<MediaDataDecoder::DecodePromise> AppleVDADecoder::ProcessDrain() {
  AssertOnTaskQueue();
  MonitorAutoLock mon(mMonitor);
  OSStatus rv = VDADecoderFlush(mDecoder, kVDADecoderFlush_EmitFrames);
  if (rv != noErr) {
    LOG("AppleVDADecoder::Drain failed waiting for platform decoder "
        "with error:%d.", rv);
  }

  DecodedData samples;
  while (!mReorderQueue.IsEmpty()) {
    samples.AppendElement(mReorderQueue.Pop());
  }
  return DecodePromise::CreateAndResolve(std::move(samples), __func__);
}

void AppleVDADecoder::SetSeekThreshold(const media::TimeUnit& aTime) {
  if (aTime.IsValid()) {
    mSeekTargetThreshold = Some(aTime);
  } else {
    mSeekTargetThreshold.reset();
  }
}

//
// Implementation details.
//

// Callback passed to the VideoToolbox decoder for returning data.
// This needs to be static because the API takes a C-style pair of
// function and userdata pointers. This validates parameters and
// forwards the decoded image back to an object method.
// Marks the frame-info dictionary as carrying the compensation reference
// ProcessDecode added for asynchronous delivery; PlatformCallback removes
// it when it consumes the reference, so the error path can tell whether
// the callback for this very frame already ran — a callback for an earlier
// frame cannot interfere.
static CFStringRef FrameInfoCompensationKey() {
  return CFSTR("FRAME_INFO_COMPENSATION");
}

static void
PlatformCallback(void* decompressionOutputRefCon,
                 CFDictionaryRef frameInfo,
                 OSStatus status,
                 VDADecodeInfoFlags infoFlags,
                 CVImageBufferRef image)
{
  AppleVDADecoder* decoder =
    static_cast<AppleVDADecoder*>(decompressionOutputRefCon);

  // The callback can fire synchronously on the error path, before
  // ProcessDecode's dictionary is set up.
  if (!frameInfo) {
    if (status != noErr) {
      decoder->OnDecodeError(status);
    }
    return;
  }

  LOG("AppleVDADecoder[%s] status %d flags %d retainCount %ld",
      __func__, status, infoFlags, CFGetRetainCount(frameInfo));

  // Validate our arguments.
  // According to Apple's TN2267
  // The output callback is still called for all flushed frames,
  // but no image buffers will be returned.
  // FIXME: Distinguish between errors and empty flushed frames.
  if (status != noErr || !image) {
    NS_WARNING("AppleVDADecoder decoder returned no data");
    image = nullptr;
  } else if (infoFlags & kVDADecodeInfo_FrameDropped) {
    NS_WARNING("  ...frame dropped...");
    image = nullptr;
  } else {
    MOZ_ASSERT(image || CFGetTypeID(image) == CVPixelBufferGetTypeID(),
               "AppleVDADecoder returned an unexpected image type");
  }

  // Borrowed references from the dictionary; CFDictionaryGetValue does not
  // hand out ownership, so they must not be released.
  CFNumberRef ptsref =
    (CFNumberRef)CFDictionaryGetValue(frameInfo, CFSTR("FRAME_PTS"));
  CFNumberRef dtsref =
    (CFNumberRef)CFDictionaryGetValue(frameInfo, CFSTR("FRAME_DTS"));
  CFNumberRef durref =
    (CFNumberRef)CFDictionaryGetValue(frameInfo, CFSTR("FRAME_DURATION"));
  CFNumberRef boref =
    (CFNumberRef)CFDictionaryGetValue(frameInfo, CFSTR("FRAME_OFFSET"));
  CFNumberRef kfref =
    (CFNumberRef)CFDictionaryGetValue(frameInfo, CFSTR("FRAME_KEYFRAME"));
  if (!ptsref || !dtsref || !durref || !boref || !kfref) {
    NS_WARNING("AppleVDADecoder: incomplete frame info");
    CFDictionaryRemoveValue((CFMutableDictionaryRef)frameInfo,
                            FrameInfoCompensationKey());
    CFRelease(frameInfo);
    return;
  }

  int64_t dts;
  int64_t pts;
  int64_t duration;
  int64_t byte_offset;
  char is_sync_point;

  CFNumberGetValue(ptsref, kCFNumberSInt64Type, &pts);
  CFNumberGetValue(dtsref, kCFNumberSInt64Type, &dts);
  CFNumberGetValue(durref, kCFNumberSInt64Type, &duration);
  CFNumberGetValue(boref, kCFNumberSInt64Type, &byte_offset);
  CFNumberGetValue(kfref, kCFNumberSInt8Type, &is_sync_point);

  // Release the reference ProcessDecode added for the decoder's async
  // delivery (TN2267); removing the key first records, per frame, that
  // the compensation was consumed.
  CFDictionaryRemoveValue((CFMutableDictionaryRef)frameInfo,
                          FrameInfoCompensationKey());
  CFRelease(frameInfo);

  AppleVDADecoder::AppleFrameRef frameRef(
      media::TimeUnit::FromMicroseconds(dts),
      media::TimeUnit::FromMicroseconds(pts),
      media::TimeUnit::FromMicroseconds(duration),
      byte_offset,
      is_sync_point == 1);

  decoder->OutputFrame(image, frameRef);
}

void AppleVDADecoder::MaybeResolveBufferedFrames() {
  mMonitor.AssertCurrentThreadOwns();

  if (mPromise.IsEmpty()) {
    return;
  }

  DecodedData results;
  while (mReorderQueue.Length() > mMaxRefFrames) {
    results.AppendElement(mReorderQueue.Pop());
  }
  mPromise.Resolve(std::move(results), __func__);
}

void AppleVDADecoder::MaybeRegisterCallbackThread() {
  ProfilerThreadId id = profiler_current_thread_id();
  if (MOZ_LIKELY(id == mCallbackThreadId)) {
    return;
  }
  mCallbackThreadId = id;
  CallbackThreadRegistry::Get()->Register(mCallbackThreadId,
                                          "AppleVDADecoderCallback");
}

nsCString AppleVDADecoder::GetCodecName() const {
  return nsCString(EnumValueToString(mStreamType));
}

// Copy and return a decoded frame.
void
AppleVDADecoder::OutputFrame(CVPixelBufferRef aImage,
                             AppleVDADecoder::AppleFrameRef aFrameRef)
{
  MaybeRegisterCallbackThread();

  if (mIsFlushing) {
    // We are in the process of flushing or shutting down; ignore frame.
    return;
  }

  LOG("mp4 output frame %lld dts %lld pts %lld duration %lld us%s",
      aFrameRef.byte_offset, aFrameRef.decode_timestamp.ToMicroseconds(),
      aFrameRef.composition_timestamp.ToMicroseconds(),
      aFrameRef.duration.ToMicroseconds(),
      aFrameRef.is_sync_point ? " keyframe" : "");

  if (!aImage) {
    // Image was dropped by decoder or none return yet.
    // We need more input to continue.
    MonitorAutoLock mon(mMonitor);
    MaybeResolveBufferedFrames();
    return;
  }

  bool useNullSample = false;
  if (mSeekTargetThreshold.isSome()) {
    if ((aFrameRef.composition_timestamp + aFrameRef.duration) <
        mSeekTargetThreshold.ref()) {
      useNullSample = true;
    } else {
      mSeekTargetThreshold.reset();
    }
  }

  // Where our resulting image will end up.
  RefPtr<MediaData> data;
  // Bounds.
  VideoInfo info;
  info.mDisplay = gfx::IntSize(mDisplayWidth, mDisplayHeight);
  info.mTransferFunction = Some(mTransferFunction);

#if defined(XP_MACOSX) && defined(MOZ_LEGACY_MACOS_TARGET)
  // The software-images copy lands in a YUV IOSurface that pre-10.8
  // CoreAnimation cannot display; always convert to BGRA instead.
  const bool useSoftwareImages = false;
#else
  const bool useSoftwareImages = mUseSoftwareImages;
#endif

  if (useNullSample) {
    data = new NullData(aFrameRef.byte_offset, aFrameRef.composition_timestamp,
                      aFrameRef.duration);
  } else if (useSoftwareImages) {
    size_t width = CVPixelBufferGetWidth(aImage);
    size_t height = CVPixelBufferGetHeight(aImage);
    DebugOnly<size_t> planes = CVPixelBufferGetPlaneCount(aImage);
    MOZ_ASSERT(planes == 2, "Likely not NV12 format and it must be.");

    VideoData::YCbCrBuffer buffer;

    // Lock the returned image data.
    CVReturn rv =
        CVPixelBufferLockBaseAddress(aImage, kCVPixelBufferLock_ReadOnly);
    if (rv != kCVReturnSuccess) {
      NS_ERROR("error locking pixel data");
      MonitorAutoLock mon(mMonitor);
      mPromise.Reject(
          MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                      RESULT_DETAIL("CVPixelBufferLockBaseAddress:%x", rv)),
          __func__);
      return;
    }
    // Y plane.
    buffer.mPlanes[0].mData =
        static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(aImage, 0));
    buffer.mPlanes[0].mStride = CVPixelBufferGetBytesPerRowOfPlane(aImage, 0);
    buffer.mPlanes[0].mWidth = width;
    buffer.mPlanes[0].mHeight = height;
    buffer.mPlanes[0].mSkip = 0;
    // Cb plane.
    buffer.mPlanes[1].mData =
        static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(aImage, 1));
    buffer.mPlanes[1].mStride = CVPixelBufferGetBytesPerRowOfPlane(aImage, 1);
    buffer.mPlanes[1].mWidth = (width + 1) / 2;
    buffer.mPlanes[1].mHeight = (height + 1) / 2;
    buffer.mPlanes[1].mSkip = 1;
    // Cr plane.
    buffer.mPlanes[2].mData =
        static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(aImage, 1)) +
        1;
    buffer.mPlanes[2].mStride = CVPixelBufferGetBytesPerRowOfPlane(aImage, 1);
    buffer.mPlanes[2].mWidth = (width + 1) / 2;
    buffer.mPlanes[2].mHeight = (height + 1) / 2;
    buffer.mPlanes[2].mSkip = 1;

    buffer.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
    buffer.mYUVColorSpace = mColorSpace;
    buffer.mColorPrimaries = mColorPrimaries;
    buffer.mColorRange = mColorRange;

    gfx::IntRect visible = gfx::IntRect(0, 0, mPictureWidth, mPictureHeight);

    // Copy the image data into our own format.
    Result<already_AddRefed<VideoData>, MediaResult> result =
        VideoData::CreateAndCopyData(
            info, mImageContainer, aFrameRef.byte_offset,
            aFrameRef.composition_timestamp, aFrameRef.duration, buffer,
            aFrameRef.is_sync_point, aFrameRef.decode_timestamp, visible,
            mKnowsCompositor);
    // TODO: Reject mPromise below with result's error return.
    data = result.unwrapOr(nullptr);
    // Unlock the returned image data.
    CVPixelBufferUnlockBaseAddress(aImage, kCVPixelBufferLock_ReadOnly);

  } else {
#ifndef MOZ_WIDGET_UIKIT
#  if defined(MOZ_LEGACY_MACOS_TARGET)
    RefPtr<layers::Image> image = CreateBGRAImage(aImage);
#  else
    CFTypeRefPtr<IOSurfaceRef> surface =
        CFTypeRefPtr<IOSurfaceRef>::WrapUnderGetRule(
            CVPixelBufferGetIOSurface(aImage));
    MOZ_ASSERT(surface, "Decoder didn't return an IOSurface backed buffer");

    RefPtr<MacIOSurface> macSurface =
        new MacIOSurface(std::move(surface), mColorSpace, mTransferFunction,
                         MacIOSurface::AllowAlpha::Yes);
    macSurface->SetYUVColorSpace(mColorSpace);
    macSurface->mColorPrimaries = mColorPrimaries;

    RefPtr<layers::Image> image = new layers::MacIOSurfaceImage(macSurface);
#  endif

    data = VideoData::CreateFromImage(
        info.mDisplay, aFrameRef.byte_offset, aFrameRef.composition_timestamp,
        aFrameRef.duration, image.forget(), aFrameRef.is_sync_point,
        aFrameRef.decode_timestamp);

#else
    MOZ_ASSERT_UNREACHABLE("No MacIOSurface on iOS");
#endif
  }

  if (!data) {
    NS_ERROR("Couldn't create VideoData for frame");
    MonitorAutoLock mon(mMonitor);
    mPromise.Reject(MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__), __func__);
    return;
  }

  // Frames come out in DTS order but we need to output them
  // in composition order.
  MonitorAutoLock mon(mMonitor);
  mReorderQueue.Push(std::move(data));
  MaybeResolveBufferedFrames();

  LOG("%llu decoded frames queued",
      static_cast<unsigned long long>(mReorderQueue.Length()));

}

#if defined(XP_MACOSX) && defined(MOZ_LEGACY_MACOS_TARGET)
// CoreAnimation cannot display VDA's YUV output before 10.8, so convert each
// frame once into a BGRA IOSurface the compositor can present directly.
// libyuv's "ARGB" is byte-order B,G,R,A on little-endian, the layout of a
// BGRA surface, and VDA output is always limited-range YUV.
already_AddRefed<layers::Image>
AppleVDADecoder::CreateBGRAImage(CVPixelBufferRef aImage)
{
  if (CVPixelBufferLockBaseAddress(aImage, kCVPixelBufferLock_ReadOnly) !=
      kCVReturnSuccess) {
    return nullptr;
  }
  gfx::IntSize size((int32_t)CVPixelBufferGetWidth(aImage),
                    (int32_t)CVPixelBufferGetHeight(aImage));
  LOG("AppleVDADecoder: BGRA frame %dx%d format %c%c%c%c", size.width,
      size.height, (char)(CVPixelBufferGetPixelFormatType(aImage) >> 24),
      (char)(CVPixelBufferGetPixelFormatType(aImage) >> 16),
      (char)(CVPixelBufferGetPixelFormatType(aImage) >> 8),
      (char)CVPixelBufferGetPixelFormatType(aImage));
  RefPtr<MacIOSurface> surface = TakePooledBGRASurface(size);
  if (!surface) {
    surface = MacIOSurface::CreateIOSurface(
        size.width, size.height, MacIOSurface::AllowAlpha::No,
        gfx::YUVColorSpace::Identity, mTransferFunction);
  }
  if (!surface || !surface->Lock(false)) {
    CVPixelBufferUnlockBaseAddress(aImage, kCVPixelBufferLock_ReadOnly);
    return nullptr;
  }
  surface->mColorPrimaries = mColorPrimaries;
  const libyuv::YuvConstants* matrix = nullptr;
  switch (mColorSpace) {
    case gfx::YUVColorSpace::BT2020:
      matrix = &libyuv::kYuv2020Constants;
      break;
    case gfx::YUVColorSpace::BT709:
      matrix = &libyuv::kYuvH709Constants;
      break;
    default:
      matrix = &libyuv::kYuvI601Constants;
      break;
  }
  if (CVPixelBufferGetPixelFormatType(aImage) ==
      kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
    libyuv::NV12ToARGBMatrix(
        (const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(aImage, 0),
        (int)CVPixelBufferGetBytesPerRowOfPlane(aImage, 0),
        (const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(aImage, 1),
        (int)CVPixelBufferGetBytesPerRowOfPlane(aImage, 1),
        (uint8_t*)surface->GetBaseAddressOfPlane(0),
        surface->GetBytesPerRow(0), matrix, size.width, size.height);
  } else {
    libyuv::UYVYToARGBMatrix(
        (const uint8_t*)CVPixelBufferGetBaseAddress(aImage),
        (int)CVPixelBufferGetBytesPerRow(aImage),
        (uint8_t*)surface->GetBaseAddressOfPlane(0),
        surface->GetBytesPerRow(0), matrix, size.width, size.height);
  }
  surface->Unlock(false);
  CVPixelBufferUnlockBaseAddress(aImage, kCVPixelBufferLock_ReadOnly);
  RefPtr<layers::Image> image = new layers::MacIOSurfaceImage(surface);
  mBGRASurfacePool.AppendElement(std::move(surface));
  return image.forget();
}

RefPtr<MacIOSurface>
AppleVDADecoder::TakePooledBGRASurface(const gfx::IntSize& aSize)
{
  // A surface is only safe to overwrite once nothing references its pixels:
  // while a decoded frame holds the surface through its MacIOSurfaceImage the
  // pool is not the sole owner (refCount > 1), and IOSurfaceIsInUse must be
  // clear so no pending CoreAnimation read in the compositor process loses
  // the race. Recycle oldest-first; evict still-referenced surfaces only
  // when a different size is requested.
  mBGRASurfacePool.RemoveElementsBy(
      [](const RefPtr<MacIOSurface>& aSurface) {
        return &::IOSurfaceIsInUse &&
               ::IOSurfaceIsInUse(aSurface->GetIOSurfaceRef().get());
      });
  for (uint32_t i = 0; i < mBGRASurfacePool.Length(); i++) {
    // A reference, not a copy: taking a RefPtr here would raise the refcount
    // and make the sole-owner test below always fail.
    RefPtr<MacIOSurface>& surface = mBGRASurfacePool[i];
    if (surface->GetSize(0) != aSize) {
      mBGRASurfacePool.RemoveElementAt(i);
      i--;
      continue;
    }
    if (surface->refCount() != 1) {
      continue;
    }
    RefPtr<MacIOSurface> result = surface;
    mBGRASurfacePool.RemoveElementAt(i);
    return result;
  }
  return nullptr;
}
#endif


void AppleVDADecoder::OnDecodeError(OSStatus aError) {
  MonitorAutoLock mon(mMonitor);
  mPromise.RejectIfExists(
      MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                  RESULT_DETAIL("OnDecodeError:%x", aError)),
      __func__);
}

RefPtr<MediaDataDecoder::DecodePromise> AppleVDADecoder::Decode(
    MediaRawData* aSample) {
  LOG("mp4 input sample %p pts %lld duration %lld us%s %zu bytes", aSample,
      aSample->mTime.ToMicroseconds(), aSample->mDuration.ToMicroseconds(),
      aSample->mKeyframe ? " keyframe" : "", aSample->Size());

  RefPtr<AppleVDADecoder> self = this;
  RefPtr<MediaRawData> sample = aSample;
  return InvokeAsync(mTaskQueue, __func__, [self, this, sample] {
    RefPtr<DecodePromise> p;
    {
      MonitorAutoLock mon(mMonitor);
      p = mPromise.Ensure(__func__);
    }
    ProcessDecode(sample);
    return p;
  });
}

void
AppleVDADecoder::ProcessDecode(MediaRawData* aSample)
{

  AutoCFTypeRef<CFDataRef> block(
    CFDataCreate(kCFAllocatorDefault, aSample->Data(), aSample->Size()));
  if (!block) {
    NS_ERROR("Couldn't create CFData");
    return;
  }

  // TimeUnit members must not be passed to CFNumberCreate by pointer: their
  // internal tick count is in the media timescale, not microseconds.
  int64_t samplePts = aSample->mTime.ToMicroseconds();
  int64_t sampleDts = aSample->mTimecode.ToMicroseconds();
  int64_t sampleDuration = aSample->mDuration.ToMicroseconds();
  AutoCFTypeRef<CFNumberRef> pts(
    CFNumberCreate(kCFAllocatorDefault,
                   kCFNumberSInt64Type,
                   &samplePts));
  AutoCFTypeRef<CFNumberRef> dts(
    CFNumberCreate(kCFAllocatorDefault,
                   kCFNumberSInt64Type,
                   &sampleDts));
  AutoCFTypeRef<CFNumberRef> duration(
    CFNumberCreate(kCFAllocatorDefault,
                   kCFNumberSInt64Type,
                   &sampleDuration));
  AutoCFTypeRef<CFNumberRef> byte_offset(
    CFNumberCreate(kCFAllocatorDefault,
                   kCFNumberSInt64Type,
                   &aSample->mOffset));
  char keyframe = aSample->mKeyframe ? 1 : 0;
  AutoCFTypeRef<CFNumberRef> cfkeyframe(
    CFNumberCreate(kCFAllocatorDefault,
                   kCFNumberSInt8Type,
                   &keyframe));

  const void* keys[] = { CFSTR("FRAME_PTS"),
                         CFSTR("FRAME_DTS"),
                         CFSTR("FRAME_DURATION"),
                         CFSTR("FRAME_OFFSET"),
                         CFSTR("FRAME_KEYFRAME") };
  const void* values[] = { pts,
                           dts,
                           duration,
                           byte_offset,
                           cfkeyframe };
  static_assert(std::size(keys) == std::size(values),
                "Non matching keys/values array size");

  AutoCFTypeRef<CFMutableDictionaryRef> frameInfo(
    CFDictionaryCreateMutable(kCFAllocatorDefault,
                              std::size(keys) + 1,
                              &kCFTypeDictionaryKeyCallBacks,
                              &kCFTypeDictionaryValueCallBacks));
  if (!frameInfo) {
    MonitorAutoLock mon(mMonitor);
    mPromise.Reject(
        MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__), __func__);
    return;
  }
  CFDictionaryAddValue(frameInfo, FrameInfoCompensationKey(),
                       kCFBooleanTrue);
  for (size_t i = 0; i < std::size(keys); i++) {
    CFDictionaryAddValue(frameInfo, keys[i], values[i]);
  }

  // TN2267: the decoder delivers frameInfo to the callback asynchronously
  // without keeping it alive, so retain our own reference for that
  // delivery; PlatformCallback releases it. The callback can also fire
  // synchronously, so this must happen before the decode call.
  CFRetain(frameInfo);

  OSStatus rv = VDADecoderDecode(mDecoder,
                                 0,
                                 block,
                                 frameInfo);

  if (rv != noErr) {
    NS_WARNING("AppleVDADecoder: Couldn't pass frame to decoder");
    // The callback for this frame may already have consumed the
    // compensation reference; only release it if it did not.
    if (CFDictionaryContainsKey(frameInfo, FrameInfoCompensationKey())) {
      CFRelease(frameInfo);
    }
    return;
  }

  return;
}

static const char* VDAErrorName(OSStatus aError) {
  switch (aError) {
    case kVDADecoderNoErr:
      return "kVDADecoderNoErr";
    case kVDADecoderHardwareNotSupportedErr:
      return "kVDADecoderHardwareNotSupportedErr";
    case kVDADecoderFormatNotSupportedErr:
      return "kVDADecoderFormatNotSupportedErr";
    case kVDADecoderConfigurationError:
      return "kVDADecoderConfigurationError";
    case kVDADecoderDecoderFailedErr:
      return "kVDADecoderDecoderFailedErr";
    default:
      return "unknown";
  }
}

MediaResult
AppleVDADecoder::InitializeSession()
{
  OSStatus rv;

  AutoCFTypeRef<CFDictionaryRef> decoderConfig(
    CreateDecoderSpecification());

  AutoCFTypeRef<CFDictionaryRef> outputConfiguration(
    CreateOutputConfiguration());

  rv =
    VDADecoderCreate(decoderConfig,
                     outputConfiguration,
                     (VDADecoderOutputCallback*)PlatformCallback,
                     this,
                     &mDecoder);

#if defined(XP_MACOSX) && defined(MOZ_LEGACY_MACOS_TARGET)
  // The hardware's native output is packed UYVY; if a VDA implementation
  // refuses it, retry once with NV12 regardless of the error kind.
  if (rv != noErr && !mOutputIsNV12) {
    LOG("AppleVDADecoder: UYVY output refused (%d), retrying with NV12", rv);
    mOutputIsNV12 = true;
    AutoCFTypeRef<CFDictionaryRef> nv12Configuration(
      CreateOutputConfiguration());
    rv =
      VDADecoderCreate(decoderConfig,
                       nv12Configuration,
                       (VDADecoderOutputCallback*)PlatformCallback,
                       this,
                       &mDecoder);
  }
  // Session creation is intermittently refused with
  // kVDADecoderDecoderFailedErr right after a previous session was
  // destroyed (loop points, error recovery); other errors are permanent,
  // so only this one deserves delayed retries before falling back to
  // software decoding.
  for (int attempt = 0;
       rv == kVDADecoderDecoderFailedErr && attempt < 3; attempt++) {
    LOG("AppleVDADecoder: session refused (%d), retry %d", rv, attempt);
    PR_Sleep(PR_MillisecondsToInterval(100));
    AutoCFTypeRef<CFDictionaryRef> retryConfiguration(
      CreateOutputConfiguration());
    rv =
      VDADecoderCreate(decoderConfig,
                       retryConfiguration,
                       (VDADecoderOutputCallback*)PlatformCallback,
                       this,
                       &mDecoder);
  }
#endif

  mIsHardwareAccelerated = rv == 0 ? 1 : 0; //kVDADecoderNoErr = 0
  if (rv != noErr) {
    size_t extraSize = mExtraData->Length();
    LOG("AppleVDADecoder: VDADecoderCreate failed: %s (%d), %dx%d, avcC %zu "
        "bytes, profile %d compat %d level %d",
        VDAErrorName(rv), rv, mPictureWidth, mPictureHeight, extraSize,
        extraSize >= 4 ? (*mExtraData)[1] : 0,
        extraSize >= 4 ? (*mExtraData)[2] : 0,
        extraSize >= 4 ? (*mExtraData)[3] : 0);
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("Couldn't create format description!"));
  }

  LOG("AppleVDADecoder: %s hardware accelerated decoding",
      mIsHardwareAccelerated ? "using" : "not using");

  return NS_OK;
}

CFDictionaryRef
AppleVDADecoder::CreateDecoderSpecification()
{
  const uint8_t* extradata = mExtraData->Elements();
  int extrasize = mExtraData->Length();

  OSType format = 'avc1';
  AutoCFTypeRef<CFNumberRef> avc_width(
    CFNumberCreate(kCFAllocatorDefault,
                   kCFNumberSInt32Type,
                   &mPictureWidth));
  AutoCFTypeRef<CFNumberRef> avc_height(
    CFNumberCreate(kCFAllocatorDefault,
                   kCFNumberSInt32Type,
                   &mPictureHeight));
  AutoCFTypeRef<CFNumberRef> avc_format(
    CFNumberCreate(kCFAllocatorDefault,
                   kCFNumberSInt32Type,
                   &format));
  AutoCFTypeRef<CFDataRef> avc_data(
    CFDataCreate(kCFAllocatorDefault,
                 extradata,
                 extrasize));

  const void* decoderKeys[] = { AppleVDALinker::skPropWidth,
                                AppleVDALinker::skPropHeight,
                                AppleVDALinker::skPropSourceFormat,
                                AppleVDALinker::skPropAVCCData };
  const void* decoderValue[] = { avc_width,
                                 avc_height,
                                 avc_format,
                                 avc_data };
  static_assert(std::size(decoderKeys) == std::size(decoderValue),
                "Non matching keys/values array size");

  return CFDictionaryCreate(kCFAllocatorDefault,
                            decoderKeys,
                            decoderValue,
                            std::size(decoderKeys),
                            &kCFTypeDictionaryKeyCallBacks,
                            &kCFTypeDictionaryValueCallBacks);
}

CFDictionaryRef
AppleVDADecoder::CreateOutputConfiguration()
{
#if defined(XP_MACOSX) && defined(MOZ_LEGACY_MACOS_TARGET)
  if (mOutputIsNV12) {
#else
  if (mUseSoftwareImages) {
#endif
    // Output format type:
    SInt32 PixelFormatTypeValue =
      kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    AutoCFTypeRef<CFNumberRef> PixelFormatTypeNumber(
      CFNumberCreate(kCFAllocatorDefault,
                     kCFNumberSInt32Type,
                     &PixelFormatTypeValue));
    const void* outputKeys[] = { kCVPixelBufferPixelFormatTypeKey };
    const void* outputValues[] = { PixelFormatTypeNumber };
    static_assert(std::size(outputKeys) == std::size(outputValues),
                  "Non matching keys/values array size");

    return CFDictionaryCreate(kCFAllocatorDefault,
                              outputKeys,
                              outputValues,
                              std::size(outputKeys),
                              &kCFTypeDictionaryKeyCallBacks,
                              &kCFTypeDictionaryValueCallBacks);
  }

#ifndef MOZ_WIDGET_UIKIT
  // Output format type:
  OSType PixelFormatTypeValue = kCVPixelFormatType_422YpCbCr8;
  AutoCFTypeRef<CFNumberRef> PixelFormatTypeNumber(
    CFNumberCreate(kCFAllocatorDefault,
                   kCFNumberSInt32Type,
                   &PixelFormatTypeValue));
  // Construct IOSurface Properties
  const void* IOSurfaceKeys[] = {kIOSurfaceIsGlobal};
  const void* IOSurfaceValues[] = {kCFBooleanTrue};

  static_assert(std::size(IOSurfaceKeys) == std::size(IOSurfaceValues),
                "Non matching keys/values array size");

  // Contruct output configuration.
  AutoCFTypeRef<CFDictionaryRef> IOSurfaceProperties(
    CFDictionaryCreate(kCFAllocatorDefault,
                       IOSurfaceKeys,
                       IOSurfaceValues,
                       std::size(IOSurfaceKeys),
                       &kCFTypeDictionaryKeyCallBacks,
                       &kCFTypeDictionaryValueCallBacks));

  const void* outputKeys[] = { kCVPixelBufferIOSurfacePropertiesKey,
                               kCVPixelBufferPixelFormatTypeKey,
                               kCVPixelBufferOpenGLCompatibilityKey };
  const void* outputValues[] = { IOSurfaceProperties,
                                 PixelFormatTypeNumber,
                                 kCFBooleanTrue };
  static_assert(std::size(outputKeys) == std::size(outputValues),
                "Non matching keys/values array size");

  return CFDictionaryCreate(kCFAllocatorDefault,
                            outputKeys,
                            outputValues,
                            std::size(outputKeys),
                            &kCFTypeDictionaryKeyCallBacks,
                            &kCFTypeDictionaryValueCallBacks);
#else
  MOZ_ASSERT_UNREACHABLE("No MacIOSurface on iOS");
#endif
}
} // namespace mozilla
#undef LOG
#undef LOGEX
