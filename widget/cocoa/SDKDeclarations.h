/* -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SDKDefines_h
#define SDKDefines_h

#import <Cocoa/Cocoa.h>

// The 10.6 SDK predates the availability macros; define them so everything
// below is weak-imported when the deployment target is older.
#ifndef NS_AVAILABLE_MAC
#define NS_AVAILABLE_MAC(m) __attribute__((availability(macos, introduced=m)))
#endif
#ifndef NS_CLASS_AVAILABLE_MAC
#define NS_CLASS_AVAILABLE_MAC(m) NS_AVAILABLE_MAC(m)
#endif
#ifndef NS_ENUM_AVAILABLE_MAC
#define NS_ENUM_AVAILABLE_MAC(m)
#endif
#ifndef NS_ENUM
#define NS_ENUM(_type, _name) enum _name : _type _name; enum _name : _type
#endif
#ifndef API_AVAILABLE
#define API_AVAILABLE(...)
#endif
#ifndef API_DEPRECATED
#define API_DEPRECATED(...)
#endif
#ifndef API_UNAVAILABLE
#define API_UNAVAILABLE(...)
#endif

// The 10.6 SDK predates the CF toll-free bridging macros.
#ifndef CFBridgingRetain
#define CFBridgingRetain(X) ((__bridge CFTypeRef)(X))
#define CFBridgingRelease(X) ((__bridge id)(X))
#endif

extern "C" {
#if !defined(MAC_OS_X_VERSION_10_9) || \
    MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_9
extern NSString* const NSWindowDidChangeOcclusionStateNotification
    NS_AVAILABLE_MAC(10_9);
extern NSString* const CBAdvertisementDataOverflowServiceUUIDsKey
    NS_AVAILABLE_MAC(10_9);
extern NSString* const CBAdvertisementDataIsConnectable NS_AVAILABLE_MAC(10_9);
#endif  // MAC_OS_X_VERSION_10_9

#if !defined(MAC_OS_X_VERSION_10_10) || \
    MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_10
extern NSString* const NSUserActivityTypeBrowsingWeb NS_AVAILABLE_MAC(10_10);

NS_CLASS_AVAILABLE_MAC(10_10)
@interface NSUserActivity : NSObject
@property(copy) NSString* activityType;
@property(copy) NSURL* webpageURL;
- (instancetype)initWithActivityType:(NSString*)activityType;
- (void)becomeCurrent;
- (void)resignCurrent;
- (void)invalidate;
- (void)setEligibleForHandoff:(BOOL)eligible;
- (void)setTitle:(NSString*)title;
@end
extern NSString* const NSAppearanceNameVibrantDark NS_AVAILABLE_MAC(10_10);
extern NSString* const NSAppearanceNameVibrantLight NS_AVAILABLE_MAC(10_10);
#endif  // MAC_OS_X_VERSION_10_10
}  // extern "C"

/**
 * This file contains header declarations from SDKs more recent than the minimum macOS SDK which we
 * require for building Firefox, which is currently the macOS 10.12 SDK.
 */

#if !defined(MAC_OS_X_VERSION_10_7) || MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_7
// Declarations needed when building against a pre-Lion (10.6) SDK. The
// build compiles at a 10.7 deployment-target floor (see toolchain.configure),
// so classes and functions referenced by generated code are annotated as
// 10.8 to keep those references weak; they resolve to nil/NULL at runtime on
// 10.6 (guarded at call sites) and to the real implementation on 10.7+.
// Notification-name constants are substituted with local string literals of
// identical value instead of being declared extern, because AppKit compares
// them by value, not identity.

typedef struct {
  CGFloat top, left, bottom, right;
} NSEdgeInsets;

static inline NSEdgeInsets NSEdgeInsetsMake(CGFloat t, CGFloat l, CGFloat b,
                                            CGFloat r) {
  NSEdgeInsets insets = {t, l, b, r};
  return insets;
}

typedef NSInteger NSEventPhase;
enum {
  NSEventPhaseNone = 0,
  NSEventPhaseBegan = 0x1 << 0,
  NSEventPhaseStationary = 0x1 << 1,
  NSEventPhaseChanged = 0x1 << 2,
  NSEventPhaseEnded = 0x1 << 3,
  NSEventPhaseCancelled = 0x1 << 4,
};

typedef NSUInteger NSEventModifierFlags;
enum {
  NSEventModifierFlagCapsLock = 1 << 16,
  NSEventModifierFlagShift = 1 << 17,
  NSEventModifierFlagControl = 1 << 18,
  NSEventModifierFlagOption = 1 << 19,
  NSEventModifierFlagCommand = 1 << 20,
  NSEventModifierFlagNumericPad = 1 << 21,
  NSEventModifierFlagHelp = 1 << 22,
  NSEventModifierFlagFunction = 1 << 23,
  NSEventModifierFlagDeviceIndependentFlagsMask = 0xFFFF0000UL,
};

