// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/panels/panel_titlebar_view_cocoa.h"

#import <Cocoa/Cocoa.h>

#include "base/logging.h"
#include "base/mac/scoped_nsautorelease_pool.h"
#include "chrome/browser/themes/theme_service.h"
#import "chrome/browser/ui/cocoa/hover_image_button.h"
#import "chrome/browser/ui/cocoa/nsview_additions.h"
#import "chrome/browser/ui/cocoa/panels/panel_window_controller_cocoa.h"
#import "chrome/browser/ui/cocoa/themed_window.h"
#import "chrome/browser/ui/cocoa/tracking_area.h"
#import "chrome/browser/ui/panels/panel_constants.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#import "third_party/GTM/AppKit/GTMNSBezierPath+RoundRect.h"
#import "third_party/GTM/AppKit/GTMNSColor+Luminance.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/mac/nsimage_cache.h"
#include "ui/gfx/scoped_ns_graphics_context_save_gstate_mac.h"
#include "ui/gfx/image/image.h"

const int kButtonPadding = 8;
const int kIconAndTextPadding = 5;

// 'Glint' is a speck of light that moves across the titlebar to attract a bit
// more attention using movement in addition to color of the titlebar.
// It initially moves fast, then starts to slow down to avoid being annoying
// if the user chooses not to react on it.
const double kGlintAnimationDuration = 0.6;
const double kStartGlintRepeatIntervalSeconds = 0.1;
const double kFinalGlintRepeatIntervalSeconds = 2.0;
const double kGlintRepeatIntervalIncreaseFactor = 1.5;

// Used to implement TestingAPI
static NSEvent* MakeMouseEvent(NSEventType type,
                               NSPoint point,
                               int modifierFlags,
                               int clickCount) {
  return [NSEvent mouseEventWithType:type
                            location:point
                       modifierFlags:modifierFlags
                           timestamp:0
                        windowNumber:0
                             context:nil
                         eventNumber:0
                          clickCount:clickCount
                            pressure:0.0];
}

// Test drag controller - does not contain a nested message loop, directly
// invokes the dragStarted/dragProgress instead.
@interface TestDragController : MouseDragController {
 @private
  BOOL dragStarted_;
}
- (void)mouseDragged:(NSEvent*)event;
@end

@implementation TestDragController
// Bypass nested message loop for tests. There is no need to check for
// threshold here as the base class does because tests only simulate a single
// 'mouse drag' to the destination point.
- (void)mouseDragged:(NSEvent*)event {
  if (!dragStarted_) {
    [[self client] dragStarted:[self initialMouseLocation]];
    dragStarted_ = YES;
  }

  [[self client] dragProgress:[event locationInWindow]];
}
@end

@implementation PanelTitlebarOverlayView
// Sometimes we do not want to bring chrome window to foreground when we click
// on any part of the titlebar. To do this, we first postpone the window
// reorder here (shouldDelayWindowOrderingForEvent is called during when mouse
// button is pressed but before mouseDown: is dispatched) and then complete
// canceling the reorder by [NSApp preventWindowOrdering] in mouseDown handler
// of this view.
- (BOOL)shouldDelayWindowOrderingForEvent:(NSEvent*)theEvent {
  disableReordering_ = ![controller_ canBecomeKeyWindow];
  return disableReordering_;
}

- (void)mouseDown:(NSEvent*)event {
  if (disableReordering_)
    [NSApp preventWindowOrdering];
  disableReordering_ = NO;
  // Continue bubbling the event up the chain of responders.
  [super mouseDown:event];
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  return YES;
}
@end

@implementation RepaintAnimation
- (id)initWithView:(NSView*)targetView duration:(double) duration {
  if ((self = [super initWithDuration:duration
                       animationCurve:NSAnimationEaseInOut])) {
    [self setAnimationBlockingMode:NSAnimationNonblocking];
    targetView_ = targetView;
  }
  return self;
}

