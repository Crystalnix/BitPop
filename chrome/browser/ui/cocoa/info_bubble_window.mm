// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/info_bubble_window.h"

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/memory/scoped_nsobject.h"
#include "content/common/notification_observer.h"
#include "content/common/notification_registrar.h"
#include "content/common/notification_service.h"
#include "content/common/notification_type.h"
#import "third_party/GTM/AppKit/GTMNSAnimation+Duration.h"

namespace {
const CGFloat kOrderInSlideOffset = 10;
const NSTimeInterval kOrderInAnimationDuration = 0.2;
const NSTimeInterval kOrderOutAnimationDuration = 0.15;
// The minimum representable time interval.  This can be used as the value
// passed to +[NSAnimationContext setDuration:] to stop an in-progress
// animation as quickly as possible.
const NSTimeInterval kMinimumTimeInterval =
    std::numeric_limits<NSTimeInterval>::min();
}

@interface InfoBubbleWindow(Private)
- (void)appIsTerminating;
- (void)finishCloseAfterAnimation;
@end

// A helper class to proxy app notifications to the window.
class AppNotificationBridge : public NotificationObserver {
 public:
  explicit AppNotificationBridge(InfoBubbleWindow* owner) : owner_(owner) {
    registrar_.Add(this, NotificationType::APP_TERMINATING,
                   NotificationService::AllSources());
  }

  // Overridden from NotificationObserver.
  void Observe(NotificationType type,
               const NotificationSource& source,
               const NotificationDetails& details) {
    switch (type.value) {
      case NotificationType::APP_TERMINATING:
        [owner_ appIsTerminating];
        break;
      default:
        NOTREACHED() << L"Unexpected notification";
    }
  }

 private:
  // The object we need to inform when we get a notification. Weak. Owns us.
  InfoBubbleWindow* owner_;

  // Used for registering to receive notifications and automatic clean up.
  NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(AppNotificationBridge);
};

// A delegate object for watching the alphaValue animation on InfoBubbleWindows.
// An InfoBubbleWindow instance cannot be the delegate for its own animation
// because CAAnimations retain their delegates, and since the InfoBubbleWindow
// retains its animations a retain loop would be formed.
@interface InfoBubbleWindowCloser : NSObject {
 @private
  InfoBubbleWindow* window_;  // Weak. Window to close.
}
- (id)initWithWindow:(InfoBubbleWindow*)window;
@end

@implementation InfoBubbleWindowCloser

- (id)initWithWindow:(InfoBubbleWindow*)window {
  if ((self = [super init])) {
    window_ = window;
  }
  return self;
}

// Callback for the alpha animation. Closes window_ if appropriate.
- (void)animationDidStop:(CAAnimation*)anim finished:(BOOL)flag {
  // When alpha reaches zero, close window_.
  if ([window_ alphaValue] == 0.0) {
    [window_ finishCloseAfterAnimation];
  }
}

@end


@implementation InfoBubbleWindow

@synthesize delayOnClose = delayOnClose_;

- (id)initWithContentRect:(NSRect)contentRect
                styleMask:(NSUInteger)aStyle
                  backing:(NSBackingStoreType)bufferingType
                    defer:(BOOL)flag {
  if ((self = [super initWithContentRect:contentRect
                               styleMask:NSBorderlessWindowMask
                                 backing:bufferingType
                                   defer:flag])) {
    [self setBackgroundColor:[NSColor clearColor]];
    [self setExcludedFromWindowsMenu:YES];
    [self setOpaque:NO];
    [self setHasShadow:YES];
    delayOnClose_ = YES;
    notificationBridge_.reset(new AppNotificationBridge(self));

    // Start invisible. Will be made visible when ordered front.
    [self setAlphaValue:0.0];

    // Set up alphaValue animation so that self is delegate for the animation.
    // Setting up the delegate is required so that the
    // animationDidStop:finished: callback can be handled.
    // Notice that only the alphaValue Animation is replaced in case
    // superclasses set up animations.
    CAAnimation* alphaAnimation = [CABasicAnimation animation];
    scoped_nsobject<InfoBubbleWindowCloser> delegate(
        [[InfoBubbleWindowCloser alloc] initWithWindow:self]);
    [alphaAnimation setDelegate:delegate];
    NSMutableDictionary* animations =
        [NSMutableDictionary dictionaryWithDictionary:[self animations]];
    [animations setObject:alphaAnimation forKey:@"alphaValue"];
    [self setAnimations:animations];
  }
  return self;
}

// According to
// http://www.cocoabuilder.com/archive/message/cocoa/2006/6/19/165953,
// NSBorderlessWindowMask windows cannot become key or main. In this
// case, this is not a desired behavior. As an example, the bubble could have
// buttons.
- (BOOL)canBecomeKeyWindow {
  return YES;
}

- (void)close {
  // Block the window from receiving events while it fades out.
  closing_ = YES;

  if (!delayOnClose_) {
    [self finishCloseAfterAnimation];
  } else {
    // Apply animations to hide self.
    [NSAnimationContext beginGrouping];
    [[NSAnimationContext currentContext]
        gtm_setDuration:kOrderOutAnimationDuration
              eventMask:NSLeftMouseUpMask];
    [[self animator] setAlphaValue:0.0];
    [NSAnimationContext endGrouping];
  }
}

// If the app is terminating but the window is still fading out, cancel the
// animation and close the window to prevent it from leaking.
// See http://crbug.com/37717
- (void)appIsTerminating {
  if (!delayOnClose_)
    return;  // The close has already happened with no Core Animation.

  // Cancel the current animation so that it closes immediately, triggering
  // |finishCloseAfterAnimation|.
  [NSAnimationContext beginGrouping];
  [[NSAnimationContext currentContext] setDuration:kMinimumTimeInterval];
  [[self animator] setAlphaValue:0.0];
  [NSAnimationContext endGrouping];
}

// Called by InfoBubbleWindowCloser when the window is to be really closed
// after the fading animation is complete.
- (void)finishCloseAfterAnimation {
  if (closing_)
    [super close];
}

// Adds animation for info bubbles being ordered to the front.
- (void)orderWindow:(NSWindowOrderingMode)orderingMode
         relativeTo:(NSInteger)otherWindowNumber {
  // According to the documentation '0' is the otherWindowNumber when the window
  // is ordered front.
  if (orderingMode == NSWindowAbove && otherWindowNumber == 0) {
    // Order self appropriately assuming that its alpha is zero as set up
    // in the designated initializer.
    [super orderWindow:orderingMode relativeTo:otherWindowNumber];

    // Set up frame so it can be adjust down by a few pixels.
    NSRect frame = [self frame];
    NSPoint newOrigin = frame.origin;
    newOrigin.y += kOrderInSlideOffset;
    [self setFrameOrigin:newOrigin];

    // Apply animations to show and move self.
    [NSAnimationContext beginGrouping];
    // The star currently triggers on mouse down, not mouse up.
    [[NSAnimationContext currentContext]
        gtm_setDuration:kOrderInAnimationDuration
              eventMask:NSLeftMouseUpMask|NSLeftMouseDownMask];
    [[self animator] setAlphaValue:1.0];
    [[self animator] setFrame:frame display:YES];
    [NSAnimationContext endGrouping];
  } else {
    [super orderWindow:orderingMode relativeTo:otherWindowNumber];
  }
}

// If the window is currently animating a close, block all UI events to the
// window.
- (void)sendEvent:(NSEvent*)theEvent {
  if (!closing_)
    [super sendEvent:theEvent];
}

- (BOOL)isClosing {
  return closing_;
}

@end