// The NSEventType/NSEventMask spellings were introduced later; map them to
// the pre-Lion names. SmartMagnify has no pre-Lion equivalent.
#define NSEventMaskAny NSAnyEventMask
#define NSEventMaskLeftMouseDown NSLeftMouseDownMask
#define NSEventMaskOtherMouseDown NSOtherMouseDownMask
#define NSEventTypeApplicationDefined NSApplicationDefined
#define NSEventTypeFlagsChanged NSFlagsChanged
#define NSEventTypeGesture NSGesture
#define NSEventTypeKeyDown NSKeyDown
#define NSEventTypeKeyUp NSKeyUp
#define NSEventTypeLeftMouseDown NSLeftMouseDown
#define NSEventTypeLeftMouseDragged NSLeftMouseDragged
#define NSEventTypeLeftMouseUp NSLeftMouseUp
#define NSEventTypeMagnify NSMagnify
#define NSEventTypeMouseEntered NSMouseEntered
#define NSEventTypeMouseExited NSMouseExited
#define NSEventTypeMouseMoved NSMouseMoved
#define NSEventTypeOther NSOther
#define NSEventTypeOtherMouseDown NSOtherMouseDown
#define NSEventTypeOtherMouseDragged NSOtherMouseDragged
#define NSEventTypeOtherMouseUp NSOtherMouseUp
#define NSEventTypeRightMouseDown NSRightMouseDown
#define NSEventTypeRightMouseDragged NSRightMouseDragged
#define NSEventTypeRightMouseUp NSRightMouseUp
#define NSEventTypeScrollWheel NSScrollWheel
#define NSEventTypeSmartMagnify NSApplicationDefined
#define NSEventTypeSystemDefined NSSystemDefined

enum {
  NSWindowStyleMaskFullScreen = 1 << 14,
};
#define NSFullScreenWindowMask (1 << 14)

// The NSWindowStyleMask spellings were introduced in 10.12; the pre-Lion
// constants carry the same values.
#define NSWindowStyleMaskBorderless NSBorderlessWindowMask
#define NSWindowStyleMaskTitled NSTitledWindowMask
#define NSWindowStyleMaskClosable NSClosableWindowMask
#define NSWindowStyleMaskMiniaturizable NSMiniaturizableWindowMask
#define NSWindowStyleMaskResizable NSResizableWindowMask
#define NSWindowStyleMaskTexturedBackground NSTexturedBackgroundWindowMask
#define NSWindowStyleMaskUnifiedTitleAndToolbar NSUnifiedTitleAndToolbarWindowMask
#define NSWindowStyleMaskHUDWindow NSHUDWindowMask
#define NSFullSizeContentViewWindowMask (1 << 15)
#define NSWindowStyleMaskFullSizeContentView NSFullSizeContentViewWindowMask
#define NSWindowStyleMaskUtilityWindow NSUtilityWindowMask

// The NSControlState spellings were introduced later.
typedef NSInteger NSControlStateValue;
#define NSControlStateValueMixed NSMixedState
#define NSControlStateValueOn NSOnState
#define NSControlStateValueOff NSOffState

// The NSButtonType spellings were introduced later.
#define NSButtonTypeMomentaryLight NSMomentaryLightButton
#define NSButtonTypePushOnPushOff NSPushOnPushOffButton
#define NSButtonTypeToggle NSToggleButton
#define NSButtonTypeSwitch NSSwitchButton
#define NSButtonTypeSwitchButton NSSwitchButton
#define NSButtonTypeRadio NSRadioButton
#define NSButtonTypeMomentaryChange NSMomentaryChangeButton
#define NSButtonTypeOnOff NSOnOffButton
#define NSButtonTypeMomentaryPushIn NSMomentaryPushInButton
#define NSButtonTypeAccelerator NSAcceleratorButton
#define NSButtonTypeMultiLevelAccelerator NSMultiLevelAcceleratorButton

// The NSControlSize spellings.
#define NSBitmapImageFileTypePNG NSPNGFileType
#define NSBitmapFormatAlphaFirst NSAlphaFirstBitmapFormat
#define NSBitmapFormatAlphaNonpremultiplied NSAlphaNonpremultipliedBitmapFormat
#define NSBitmapFormatFloatingPointSamples NSFloatingPointSamplesBitmapFormat

#define NSControlSizeMini NSMiniControlSize
#define NSControlSizeSmall NSSmallControlSize
#define NSControlSizeRegular NSRegularControlSize

// AutoLayout anchors (10.7+) — minimal surface for the TouchBar code.
@class NSLayoutXAxisAnchor;
@interface NSView (PowerFoxAnchorCompat)
@property(readonly, strong) NSLayoutXAxisAnchor* widthAnchor;
@property BOOL translatesAutoresizingMaskIntoConstraints;
@end

@interface NSButton (PowerFoxTouchBarCompat)
@property BOOL imageHugsTitle;
@end

NS_CLASS_AVAILABLE_MAC(10_7)
@interface NSLayoutConstraint : NSObject
@property BOOL active NS_AVAILABLE_MAC(10_10);
+ (NSArray*)constraintsWithVisualFormat:(NSString*)format
                                options:(NSUInteger)opts
                                metrics:(NSDictionary*)metrics
                                  views:(NSDictionary*)views;
+ (void)activateConstraints:(NSArray*)constraints;
@end