- (void)setCurrentProgress:(NSAnimationProgress)progress {
  [super setCurrentProgress:progress];
  [targetView_ setNeedsDisplay:YES];
}
@end


@implementation PanelTitlebarViewCocoa

- (id)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame]))
    dragController_.reset([[MouseDragController alloc] initWithClient:self]);
  return self;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [self stopGlintAnimation];
  [super dealloc];
}

- (void)onCloseButtonClick:(id)sender {
  [controller_ closePanel];
}

- (void)onMinimizeButtonClick:(id)sender {
  [controller_ minimizeButtonClicked:[[NSApp currentEvent] modifierFlags]];
}

- (void)onRestoreButtonClick:(id)sender {
  [controller_ restoreButtonClicked:[[NSApp currentEvent] modifierFlags]];
}

- (void)drawRect:(NSRect)rect {
  ThemeService* theme =
      static_cast<ThemeService*>([[self window] themeProvider]);

  NSColor* titleColor = nil;

  if (isDrawingAttention_) {
    // Use system highlight color for DrawAttention state.
    NSColor* attentionColor = [NSColor selectedTextBackgroundColor];
    [attentionColor set];
    NSRectFillUsingOperation([self bounds], NSCompositeSourceOver);
    // Cover it with semitransparent gradient for better look.
    NSColor* startColor = [NSColor colorWithCalibratedWhite:1.0 alpha:0.2];
    NSColor* endColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.2];
    scoped_nsobject<NSGradient> gradient(
      [[NSGradient alloc] initWithStartingColor:startColor
                                    endingColor:endColor]);
    [gradient drawInRect:[self bounds] angle:270.0];

    titleColor = [attentionColor gtm_legibleTextColor];

    if ([glintAnimation_ isAnimating]) {
      scoped_nsobject<NSGradient> glint([NSGradient alloc]);
      NSColor* gold =
          [NSColor colorWithCalibratedRed:0.8 green:0.8 blue:0.0 alpha:1.0];
      [glint initWithColorsAndLocations:gold, 0.0,
           [NSColor colorWithCalibratedWhite:1.0 alpha:0.4], 0.3,
           [NSColor colorWithCalibratedWhite:1.0 alpha:0.0], 1.0,
           nil];
      NSRect bounds = [self bounds];
      NSPoint point = bounds.origin;
      // The size and position values are experimentally choosen to create
      // a "speck of light attached to the top edge" effect.
      int gradientRadius = NSHeight(bounds) * 2;
      point.y += gradientRadius / 2;
      double rangeOfMotion = NSWidth(bounds) + 4 * gradientRadius;
      double startPoint = - 2 * gradientRadius;
      point.x = startPoint + rangeOfMotion * [glintAnimation_ currentValue];
      [glint drawFromCenter:point
                     radius:0.0
                   toCenter:point
                     radius:gradientRadius
                    options:NSGradientDrawsBeforeStartingLocation];
    }
  } else if (theme && !theme->UsingDefaultTheme()) {
    NSColor* backgroundColor = nil;
    if ([[self window] isMainWindow]) {
      backgroundColor = theme->GetNSImageColorNamed(IDR_THEME_TOOLBAR, true);
    } else {
      // Based on -[TabView drawRect:], we need to check if the theme has an
      // IDR_THEME_TAB_BACKGROUND or IDR_THEME_FRAME resource; otherwise,
      // we'll potentially end up with a bizarre looking blue background for
      // inactive tabs, which looks really out of place on a Mac.
      if (theme->HasCustomImage(IDR_THEME_TAB_BACKGROUND) ||
          theme->HasCustomImage(IDR_THEME_FRAME)) {
        backgroundColor =
            theme->GetNSImageColorNamed(IDR_THEME_TAB_BACKGROUND, true);
      } else {
        backgroundColor = [[self window] backgroundColor];  // Fallback.
      }
    }
    // Fill with white to avoid bleeding the system titlebar through
    // semitransparent theme images.
    [[NSColor whiteColor] set];
    NSRectFill([self bounds]);

    NSPoint phase = [[self window] themePatternPhase];
    [[NSGraphicsContext currentContext] setPatternPhase:phase];
    DCHECK(backgroundColor);
    [backgroundColor set];
    NSRectFillUsingOperation([self bounds], NSCompositeSourceOver);

    titleColor = [[self window] isMainWindow]
        ? theme->GetNSColor(ThemeService::COLOR_TAB_TEXT, true)
        : theme->GetNSColor(ThemeService::COLOR_BACKGROUND_TAB_TEXT, true);
  } else {
    // Default theme or no theme.
    BOOL isActive = [[self window] isMainWindow];

    // If titlebar is close to minimized state or is at minimized state and only
    // shows a few pixels, change the color to something light and add border.
    NSRect windowFrame = [[self window] frame];
    if (NSHeight(windowFrame) < 8) {
      NSColor* lightBackgroundColor =
          [NSColor colorWithCalibratedRed:0xf5/255.0
                                    green:0xf4/255.0
                                     blue:0xf0/255.0
                                    alpha:1.0];
      [lightBackgroundColor set];
      NSRectFill([self bounds]);

      NSColor* borderColor =
          [NSColor colorWithCalibratedRed:0xc9/255.0
                                    green:0xc9/255.0
                                     blue:0xc9/255.0
                                    alpha:1.0];
      [borderColor set];
      NSFrameRect([self bounds]);
    } else {
      // use solid black-ish colors.
      NSColor* backgroundColor = isActive ?
        [NSColor colorWithCalibratedRed:0x3a/255.0
                                  green:0x3d/255.0
                                   blue:0x3d/255.0
                                  alpha:1.0] :
        [NSColor colorWithCalibratedRed:0x7a/255.0
                                  green:0x7c/255.0
                                   blue:0x7c/255.0
                                  alpha:1.0];

      [backgroundColor set];
      NSRectFill([self bounds]);
    }

    titleColor = [NSColor colorWithCalibratedRed:0xf9/255.0
                                           green:0xf9/255.0
                                            blue:0xf9/255.0
                                           alpha:1.0];
  }

  DCHECK(titleColor);
  [title_ setTextColor:titleColor];
}

