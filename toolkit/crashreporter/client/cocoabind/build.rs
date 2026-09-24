/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use mozbuild::config::BINDGEN_SYSTEM_FLAGS as CFLAGS;

const TYPES: &[&str] = &[
    "ActionCell",
    "Application",
    "Array",
    "AttributedString",
    "Box",
    "Button",
    "ButtonCell",
    "Cell",
    "ClassDescription",
    "Color",
    "Control",
    "DefaultRunLoopMode",
    "Dictionary",
    "ForegroundColorAttributeName",
    "LayoutDimension",
    "LayoutGuide",
    "LayoutXAxisAnchor",
    "LayoutYAxisAnchor",
    "MutableAttributedString",
    "MutableParagraphStyle",
    "MutableString",
    "ModalPanelRunLoopMode",
    "Panel",
    "ProcessInfo",
    "ProgressIndicator",
    "Proxy",
    "RunLoop",
    "ScrollView",
    "SplitView",
    "StackView",
    "String",
    "TextContainer",
    "TextField",
    "TextView",
    "Value",
    "View",
    "Window",
];

fn main() {
    // Ignore BINDGEN_SYSTEM_FLAGS' -std=gnu++## flag. cocoabind parses Cocoa.h
    // with `-x objective-c` and bindgen/libclang rejects `-std=gnu++##` (and
    // `-std=c++##`) as incompatible with Objective-C.
    let cflags: Vec<&str> = CFLAGS
        .iter()
        .copied()
        .filter(|flag| !flag.starts_with("-std=gnu++") && !flag.starts_with("-std=c++"))
        .collect();
    // The 10.6 SDK lacks the declarations (and lightweight generics) that the
    // bindings and the UI code assume.
    let is_legacy_sdk = cflags.iter().any(|f| {
        f.contains("MacOSX10.6.sdk") || f.contains("MacOSX10.5.sdk") || f.contains("overlay-sdk")
    });

    let mut builder = bindgen::Builder::default()
        .header_contents(
            "cocoa_bindings.h",
            "#define self self_
            #import <Cocoa/Cocoa.h>
            #if !defined(MAC_OS_X_VERSION_10_7) || \
                MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_7
            #define PFX_PRE_10_7_SDK 1
            // Declarations from newer SDKs that the crashreporter UI uses.
            @class NSLayoutConstraint;
            typedef float NSLayoutPriority;
            typedef NSInteger NSLayoutConstraintOrientation;
            enum {
              NSLayoutConstraintOrientationHorizontal = 0,
              NSLayoutConstraintOrientationVertical = 1,
            };
            typedef NSInteger NSUserInterfaceLayoutOrientation;
            enum {
              NSUserInterfaceLayoutOrientationHorizontal = 0,
              NSUserInterfaceLayoutOrientationVertical = 1,
            };
            typedef NSInteger NSLayoutAttribute;
            enum {
              NSLayoutAttributeNotAnAttribute = 0,
              NSLayoutAttributeLeft = 1,
              NSLayoutAttributeRight = 2,
              NSLayoutAttributeTop = 3,
              NSLayoutAttributeBottom = 4,
              NSLayoutAttributeLeading = 5,
              NSLayoutAttributeTrailing = 6,
              NSLayoutAttributeWidth = 7,
              NSLayoutAttributeHeight = 8,
              NSLayoutAttributeCenterX = 9,
              NSLayoutAttributeCenterY = 10,
              NSLayoutAttributeBaseline = 11,
            };

            @interface NSLayoutConstraint : NSObject
            @property BOOL active;
            @property CGFloat constant;
            @property NSLayoutPriority priority;
            + (void)activateConstraints:(NSArray*)constraints;
            + (NSArray*)constraintsWithVisualFormat:(NSString*)format
                                            options:(NSUInteger)opts
                                            metrics:(NSDictionary*)metrics
                                              views:(NSDictionary*)views;
            @end

            @interface NSLayoutAnchor <NSCopying>
            - (NSLayoutConstraint*)constraintEqualToAnchor:(NSLayoutAnchor*)anchor;
            - (NSLayoutConstraint*)constraintEqualToAnchor:(NSLayoutAnchor*)anchor
                                                   constant:(CGFloat)c;
            - (NSLayoutConstraint*)constraintGreaterThanOrEqualToAnchor:
                (NSLayoutAnchor*)anchor;
            - (NSLayoutConstraint*)constraintGreaterThanOrEqualToAnchor:
                (NSLayoutAnchor*)anchor constant:(CGFloat)c;
            - (NSLayoutConstraint*)constraintLessThanOrEqualToAnchor:(NSLayoutAnchor*)anchor;
            - (NSLayoutConstraint*)constraintLessThanOrEqualToAnchor:
                (NSLayoutAnchor*)anchor constant:(CGFloat)c;
            @end

            @interface NSLayoutXAxisAnchor : NSLayoutAnchor
            @end
            @interface NSLayoutYAxisAnchor : NSLayoutAnchor
            @end
            @interface NSLayoutDimension : NSLayoutAnchor
            - (NSLayoutConstraint*)constraintEqualToConstant:(CGFloat)c;
            - (NSLayoutConstraint*)constraintGreaterThanOrEqualToConstant:(CGFloat)c;
            - (NSLayoutConstraint*)constraintLessThanOrEqualToConstant:(CGFloat)c;
            - (NSLayoutConstraint*)constraintEqualToAnchor:(NSLayoutAnchor*)anchor;
            - (NSLayoutConstraint*)constraintGreaterThanOrEqualToAnchor:
                (NSLayoutAnchor*)anchor;
            - (NSLayoutConstraint*)constraintLessThanOrEqualToAnchor:(NSLayoutAnchor*)anchor;
            @end

            @interface NSView (PFXLayoutCompat)
            @property(readonly, strong) NSLayoutXAxisAnchor* leadingAnchor;
            @property(readonly, strong) NSLayoutXAxisAnchor* trailingAnchor;
            @property(readonly, strong) NSLayoutXAxisAnchor* centerXAnchor;
            @property(readonly, strong) NSLayoutYAxisAnchor* topAnchor;
            @property(readonly, strong) NSLayoutYAxisAnchor* bottomAnchor;
            @property(readonly, strong) NSLayoutYAxisAnchor* centerYAnchor;
            @property(readonly, strong) NSLayoutDimension* widthAnchor;
            @property(readonly, strong) NSLayoutDimension* heightAnchor;
            @property BOOL translatesAutoresizingMaskIntoConstraints;
            @property(readonly) NSArray* constraints;
            - (void)layoutSubtreeIfNeeded;
            - (void)setContentHuggingPriority:(NSLayoutPriority)priority
                                forOrientation:(NSLayoutConstraintOrientation)orientation;
            - (void)setUserInterfaceLayoutDirection:
                (NSUserInterfaceLayoutDirection)direction;
            @end

            @interface NSWindow (PFXKeyNavCompat)
            - (void)selectNextKeyView:(id)sender;
            - (void)selectPreviousKeyView:(id)sender;
            @end

            @interface NSColor (PFXColorCompat)
            @property(class, strong, readonly) NSColor* placeholderTextColor;
            @end

            @interface NSTextContainer (PFXCompat)
            - (void)setSize:(NSSize)size;
            @end

            @interface NSRunLoop (PFXCompat)
            - (void)performInModes:(NSArray*)modes block:(void (^)(void))block;
            @end

            @interface NSView (NSConstraintBasedLayoutInstallingConstraints)
            - (void)addConstraint:(NSLayoutConstraint*)constraint;
            - (void)removeConstraint:(NSLayoutConstraint*)constraint;
            @end

            @interface NSView (NSConstraintBasedLayoutLayering)
            - (void)setWantsLayer:(BOOL)flag;
            @end

            enum {
              NSStackViewGravityLeading = 1,
              NSStackViewGravityTop = 1,
              NSStackViewGravityCenter = 2,
              NSStackViewGravityTrailing = 3,
              NSStackViewGravityBottom = 3,
            };

            @interface NSStackView : NSView
            @property(copy) NSArray* views;
            @property CGFloat spacing;
            @property NSLayoutConstraintOrientation orientation;
            @property NSLayoutConstraintOrientation alignment;
            - (void)setHuggingPriority:(NSLayoutPriority)priority
                         forOrientation:(NSLayoutConstraintOrientation)orientation;
            @end

            @interface NSStackView (NSStackViewGravityAreas)
            - (void)addView:(NSView*)view inGravity:(NSUInteger)gravity;
            - (void)insertView:(NSView*)view atIndex:(NSUInteger)index
                      inGravity:(NSUInteger)gravity;
            @end

            @interface NSApplication (NSEvent)
            - (void)pfxAvailabilityAnchor;
            @end

            @interface NSApplication (PFXLayoutCompat)
            @property NSUserInterfaceLayoutDirection userInterfaceLayoutDirection;
            @end

            @interface NSTextField (NSTextFieldConvenience)
            + (instancetype)wrappingLabelWithString:(NSString*)string;
            @end

            @interface NSMutableParagraphStyle ()
            @end

            typedef NSInteger NSControlStateValue;
            typedef NSString* NSRunLoopMode;
            typedef NSString* NSAttributedStringKey;
            enum {
              NSTextAlignmentRight = 1,
            };
            enum {
              NSBezelStyleRounded = NSRoundedBezelStyle,
              NSButtonTypeSwitch = NSSwitchButton,
              NSEventTypeApplicationDefined = NSApplicationDefined,
              NSWindowStyleMaskTitled = NSTitledWindowMask,
              NSWindowStyleMaskClosable = NSClosableWindowMask,
              NSWindowStyleMaskMiniaturizable = NSMiniaturizableWindowMask,
              NSWindowStyleMaskResizable = NSResizableWindowMask,
            };
            #endif
            ",
        )
        .generate_block(true)
        .prepend_enum_name(false)
        .clang_args(cflags)
        .clang_args(["-x", "objective-c"])
        .clang_arg("-fblocks")
        .derive_default(true)
        .allowlist_item("TransformProcessType");
    // Constants referenced by the UI under modern spellings.
    for name in [
        "NSControlStateValue.*",
        "NSRunLoopMode",
        "NSBackingStore.*",
        "NSBezelStyle.*",
        "NSButtonType.*",
        "NSEventType.*",
        "NSNoTitle",
        "NSWindowStyleMask.*",
        "NSBezelBorder",
        "NSLayoutAttribute.*",
        "NSLayoutConstraintOrientation.*",
        "NSTextAlignment.*",
        "NSUserInterfaceLayout.*",
        "NSWritingDirection.*",
        "NSAttributedStringKey",
        "NSViewWidthSizable",
        "NSViewHeightSizable",
        "NSWindowAbove",
    ] {
        builder = builder.allowlist_item(name);
    }
    for name in TYPES {
        // (I|P) covers generated traits (interfaces and protocols). `(_.*)?` covers categories
        // (which are generated as `CLASS_CATEGORY`).
        builder = builder.allowlist_item(format!("(I|P)?NS{name}(_.*)?"));
    }
    let bindings = builder
        .generate()
        .expect("unable to generate cocoa bindings");
    println!("cargo:rustc-check-cfg=cfg(pfx_pre_10_7_sdk)");
    if is_legacy_sdk {
        println!("cargo:rustc-cfg=pfx_pre_10_7_sdk");
    }
    let out_path = std::path::PathBuf::from(std::env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("cocoa_bindings.rs"))
        .expect("failed to write cocoa bindings");
    println!("cargo:rustc-link-lib=framework=AppKit");
    println!("cargo:rustc-link-lib=framework=Cocoa");
    println!("cargo:rustc-link-lib=framework=Foundation");
}