NS_CLASS_AVAILABLE_MAC(10_7)
@interface NSLayoutXAxisAnchor : NSObject
- (NSLayoutConstraint*)constraintGreaterThanOrEqualToConstant:(CGFloat)c;
- (NSLayoutConstraint*)constraintLessThanOrEqualToConstant:(CGFloat)c;
@end

enum {
  NSLayoutFormatAlignAllLeft = 1 << 0,
  NSLayoutFormatAlignAllRight = 1 << 1,
  NSLayoutFormatAlignAllTop = 1 << 2,
  NSLayoutFormatAlignAllBottom = 1 << 3,
  NSLayoutFormatAlignAllLeading = 1 << 4,
  NSLayoutFormatAlignAllTrailing = 1 << 5,
  NSLayoutFormatAlignAllCenterX = 1 << 6,
  NSLayoutFormatAlignAllCenterY = 1 << 7,
  NSLayoutFormatAlignmentMask = 0xFF,
  NSLayoutFormatDirectionLeadingToTrailing = 0 << 8,
  NSLayoutFormatDirectionLeftToRight = 1 << 8,
  NSLayoutFormatDirectionRightToLeft = 2 << 8,
};

// Keyed subscripting compiles against these declarations; the runtime
// methods only exist on 10.8+, so subscripted code paths must stay guarded.
@interface NSDictionary (PowerFoxKeyedSubscripting)
- (id)objectForKeyedSubscript:(id)aKey;
@end

@interface NSMutableDictionary (PowerFoxKeyedSubscripting)
- (void)setObject:(id)anObject forKeyedSubscript:(id<NSCopying>)aKey;
@end

NS_CLASS_AVAILABLE_MAC(10_9)
@interface NSProgress : NSObject
@property(copy) NSString* kind;
@property BOOL cancellable;
@property(copy) void (^cancellationHandler)(void);
@property int64_t totalUnitCount;
@property int64_t completedUnitCount;
- (instancetype)initWithParent:(NSProgress*)parent
                      userInfo:(NSDictionary*)userInfo;
- (void)publish;
- (void)unpublish;
@end

extern "C" NSString* const NSProgressKindFile NS_AVAILABLE_MAC(10_9);
extern "C" NSString* const NSProgressFileOperationKindKey
    NS_AVAILABLE_MAC(10_9);
extern "C" NSString* const NSProgressFileOperationKindDownloading
    NS_AVAILABLE_MAC(10_9);
extern "C" NSString* const NSProgressFileURLKey NS_AVAILABLE_MAC(10_9);

NS_CLASS_AVAILABLE_MAC(26_0)
@interface NSGlassEffectView : NSView
@property CGFloat cornerRadius;
@end

@interface NSColor (PowerFoxClassPropertyCompat)
@property(class, strong, readonly) NSColor* controlBackgroundColor;
@property(readonly) CGColorRef CGColor NS_AVAILABLE_MAC(10_8);
@end

// The NSBezelStyle* spellings map to the pre-Lion constants.
#define NSBezelStyleRounded NSRoundedBezelStyle
#define NSBezelStyleRegularSquare NSRegularSquareBezelStyle
#define NSBezelStyleThickSquare NSThickSquareBezelStyle
#define NSBezelStyleThickerSquare NSThickerSquareBezelStyle
#define NSBezelStyleDisclosure NSDisclosureBezelStyle
#define NSBezelStyleShadowlessSquare NSShadowlessSquareBezelStyle
#define NSBezelStyleCircular NSCircularBezelStyle
#define NSBezelStyleTexturedSquare NSTexturedSquareBezelStyle
#define NSBezelStyleHelpButton NSHelpButtonBezelStyle
#define NSBezelStyleSmallSquare NSSmallSquareBezelStyle
#define NSBezelStyleTexturedRounded NSTexturedRoundedBezelStyle
#define NSBezelStyleRoundRect NSRoundRectBezelStyle
#define NSBezelStyleRoundedDisclosure NSDisclosureBezelStyle
#define NSBezelStyleRecessed NSRoundRectBezelStyle
#define NSBezelStyleInline NSRegularSquareBezelStyle

enum {
  NSRectEdgeMinX = 0,
  NSRectEdgeMinY = 1,
  NSRectEdgeMaxX = 2,
  NSRectEdgeMaxY = 3,
};

// NSEventSubtype spellings.
#define NSEventSubtypeTabletPoint NSTabletPointEventSubtype

// Old spellings used alongside the new ones.
#define NSGesture NSEventTypeGesture
#define NSMagnify NSEventTypeMagnify

// NSLevelIndicator styles.
enum {
  NSLevelIndicatorStyleRelevancy = 0,
  NSLevelIndicatorStyleContinuousCapacity = 1,
  NSLevelIndicatorStyleDiscreteCapacity = 2,
  NSLevelIndicatorStyleRatingLevel = 3,
};