- (void)attach {
  // Interface Builder can not put a view as a sibling of contentView,
  // so need to do it here. Placing ourself as the last child of the
  // internal view allows us to draw on top of the titlebar.
  // Note we must use [controller_ window] here since we have not been added
  // to the view hierarchy yet.
  NSView* contentView = [[controller_ window] contentView];
  NSView* rootView = [contentView superview];
  [rootView addSubview:self];

  // Figure out the rectangle of the titlebar and set us on top of it.
  // The titlebar covers window's root view where not covered by contentView.
  // Compute the titlebar frame in coordinate system of the window's root view.
  //        NSWindow
  //           |
  //    ___root_view____
  //     |            |
  // contentView  titlebar
  NSSize titlebarSize = NSMakeSize(0, panel::kTitlebarHeight);
  titlebarSize = [contentView convertSize:titlebarSize toView:rootView];
  NSRect rootViewBounds = [[self superview] bounds];
  NSRect titlebarFrame =
      NSMakeRect(NSMinX(rootViewBounds),
                 NSMaxY(rootViewBounds) - titlebarSize.height,
                 NSWidth(rootViewBounds),
                 titlebarSize.height);
  [self setFrame:titlebarFrame];

  [title_ setFont:[[NSFontManager sharedFontManager]
                   fontWithFamily:@"Arial"
                           traits:NSBoldFontMask
                           weight:0
                             size:14.0]];
  [title_ setDrawsBackground:NO];

  ResourceBundle& rb = ResourceBundle::GetSharedInstance();

  [self initializeImageButton:customCloseButton_
      image:rb.GetNativeImageNamed(IDR_PANEL_CLOSE).ToNSImage()
      hoverImage:rb.GetNativeImageNamed(IDR_PANEL_CLOSE_H).ToNSImage()
      pressedImage:rb.GetNativeImageNamed(IDR_PANEL_CLOSE_C).ToNSImage()
      toolTip:l10n_util::GetNSStringWithFixup(IDS_PANEL_CLOSE_TOOLTIP)];

  // Iniitalize the minimize and restore buttons.
  [self initializeImageButton:minimizeButton_
      image:rb.GetNativeImageNamed(IDR_PANEL_MINIMIZE).ToNSImage()
      hoverImage:rb.GetNativeImageNamed(IDR_PANEL_MINIMIZE_H).ToNSImage()
      pressedImage:rb.GetNativeImageNamed(IDR_PANEL_MINIMIZE_C).ToNSImage()
      toolTip:l10n_util::GetNSStringWithFixup(IDS_PANEL_MINIMIZE_TOOLTIP)];

  [self initializeImageButton:restoreButton_
      image:rb.GetNativeImageNamed(IDR_PANEL_RESTORE).ToNSImage()
      hoverImage:rb.GetNativeImageNamed(IDR_PANEL_RESTORE_H).ToNSImage()
      pressedImage:rb.GetNativeImageNamed(IDR_PANEL_RESTORE_C).ToNSImage()
      toolTip:l10n_util::GetNSStringWithFixup(IDS_PANEL_RESTORE_TOOLTIP)];

  [controller_ updateTitleBarMinimizeRestoreButtonVisibility];

  [self updateCustomButtonsLayout];

  // Set autoresizing behavior: glued to edges on left, top and right.
  [self setAutoresizingMask:(NSViewMinYMargin | NSViewWidthSizable)];

  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(didChangeTheme:)
             name:kBrowserThemeDidChangeNotification
           object:nil];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(didChangeFrame:)
             name:NSViewFrameDidChangeNotification
           object:self];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(didChangeMainWindow:)
             name:NSWindowDidBecomeMainNotification
           object:[self window]];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(didChangeMainWindow:)
             name:NSWindowDidResignMainNotification
           object:[self window]];
}

