/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MacIOSurface_h_
#define MacIOSurface_h_
#ifdef XP_DARWIN
#  include <CoreVideo/CoreVideo.h>
#  include <IOSurface/IOSurfaceRef.h>
#  include <QuartzCore/QuartzCore.h>
#  include <dlfcn.h>

#  if !defined(MAC_OS_X_VERSION_10_7) || \
      MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_7
// Pixel formats and colorimetry constants missing from the pre-Lion SDK.
// The externs are weak so string comparisons against them are safe (nil)
// on systems where they do not exist.
enum : OSType {
  kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange = '420v',
  kCVPixelFormatType_420YpCbCr8BiPlanarFullRange = '420f',
  kCVPixelFormatType_420YpCbCr8PlanarFullRange = 'f420',
  kCVPixelFormatType_422YpCbCr8FullRange = 'yuvf',
  kCVPixelFormatType_422YpCbCr8_yuvs = 'yuvs',
  kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange = 'x420',
  kCVPixelFormatType_420YpCbCr10BiPlanarFullRange = 'xf20',
  kCVPixelFormatType_422YpCbCr10BiPlanarVideoRange = 'x422',
  kCVPixelFormatType_422YpCbCr10BiPlanarFullRange = 'xf22',
  kCVPixelFormatType_4444AYpCbCr8 = 'y408',
  kCVPixelFormatType_4444AYpCbCr16 = 'y416',
  kCVPixelFormatType_4444AYpCbCrFloat = 'r4fl',
  kCVPixelFormatType_OneComponent8 = 'L008',
  kCVPixelFormatType_64RGBAHalf = 'RGhA',
  kCVPixelFormatType_128RGBAFloat = 'RGfA',
  kCVPixelFormatType_ARGB2101010LEPacked = 'l10r',
};

extern "C" {
extern const CFStringRef kCVImageBufferYCbCrMatrix_ITU_R_2020
    __attribute__((weak_import));
extern const CFStringRef kCVImageBufferColorPrimaries_ITU_R_2020
    __attribute__((weak_import));
extern const CFStringRef kCVImageBufferColorPrimaries_P3_D65
    __attribute__((weak_import));
extern const CFStringRef kCVImageBufferColorPrimaries_P22
    __attribute__((weak_import));
extern const CFStringRef kCVImageBufferTransferFunction_sRGB
    __attribute__((weak_import));
extern const CFStringRef kCVImageBufferTransferFunction_ITU_R_2100_HLG
    __attribute__((weak_import));
extern const CFStringRef kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ
    __attribute__((weak_import));
extern const CFStringRef kCVImageBufferTransferFunction_Linear
    __attribute__((weak_import));
extern const CFStringRef kCVImageBufferContentLightLevelInfoKey
    __attribute__((weak_import));
extern const CFStringRef kCVImageBufferMasteringDisplayColorVolumeKey
    __attribute__((weak_import));
void IOSurfaceIncrementUseCount(IOSurfaceRef buffer)
    __attribute__((weak_import));
void IOSurfaceDecrementUseCount(IOSurfaceRef buffer)
    __attribute__((weak_import));
Boolean IOSurfaceIsInUse(IOSurfaceRef buffer) __attribute__((weak_import));
extern const CFStringRef kCGColorSpaceDisplayP3 __attribute__((weak_import));
extern const CFStringRef kCGColorSpaceITUR_2020 __attribute__((weak_import));
extern const CFStringRef kCGColorSpaceITUR_709 __attribute__((weak_import));
}
#  endif

#  include "mozilla/gfx/Types.h"
#  include "mozilla/Maybe.h"
#  include "CFTypeRefPtr.h"

namespace mozilla {
namespace gl {
class GLContext;
}
}  // namespace mozilla

#  ifdef XP_MACOSX
struct _CGLContextObject;

typedef _CGLContextObject* CGLContextObj;
#  endif
typedef uint32_t IOSurfaceID;

#  ifdef XP_MACOSX
#    import <OpenGL/OpenGL.h>
#  else
#    include "GLTypes.h"
typedef realGLboolean GLboolean;
#    include <OpenGLES/ES2/gl.h>
#  endif

#  include "2D.h"
#  include "mozilla/RefCounted.h"