// NSVisualEffectView (10.10) and its supporting types.
typedef NSInteger NSVisualEffectMaterial;
enum {
  NSVisualEffectMaterialTitlebar = 3,
  NSVisualEffectMaterialSidebar = 6,
  NSVisualEffectMaterialMenu = 5,
  NSVisualEffectMaterialToolTip = 17,
  NSVisualEffectMaterialContentBackground = 18,
  NSVisualEffectMaterialUnderWindowBackground = 21,
  NSVisualEffectMaterialHeaderView = 10,
  NSVisualEffectMaterialSheet = 11,
  NSVisualEffectMaterialPopover = 19,
};

typedef NSInteger NSVisualEffectBlendingMode;
enum {
  NSVisualEffectBlendingModeBehindWindow = 0,
  NSVisualEffectBlendingModeWithinWindow = 1,
};

typedef NSInteger NSVisualEffectState;
enum {
  NSVisualEffectStateFollowsWindowActiveState = 0,
  NSVisualEffectStateActive = 1,
  NSVisualEffectStateInactive = 2,
};

@class NSAppearance;

NS_CLASS_AVAILABLE_MAC(10_10)
@interface NSVisualEffectView : NSView
@property NSVisualEffectMaterial material;
@property NSVisualEffectBlendingMode blendingMode;
@property NSVisualEffectState state;
@property BOOL appearsOnRowOnly;
@property(strong) NSAppearance* appearance NS_AVAILABLE_MAC(10_9);
@end

// NSApplication appearance (10.9) and NSAppearance currentAppearance.
NS_CLASS_AVAILABLE_MAC(10_9)
@interface NSAppearance : NSObject
@end

@interface NSApplication (PowerFoxAppearanceCompat)
@property(strong) NSAppearance* appearance API_AVAILABLE(macos(10.9));
@property(readonly, strong) NSAppearance* effectiveAppearance
    API_AVAILABLE(macos(10.14));
@end

@interface NSAppearance (PowerFoxAppearanceCompat)
@property(class, strong) NSAppearance* currentAppearance API_AVAILABLE(macos(10.9));
@end

@interface NSStatusItem (PowerFoxButtonCompat)
@property(readonly, strong) NSButton* button API_AVAILABLE(macos(10.10));
@end

@interface NSNumber (PowerFoxValueCompat)
- (int)intValue;
@end

extern "C" {
enum {
  kCGScrollWheelEventScrollPhase = 99,
  kCGScrollWheelEventScrollCount = 42,
  kCGScrollWheelEventScrollDeltaAxis1 = 43,
  kCGScrollWheelEventMomentumPhase = 123,
  kCGEventUnacceleratedPointerMovementX = 177,
  kCGEventUnacceleratedPointerMovementY = 178,
};
}

@interface NSWorkspace (PowerFoxWorkspace10_10)
- (BOOL)openURLs:(NSArray*)urls
    withAppBundleIdentifier:(NSString*)bundleIdentifier
                   options:(NSWorkspaceLaunchOptions)options
additionalEventParamDescriptor:(NSAppleEventDescriptor*)descriptor
         launchIdentifiers:(NSArray**)identifiers;
@end

#if __has_include(<CoreLocation/CLLocationManager.h>)
#import <CoreLocation/CLLocation.h>
#import <CoreLocation/CLLocationManager.h>
typedef NSInteger CLAuthorizationStatus;
enum {
  kCLAuthorizationStatusNotDetermined = 0,
  kCLAuthorizationStatusRestricted = 1,
  kCLAuthorizationStatusDenied = 2,
  kCLAuthorizationStatusAuthorized = 3,
  kCLAuthorizationStatusAuthorizedAlways = 3,
};
@interface CLLocationManager (PowerFoxCLCompat)
- (CLAuthorizationStatus)authorizationStatus;
@end
#endif

typedef NSInteger NSImageResizingMode;
enum {
  NSImageResizingModeAutomatic = 0,
  NSImageResizingModeTile = 1,
  NSImageResizingModeStretch = 2,
};

@interface NSImage (PowerFoxImageCompat)
@property NSImageResizingMode resizingMode;
@end

@interface NSWorkspace (PowerFoxA11yCompat)
@property(readonly) BOOL accessibilityDisplayShouldReduceTransparency
    NS_AVAILABLE_MAC(10_10);
@property(readonly) BOOL accessibilityDisplayShouldInvertColors
    NS_AVAILABLE_MAC(10_10);
@property(readonly) BOOL accessibilityDisplayShouldIncreaseContrast
    NS_AVAILABLE_MAC(10_10);
@end

extern "C" NSString* const
    NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
        NS_AVAILABLE_MAC(10_8);
extern "C" NSString* const NSAccessibilityPopoverRole NS_AVAILABLE_MAC(10_8);

typedef NSInteger NSModalResponse;

#define NSVerticalGlyphFormAttributeName @"NSVerticalGlyphForm"