- (void)initializeImageButton:(HoverImageButton*)button
                        image:(NSImage*)image
                   hoverImage:(NSImage*)hoverImage
                 pressedImage:(NSImage*)pressedImage
                      toolTip:(NSString*)toolTip {
  [button setDefaultImage:image];
  [button setDefaultOpacity:1.0];
  [button setHoverImage:hoverImage];
  [button setHoverOpacity:1.0];
  [button setPressedImage:pressedImage];
  [button setPressedOpacity:1.0];
  [button setToolTip:toolTip];
  [[button cell] setHighlightsBy:NSNoCellMask];
}

- (void)setTitle:(NSString*)newTitle {
  [title_ setStringValue:newTitle];
  [self updateIconAndTitleLayout];
}

- (void)setIcon:(NSView*)newIcon {
  [icon_ removeFromSuperview];
  icon_ = newIcon;
  if (icon_) {
    [self addSubview:icon_ positioned:NSWindowBelow relativeTo:overlay_];
    [icon_ setWantsLayer:YES];
  }
  [self updateIconAndTitleLayout];
}

- (NSView*)icon {
  return icon_;
}

- (void)setMinimizeButtonVisibility:(BOOL)visible {
  [minimizeButton_ setHidden:!visible];
}

- (void)setRestoreButtonVisibility:(BOOL)visible {
  [restoreButton_ setHidden:!visible];
}