class MacIOSurface final
    : public mozilla::external::AtomicRefCounted<MacIOSurface> {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(MacIOSurface)
  typedef mozilla::gfx::SourceSurface SourceSurface;
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::gfx::BackendType BackendType;
  typedef mozilla::gfx::IntSize IntSize;
  typedef mozilla::gfx::ChromaSubsampling ChromaSubsampling;
  typedef mozilla::gfx::YUVColorSpace YUVColorSpace;
  typedef mozilla::gfx::ColorSpace2 ColorSpace2;
  typedef mozilla::gfx::TransferFunction TransferFunction;
  typedef mozilla::gfx::ColorRange ColorRange;
  typedef mozilla::gfx::ColorDepth ColorDepth;

  enum class AllowAlpha : bool { No, Yes };

  // The usage count of the IOSurface is increased by 1 during the lifetime
  // of the MacIOSurface instance.
  // MacIOSurface holds a reference to the corresponding IOSurface.

  static already_AddRefed<MacIOSurface> CreateIOSurface(
      int aWidth, int aHeight, AllowAlpha aAllowAlpha,
      YUVColorSpace aColorSpace = YUVColorSpace::Identity,
      TransferFunction aTransferFunction = TransferFunction::SRGB);
  static already_AddRefed<MacIOSurface> CreateBiPlanarSurface(
      const IntSize& aYSize, const IntSize& aCbCrSize,
      ChromaSubsampling aChromaSubsampling, YUVColorSpace aColorSpace,
      TransferFunction aTransferFunction, ColorRange aColorRange,
      ColorDepth aColorDepth, AllowAlpha aAllowAlpha);
  static already_AddRefed<MacIOSurface> CreateSinglePlanarSurface(
      const IntSize& aSize, YUVColorSpace aColorSpace,
      TransferFunction aTransferFunction, ColorRange aColorRange,
      AllowAlpha aAllowAlpha);
  static void ReleaseIOSurface(MacIOSurface* aIOSurface);
  static already_AddRefed<MacIOSurface> LookupSurface(
      IOSurfaceID aSurfaceID, YUVColorSpace aColorSpace,
      TransferFunction aTransferFunction, AllowAlpha aAllowAlpha);
  static mozilla::gfx::SurfaceFormat SurfaceFormatForPixelFormat(
      OSType aPixelFormat, AllowAlpha aAllowAlpha);
  static bool HasAlphaForPixelFormat(OSType aPixelFormat);

  explicit MacIOSurface(CFTypeRefPtr<IOSurfaceRef> aIOSurfaceRef,
                        YUVColorSpace aColorSpace,
                        TransferFunction aTransferFunction,
                        AllowAlpha aAllowAlpha);

  ~MacIOSurface();
  IOSurfaceID GetIOSurfaceID() const;
  void* GetBaseAddress() const;
  void* GetBaseAddressOfPlane(size_t planeIndex) const;
  size_t GetPlaneCount() const;
  OSType GetPixelFormat() const;
  // GetWidth() and GetHeight() return values in "display pixels".  A
  // "display pixel" is the smallest fully addressable part of a display.
  // But in HiDPI modes each "display pixel" corresponds to more than one
  // device pixel.  Use GetDevicePixel**() to get device pixels.
  size_t GetWidth(size_t plane = 0) const;
  size_t GetHeight(size_t plane = 0) const;
  IntSize GetSize(size_t plane = 0) const {
    return IntSize(GetWidth(plane), GetHeight(plane));
  }
  size_t GetDevicePixelWidth(size_t plane = 0) const;
  size_t GetDevicePixelHeight(size_t plane = 0) const;
  size_t GetBytesPerRow(size_t plane = 0) const;
  size_t GetAllocSize() const;
  bool Lock(bool aReadOnly = true);
  void Unlock(bool aReadOnly = true);
  bool IsLocked() const { return mIsLocked; }
  void IncrementUseCount();
  void DecrementUseCount();
  bool HasAlpha() const { return mHasAlpha; }
  mozilla::gfx::SurfaceFormat GetFormat() const;
  mozilla::gfx::SurfaceFormat GetReadFormat() const;
  mozilla::gfx::ColorDepth GetColorDepth() const;
  // This would be better suited on MacIOSurfaceImage type, however due to the
  // current data structure, this is not possible as only the IOSurfaceRef is
  // being used across.
  void SetYUVColorSpace(YUVColorSpace aColorSpace) {
    mColorSpace = aColorSpace;
  }
  YUVColorSpace GetYUVColorSpace() const { return mColorSpace; }
  TransferFunction GetTransferFunction() const { return mTransferFunction; }
  bool IsFullRange() const {
    OSType format = GetPixelFormat();
    return (format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange ||
            format == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange ||
            format == kCVPixelFormatType_422YpCbCr10BiPlanarFullRange ||
            format == kCVPixelFormatType_422YpCbCr8FullRange);
  }
  mozilla::gfx::ColorRange GetColorRange() const {
    if (IsFullRange()) return mozilla::gfx::ColorRange::FULL;
    return mozilla::gfx::ColorRange::LIMITED;
  }

  bool IsHDRSurface() {
    return mozilla::gfx::IsHDRTransferFunction(mTransferFunction);
  }

  // Bind this IOSurface to a texture using the most efficient mechanism
  // available on the current platform.
  //
  // Note that on iOS simulator, due to incomplete support for
  // texImageIOSurface, this will only use texImage2D to upload, and cannot be
  // used to read-back the GL texture to an IOSurface.
  bool BindTexImage(mozilla::gl::GLContext* aGL, size_t aPlane,
                    mozilla::gfx::SurfaceFormat* aOutReadFormat = nullptr);

  already_AddRefed<SourceSurface> GetAsSurface();

  // Creates a DrawTarget that wraps the data in the IOSurface. Rendering to
  // this DrawTarget directly manipulates the contents of the IOSurface.
  // Only call when the surface is already locked for writing!
  // The returned DrawTarget must only be used while the surface is still
  // locked.
  // Also, only call this if you're reasonably sure that the DrawTarget of the
  // selected backend supports the IOSurface's SurfaceFormat.
  already_AddRefed<DrawTarget> GetAsDrawTargetLocked(BackendType aBackendType);

  static size_t GetMaxWidth();
  static size_t GetMaxHeight();

#  ifdef DEBUG
  static mozilla::Maybe<OSType> ChoosePixelFormat(
      mozilla::gfx::ChromaSubsampling aChromaSubsampling,
      mozilla::gfx::ColorRange aColorRange,
      mozilla::gfx::ColorDepth aColorDepth);
#  endif

  CFTypeRefPtr<IOSurfaceRef> GetIOSurfaceRef() { return mIOSurfaceRef; }

  void SetColorSpace(mozilla::gfx::ColorSpace2) const;
  void SetTransferFunction(mozilla::gfx::TransferFunction) const;

  ColorSpace2 mColorPrimaries = ColorSpace2::UNKNOWN;

 private:
  CFTypeRefPtr<IOSurfaceRef> mIOSurfaceRef;
  const bool mHasAlpha;
  YUVColorSpace mColorSpace = YUVColorSpace::Identity;
  TransferFunction mTransferFunction = TransferFunction::SRGB;
  bool mIsLocked = false;
};

#endif
#endif