// AVFoundation does not exist in the 10.6 SDK; declare the pieces used by
// nsCocoaUtils (methods come from its own pre-10.14 categories).
#if !__has_include(<AVFoundation/AVFoundation.h>)
typedef NSString* AVMediaType;
// 10_8 rather than the true 10_7 so the references weak-link: the compile
// floor is 10.7 (thread_local), so 10_7 availability would produce hard
// references that cannot resolve on 10.6.
extern AVMediaType const AVMediaTypeVideo NS_AVAILABLE_MAC(10_8);
extern AVMediaType const AVMediaTypeAudio NS_AVAILABLE_MAC(10_8);
typedef NSInteger AVAuthorizationStatus;
enum {
  AVAuthorizationStatusNotDetermined = 0,
  AVAuthorizationStatusRestricted = 1,
  AVAuthorizationStatusDenied = 2,
  AVAuthorizationStatusAuthorized = 3,
};
NS_CLASS_AVAILABLE_MAC(10_8)
@interface AVCaptureDevice : NSObject
@end
#endif

// Property spellings on existing classes that the pre-Lion SDK declares
// only as methods (or not at all). Dot-syntax needs the @property form.
typedef NSInteger NSWindowAnimationBehavior;
enum {
  NSWindowAnimationBehaviorDefault = 0,
  NSWindowAnimationBehaviorNone = 2,
  NSWindowAnimationBehaviorDocumentWindow = 3,
  NSWindowAnimationBehaviorUtilityWindow = 4,
  NSWindowAnimationBehaviorAlertPanel = 5,
};

@class NSDraggingSession;

// NSUserInterfaceLayoutDirection already exists in the 10.6 SDK.

@interface NSWindow (PowerFoxPropertyCompat)
@property(readonly, getter=isMiniaturized) BOOL miniaturized;
@property(readonly, getter=isZoomed) BOOL zoomed;
@property(readonly, getter=isVisible) BOOL visible;
@property(readonly, getter=isOnActiveSpace) BOOL onActiveSpace;
@property NSWindowAnimationBehavior animationBehavior;
@property(copy) NSString* title;
@property BOOL restorable;
@property(strong) NSAppearance* appearance NS_AVAILABLE_MAC(10_9);
@property(readonly) NSUserInterfaceLayoutDirection
    windowTitlebarLayoutDirection NS_AVAILABLE_MAC(10_12);
@property(strong) NSUserActivity* userActivity NS_AVAILABLE_MAC(10_10);
@end

@interface NSView (PowerFoxPropertyCompat)
@property BOOL wantsLayer;
@property BOOL needsDisplay;
@property CGFloat alphaValue;
@property(getter=isHidden) BOOL hidden;
@end

// The 10.6 SDK already declares the enum in NSRunningApplication.h.
@interface NSApplication (PowerFoxPropertyCompat2)
@property(readonly, getter=isActive) BOOL active;
@property NSApplicationActivationPolicy activationPolicy;
@property NSMenu* windowsMenu;
@property NSInteger userInterfaceLayoutDirection;
@property(readonly) NSArray* windows;
@property(readonly) NSWindow* keyWindow;
@property(readonly) NSWindow* mainWindow;
@property NSAppearance* appearance;
@end

@interface NSMenu (PowerFoxLayoutCompat)
@property NSInteger userInterfaceLayoutDirection;
@end

typedef NSInteger NSDraggingContext;
enum {
  NSDraggingContextOutsideApplication = 0,
  NSDraggingContextWithinApplication = 1,
};

// The 10.6 SDK declares these only as methods on NSDraggingSource etc.;
// the modern protocol spellings need the formal declaration.
@protocol NSDraggingSource10_7 <NSObject>
@optional
- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context;
- (BOOL)ignoreModifierKeysForDraggingSession:(NSDraggingSession*)session;
- (void)draggingSession:(NSDraggingSession*)session
    endedAtPoint:(NSPoint)screenPoint
          operation:(NSDragOperation)operation;
- (void)draggingSession:(NSDraggingSession*)session
    movedToPoint:(NSPoint)screenPoint;
- (void)draggingSession:(NSDraggingSession*)session
    willBeginAtPoint:(NSPoint)screenPoint;
@end

// Print and text-alignment spellings.
enum {
  NSPaperOrientationPortrait = 0,
  NSPaperOrientationLandscape = 1,
};
enum {
  NSModalResponseOK = 1,
  NSModalResponseCancel = 0,
  NSModalResponseStop = -1000,
  NSModalResponseContinue = -1001,
  NSModalResponseAbort = -1002,
};
#define NSTextAlignmentLeft NSLeftTextAlignment
#define NSTextAlignmentRight NSRightTextAlignment
#define NSTextAlignmentCenter NSCenterTextAlignment
#define NSTextAlignmentJustified NSJustifiedTextAlignment
#define NSTextAlignmentNatural NSNaturalTextAlignment

@interface NSScreen (PowerFoxEDRCompat)
@property(readonly) CGFloat maximumPotentialExtendedDynamicRangeColorComponentValue
    API_AVAILABLE(macos(10.15));
@property(readonly) CGFloat maximumExtendedDynamicRangeColorComponentValue
    API_AVAILABLE(macos(10.15));
@end

enum {
  NSWindowCollectionBehaviorFullScreenPrimary = 1 << 7,
  NSWindowCollectionBehaviorFullScreenAuxiliary = 1 << 8,
  NSWindowCollectionBehaviorFullScreenNone = 1 << 9,
  NSWindowCollectionBehaviorFullScreenAllowsTiling = 1 << 11,
  NSWindowCollectionBehaviorFullScreenDisallowsTiling = 1 << 12,
};