- (void)updateCustomButtonsLayout {
  NSRect bounds = [self bounds];
  NSRect closeButtonFrame = [customCloseButton_ frame];
  closeButtonFrame.origin.x =
      NSWidth(bounds) - NSWidth(closeButtonFrame) - kButtonPadding;
  closeButtonFrame.origin.y =
      (NSHeight(bounds) - NSHeight(closeButtonFrame)) / 2;
  [customCloseButton_ setFrame:closeButtonFrame];

  NSRect buttonFrame = [minimizeButton_ frame];
  buttonFrame.origin.x =
      closeButtonFrame.origin.x - NSWidth(buttonFrame) - kButtonPadding;
  buttonFrame.origin.y = (NSHeight(bounds) - NSHeight(buttonFrame)) / 2;
  [minimizeButton_ setFrame:buttonFrame];
  [restoreButton_ setFrame:buttonFrame];
}

- (void)updateIconAndTitleLayout {
  NSRect iconFrame = [icon_ frame];
  // NSTextField for title_ is set to Layout:Truncate, LineBreaks:TruncateTail
  // in Interface Builder so it is sized in a single-line mode.
  [title_ sizeToFit];
  NSRect titleFrame = [title_ frame];
  // Only one of minimize/restore button is visible at a time so just allow for
  // the width of one of them.
  NSRect minimizeRestoreButtonFrame = [minimizeButton_ frame];
  NSRect bounds = [self bounds];

  // Place the icon and title at the left edge of the titlebar.
  int iconWidth = NSWidth(iconFrame);
  int titleWidth = NSWidth(titleFrame);
  int availableWidth = minimizeRestoreButtonFrame.origin.x - kButtonPadding;

  if (2 * kIconAndTextPadding + iconWidth + titleWidth > availableWidth)
    titleWidth = availableWidth - iconWidth - 2 * kIconAndTextPadding;
  if (titleWidth < 0)
    titleWidth = 0;

  iconFrame.origin.x = kIconAndTextPadding;
  iconFrame.origin.y = (NSHeight(bounds) - NSHeight(iconFrame)) / 2;
  [icon_ setFrame:iconFrame];

  titleFrame.origin.x = 2 * kIconAndTextPadding + iconWidth;
  // In bottom-heavy text labels, let's compensate for occasional integer
  // rounding to avoid text label to feel too low.
  titleFrame.origin.y = (NSHeight(bounds) - NSHeight(titleFrame)) / 2 + 2;
  titleFrame.size.width = titleWidth;
  [title_ setFrame:titleFrame];
}

// PanelManager controls size/position of the window.
- (BOOL)mouseDownCanMoveWindow {
  return NO;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  return YES;
}

- (void)didChangeFrame:(NSNotification*)notification {
  // Update buttons first because title layout depends on buttons layout.
  [self updateCustomButtonsLayout];
  [self updateIconAndTitleLayout];
}

- (void)didChangeTheme:(NSNotification*)notification {
  [self setNeedsDisplay:YES];
}