typedef NSInteger NSPopoverBehavior;
enum {
  NSPopoverBehaviorApplicationDefined = 0,
  NSPopoverBehaviorTransient = 1,
  NSPopoverBehaviorSemitransient = 2,
};

typedef NSInteger NSLayoutConstraintOrientation;
enum {
  NSLayoutConstraintOrientationHorizontal = 0,
  NSLayoutConstraintOrientationVertical = 1,
};

enum {
  NSDraggingItemEnumerationConcurrent = 1,
};

// The NSOpenGL profile attribute requires 10.7; requesting it on 10.6
// yields a legacy-profile context via the compat fallback.
enum {
  NSOpenGLPFAOpenGLProfile = 99,
  NSOpenGLProfileVersionLegacy = 0x1000,
  NSOpenGLProfileVersion3_2Core = 0x3200,
  NSOpenGLProfileVersion4_1Core = 0x4100,
};

typedef NSInteger NSCorrectionIndicatorType;
enum {
  NSCorrectionIndicatorTypeDefault = 0,
};

enum {
  NSUserNotificationActivationTypeNone = 0,
  NSUserNotificationActivationTypeContentsClicked = 1,
  NSUserNotificationActivationTypeActionButtonClicked = 2,
  NSUserNotificationActivationTypeReplied = 3,
  NSUserNotificationActivationTypeAdditionalActionClicked = 4,
};

NSString* const NSPreferredScrollerStyleDidChangeNotification =
    @"NSPreferredScrollerStyleDidChangeNotification";

NSString* const NSAppearanceNameAqua = @"NSAppearanceNameAqua";
NSString* const NSPasteboardNameFind = @"NSPasteboardNameFind";


@class NSSharingService;

@protocol NSSharingServiceDelegate <NSObject>
@optional
- (void)sharingService:(NSSharingService*)sharingService
         didShareItems:(NSArray*)items;
- (void)sharingService:(NSSharingService*)sharingService
    didFailToShareItems:(NSArray*)items
                  error:(NSError*)error;
@end

NS_CLASS_AVAILABLE_MAC(10_8)
@interface NSSharingService : NSObject
@property(readonly, copy) NSString* menuItemTitle;
@property(readonly, strong) NSImage* image;
@property(assign) id<NSSharingServiceDelegate> delegate;
+ (NSArray*)sharingServicesForItems:(NSArray*)items;
+ (NSSharingService*)sharingServiceNamed:(NSString*)serviceName;
- (void)setSubject:(NSString*)subject;
- (void)performWithItems:(NSArray*)items;
@end
extern NSString* const NSSharingServiceNamePostOnTwitter NS_AVAILABLE_MAC(10_8);

NS_CLASS_AVAILABLE_MAC(10_14)
@interface NSMenuItemBadge : NSObject
- (instancetype)initWithString:(NSString*)string;
@end

@interface NSMenuItem (PowerFoxMenuItemBadge)
@property(strong) NSMenuItemBadge* badge;
@end

NS_CLASS_AVAILABLE_MAC(10_8)
@interface NSTitlebarAccessoryViewController : NSViewController
@property NSInteger layoutAttribute;
@property BOOL hidden;
@end
NSString* const NSAccessibilityAnnouncementRequestedNotification =
    @"AXAnnouncementRequested";

@protocol NSPopoverDelegate <NSObject>
@end

@protocol NSDraggingSource <NSObject>
@end

@protocol NSDraggingDestination <NSObject>
@end

@protocol NSStandardKeyBindingResponding <NSObject>
@end

@protocol NSUserNotificationCenterDelegate <NSObject>
@end

NS_CLASS_AVAILABLE_MAC(10_15)
@interface NSWorkspaceOpenConfiguration : NSObject
@property(copy) NSArray* arguments;
@property BOOL createsNewApplicationInstance;
@property(copy) NSDictionary* environment;
+ (instancetype)configuration;
@end

NS_CLASS_AVAILABLE_MAC(10_8)
@interface NSPopover : NSResponder
@property NSPopoverBehavior behavior;
@property(assign) id<NSPopoverDelegate> delegate;
@property(copy) NSViewController* contentViewController;
@property(readonly, getter=isShown) BOOL shown;
@property NSSize contentSize;
- (void)showRelativeToRect:(NSRect)positioningRect
                    ofView:(NSView*)positioningView
             preferredEdge:(NSRectEdge)preferredEdge;
- (void)close;
@end

NS_CLASS_AVAILABLE_MAC(10_8)
@interface NSDraggingItem : NSObject
- (id)initWithPasteboardWriter:(id<NSPasteboardWriting>)pasteboardWriter;
- (void)setDraggingFrame:(NSRect)frame contents:(id)contents;
@end

NS_CLASS_AVAILABLE_MAC(10_8)
@interface NSDraggingSession : NSObject
@property BOOL animatesToStartingPositionsOnCancelOrFail;
- (void)enumerateDraggingItemsWithOptions:(NSUInteger)enumOpts
                                  forView:(NSView*)view
                                  classes:(NSArray*)classArray
                            searchOptions:(NSDictionary*)searchOptions
                               usingBlock:
                                   (void (^)(NSDraggingItem* draggingItem,
                                             NSInteger idx, BOOL* stop))block;