- (void)didChangeMainWindow:(NSNotification*)notification {
  [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event {
  [dragController_ mouseDown:event];
}

- (void)mouseUp:(NSEvent*)event {
  [dragController_ mouseUp:event];

  if ([event clickCount] == 1)
    [controller_ onTitlebarMouseClicked:[event modifierFlags]];
}

- (void)mouseDragged:(NSEvent*)event {
  [dragController_ mouseDragged:event];
}

// MouseDragControllerClient implementaiton

- (void)prepareForDrag {
}

- (void)dragStarted:(NSPoint)initialMouseLocation {
  NSPoint initialMouseLocationScreen =
      [[self window] convertBaseToScreen:initialMouseLocation];
  [controller_ startDrag:initialMouseLocationScreen];
}

- (void)dragEnded:(BOOL)cancelled {
  [controller_ endDrag:cancelled];
}

- (void)dragProgress:(NSPoint)mouseLocation {
  NSPoint mouseLocationScreen =
      [[self window] convertBaseToScreen:mouseLocation];
  [controller_ drag:mouseLocationScreen];
}

- (void)cleanupAfterDrag {
}

// End of MouseDragControllerClient implementaiton

- (void)drawAttention {
  if (isDrawingAttention_)
    return;
  isDrawingAttention_ = YES;

  [self startGlintAnimation];
}

- (void)stopDrawingAttention {
  if (!isDrawingAttention_)
    return;
  isDrawingAttention_ = NO;

  [self stopGlintAnimation];
  [self setNeedsDisplay:YES];
}

- (BOOL)isDrawingAttention {
  return isDrawingAttention_;
}

- (void)startGlintAnimation {
  glintInterval_ = kStartGlintRepeatIntervalSeconds;
  [self restartGlintAnimation:nil];
}

- (void)stopGlintAnimation {
  if (glintAnimationTimer_.get()) {
    [glintAnimationTimer_ invalidate];
    glintAnimationTimer_.reset();
  }
  if ([glintAnimation_ isAnimating])
    [glintAnimation_ stopAnimation];
}

- (void)restartGlintAnimation:(NSTimer*)timer {
  if (!glintAnimation_.get()) {
    glintAnimation_.reset(
        [[RepaintAnimation alloc] initWithView:self
                                      duration:kGlintAnimationDuration]);
    [glintAnimation_ setDelegate:self];
  }
  [glintAnimation_ startAnimation];
}

- (void)animationDidEnd:(NSAnimation*)animation {
  if (animation == glintAnimation_.get()) {  // Restart after a timeout.
    glintAnimationTimer_.reset([[NSTimer
        scheduledTimerWithTimeInterval:glintInterval_
                                target:self
                              selector:@selector(restartGlintAnimation:)
                              userInfo:nil
                               repeats:NO] retain]);
    // Gradually reduce the frequency of repeating the animation,
    // calming it down if user decides not to act upon it.
    if (glintInterval_ < kFinalGlintRepeatIntervalSeconds)
      glintInterval_ *= kGlintRepeatIntervalIncreaseFactor;
  }
}

// (Private/TestingAPI)
- (PanelWindowControllerCocoa*)controller {
  return controller_;
}

- (NSTextField*)title {
  return title_;
}

- (void)simulateCloseButtonClick {
  [[customCloseButton_ cell] performClick:customCloseButton_];
}

- (void)pressLeftMouseButtonTitlebar:(NSPoint)mouseLocation
                           modifiers:(int)modifierFlags {
  // Override the drag controller. It's ok to create a new one for each drag.
  dragController_.reset([[TestDragController alloc] initWithClient:self]);
  // Convert from Cocoa's screen coordinates to base coordinates since the mouse
  // event takes base (NSWindow) coordinates.
  NSPoint mouseLocationWindow =
      [[self window] convertScreenToBase:mouseLocation];
  NSEvent* event = MakeMouseEvent(NSLeftMouseDown, mouseLocationWindow,
      modifierFlags, 0);
  [self mouseDown:event];
}

- (void)releaseLeftMouseButtonTitlebar:(int)modifierFlags {
  NSEvent* event = MakeMouseEvent(NSLeftMouseUp, NSZeroPoint, modifierFlags, 1);
  [self mouseUp:event];
}

- (void)dragTitlebar:(NSPoint)mouseLocation {
  // Convert from Cocoa's screen coordinates to base coordinates since the mouse
  // event takes base (NSWindow) coordinates.
  NSPoint mouseLocationWindow =
      [[self window] convertScreenToBase:mouseLocation];
  NSEvent* event =
      MakeMouseEvent(NSLeftMouseDragged, mouseLocationWindow, 0, 0);
  [self mouseDragged:event];
}

- (void)cancelDragTitlebar {
  [self dragEnded:YES];
}

- (void)finishDragTitlebar {
  [self dragEnded:NO];
}

- (NSButton*)closeButton {
  return closeButton_;
}

- (NSButton*)minimizeButton {
  return minimizeButton_;
}

- (NSButton*)restoreButton {
  return restoreButton_;
}

@end