@end

@interface NSWindow (NSWindow10_7)
@property BOOL restorable;
- (void)disableSnapshotRestoration;
- (void)toggleFullScreen:(id)sender;
@property(readonly) CGFloat backingScaleFactor;
- (NSRect)convertRectFromScreen:(NSRect)aRect;
- (NSRect)convertRectToScreen:(NSRect)aRect;
@end

@interface NSView (NSView10_7)
- (NSDraggingSession*)beginDraggingSessionWithItems:(NSArray*)items
                                               event:(NSEvent*)event
                                              source:(id)source;
- (void)setContentHuggingPriority:(float)priority
                   forOrientation:(NSLayoutConstraintOrientation)orientation;
@end

@interface NSEvent (NSEvent10_7)
@property(readonly) NSEventPhase phase;
@property(readonly) NSEventPhase momentumPhase;
@property(readonly) CGFloat scrollingDeltaX;
@property(readonly) CGFloat scrollingDeltaY;
@property(readonly) BOOL hasPreciseScrollingDeltas;
+ (BOOL)isSwipeTrackingFromScrollEventsEnabled;
@end

@interface NSColor (NSColor10_7)
+ (NSColor*)colorWithSRGBRed:(CGFloat)red
                       green:(CGFloat)green
                        blue:(CGFloat)blue
                       alpha:(CGFloat)alpha;
@end

@interface NSSpellChecker (NSSpellChecker10_7)
+ (BOOL)isAutomaticTextReplacementEnabled;
+ (BOOL)isAutomaticQuoteSubstitutionEnabled;
+ (BOOL)isAutomaticDashSubstitutionEnabled;
- (void)showCorrectionIndicatorOfType:(NSCorrectionIndicatorType)type
                         primaryString:(NSString*)primaryString
                      alternativeStrings:(NSArray*)alternativeStrings
                         forStringInRect:(NSRect)rect
                                     view:(NSView*)view
                        completionHandler:(void (^)(NSString* acceptedString))handler;
@end

@interface NSArray (NSArray10_7)
- (id)firstObject;
@end

@interface NSCell (NSCell10_7)
- (void)drawFocusRingMaskWithFrame:(NSRect)cellFrame inView:(NSView*)controlView;
@end

@interface NSTextCheckingResult (NSTextCheckingResult10_8)
@property(readonly) NSArray* alternativeStrings;
@end

typedef NSInteger NSUserNotificationActivationType;

NS_CLASS_AVAILABLE_MAC(10_8)
@interface NSUserNotificationAction : NSObject
@property(readonly, copy) NSString* identifier;
@property(readonly, copy) NSString* title;
@end

@interface NSUserNotification : NSObject
@property(copy) NSString* title;
@property(copy) NSString* subtitle;
@property(copy) NSString* informativeText;
@property(copy) NSDictionary* userInfo;
@property(readonly) NSUserNotificationActivationType activationType;
@property(copy) NSUserNotificationAction* additionalActivationAction;
@property(copy) NSArray* additionalActions;
@property BOOL hasActionButton;
@property(copy) NSString* actionButtonTitle;
@property(copy) NSImage* contentImage;
@property(copy) NSString* soundName;
@end

@interface NSUserNotificationCenter : NSObject
+ (NSUserNotificationCenter*)defaultUserNotificationCenter;
@property(assign) id<NSUserNotificationCenterDelegate> delegate;
@end

typedef NSInteger AXCustomContentImportance;
enum {
  AXCustomContentImportanceDefault = 0,
  AXCustomContentImportanceLow = 1,
  AXCustomContentImportanceHigh = 2,
};

NS_CLASS_AVAILABLE_MAC(11_0)
@interface AXCustomContent : NSObject
@property AXCustomContentImportance importance;
+ (AXCustomContent*)customContentWithLabel:(NSString*)label
                                     value:(NSString*)value;
@end

typedef NSString* NSAccessibilityRole;
typedef NSString* NSAccessibilitySubrole;

NS_CLASS_AVAILABLE_MAC(10_13)
@interface NSAccessibilityCustomAction : NSObject
- (instancetype)initWithName:(NSString*)name
                     target:(id)target
                   selector:(SEL)selector;
@end

typedef NSInteger NSAccessibilityPriorityLevel;
enum {
  NSAccessibilityPriorityLow = 10,
  NSAccessibilityPriorityMedium = 50,
  NSAccessibilityPriorityHigh = 90,
};

extern "C" {
extern NSAccessibilitySubrole const NSAccessibilityToggleSubrole
    NS_AVAILABLE_MAC(10_9);
extern NSAccessibilitySubrole const NSAccessibilitySwitchSubrole
    NS_AVAILABLE_MAC(10_9);
extern NSString* const NSAccessibilityMarkedMisspelledTextAttribute
    NS_AVAILABLE_MAC(10_4);
extern NSString* const NSAccessibilityAnnouncementKey NS_AVAILABLE_MAC(10_8);
extern NSString* const NSAccessibilityPriorityKey NS_AVAILABLE_MAC(10_9);
void NSAccessibilityPostNotificationWithUserInfo(
    id element, NSString* notification, NSDictionary* userInfo)
    __attribute__((weak_import));
extern NSString* const NSUserNotificationDefaultSoundName
    NS_AVAILABLE_MAC(10_8);
}

#endif

#if !defined(MAC_OS_X_VERSION_10_12_2) || MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_12_2

@class NSTouchBar;

@interface NSView (NSView10_12_2)
- (NSTouchBar*)makeTouchBar;
@end

#endif

#if !defined(MAC_OS_X_VERSION_10_13) || MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_13

using NSAppearanceName = NSString*;

@interface NSColor (NSColor10_13)
// "Available in 10.10", but not present in any SDK less than 10.13
@property(class, strong, readonly) NSColor* systemPurpleColor NS_AVAILABLE_MAC(10_10);
@end

@interface NSTask (NSTask10_13)
@property(copy) NSURL* executableURL NS_AVAILABLE_MAC(10_13);
@property(copy) NSArray* arguments;
- (BOOL)launchAndReturnError:(NSError**)error NS_AVAILABLE_MAC(10_13);
@end

#endif

#if !defined(MAC_OS_X_VERSION_10_14) || MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_14

@protocol NSAppearanceCustomization <NSObject>
@end

const NSAppearanceName NSAppearanceNameDarkAqua = @"NSAppearanceNameDarkAqua";

@interface NSWindow (NSWindow10_14)
@property(weak) NSObject<NSAppearanceCustomization>* appearanceSource NS_AVAILABLE_MAC(10_14);
@end

@interface NSAppearance (NSAppearance10_14)
- (NSAppearanceName)bestMatchFromAppearancesWithNames:(NSArray*)appearances
    NS_AVAILABLE_MAC(10_14);
@end

@interface NSColor (NSColor10_14)
@property(class, strong, readonly) NSColor* underPageBackgroundColor;
@property(class, strong, readonly)
    NSColor* unemphasizedSelectedContentBackgroundColor;
// Available in 10.10, but retroactively made public in 10.14.
@property(class, strong, readonly) NSColor* linkColor NS_AVAILABLE_MAC(10_10);
@end

#endif

#if !defined(MAC_OS_VERSION_11_0) || MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_VERSION_11_0
// The declarations below do not have NS_AVAILABLE_MAC(11_0) on them because we're building with a
// pre-macOS 11 SDK, so macOS 11 identifies itself as 10.16, and @available(macOS 11.0, *) checks
// won't work. You'll need to use an annoying double-whammy check for these:
//
// #if !defined(MAC_OS_VERSION_11_0) || MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_VERSION_11_0
//   if (nsCocoaFeatures::OnBigSurOrLater()) {
// #else
//   if (@available(macOS 11.0, *)) {
// #endif
//     ...
//   }
//

typedef NS_ENUM(NSInteger, NSTitlebarSeparatorStyle) {
  NSTitlebarSeparatorStyleAutomatic,
  NSTitlebarSeparatorStyleNone,
  NSTitlebarSeparatorStyleLine,
  NSTitlebarSeparatorStyleShadow
};

@interface NSWindow (NSWindow11_0)
@property NSTitlebarSeparatorStyle titlebarSeparatorStyle;
@end

@interface NSMenu (NSMenu11_0)
// In reality, NSMenu implements the NSAppearanceCustomization protocol, and picks up the appearance
// property from that protocol. But we can't tack on protocol implementations, so we just declare
// the property setter here.
- (void)setAppearance:(NSAppearance*)appearance;
@end

#endif

#if !defined(MAC_OS_VERSION_12_0) || MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_VERSION_12_0

typedef CFTypeRef AXTextMarkerRef;
typedef CFTypeRef AXTextMarkerRangeRef;

extern "C" {
CFTypeID AXTextMarkerGetTypeID();
AXTextMarkerRef AXTextMarkerCreate(CFAllocatorRef allocator, const UInt8* bytes, CFIndex length);
const UInt8* AXTextMarkerGetBytePtr(AXTextMarkerRef text_marker);
CFIndex AXTextMarkerGetLength(AXTextMarkerRef text_marker);
CFTypeID AXTextMarkerRangeGetTypeID();
AXTextMarkerRangeRef AXTextMarkerRangeCreate(CFAllocatorRef allocator, AXTextMarkerRef start_marker,
                                             AXTextMarkerRef end_marker);
AXTextMarkerRef AXTextMarkerRangeCopyStartMarker(AXTextMarkerRangeRef text_marker_range);
AXTextMarkerRef AXTextMarkerRangeCopyEndMarker(AXTextMarkerRangeRef text_marker_range);
}

@interface NSScreen (NSScreen12_0)
// https://developer.apple.com/documentation/appkit/nsscreen/3882821-safeareainsets?language=objc&changes=latest_major
@property(readonly) NSEdgeInsets safeAreaInsets;
@end

#endif

#endif  // SDKDefines_h
