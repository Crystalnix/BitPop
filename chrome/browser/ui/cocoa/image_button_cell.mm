// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/image_button_cell.h"

#include "base/logging.h"
#import "chrome/browser/themes/theme_service.h"
#import "chrome/browser/ui/cocoa/image_utils.h"
#import "chrome/browser/ui/cocoa/themed_window.h"
#include "ui/gfx/image/image.h"

namespace {

// Adjust the overlay position relative to the top right of the button image.
const CGFloat kOverlayOffsetX = -3;
const CGFloat kOverlayOffsetY = 5;

// When the window doesn't have focus then we want to draw the button with a
// slightly lighter color. We do this by just reducing the alpha.
const CGFloat kImageNoFocusAlpha = 0.65;

} // namespace

@interface ImageButtonCell (Private)
- (void)sharedInit;
- (image_button_cell::ButtonState)currentButtonState;
- (NSImage*)imageForID:(NSInteger)imageID
           controlView:(NSView*)controlView;
@end

@implementation ImageButtonCell

@synthesize isMouseInside = isMouseInside_;
@synthesize overlayImageID = overlayImageID_;

// For nib instantiations
- (id)initWithCoder:(NSCoder*)decoder {
  if ((self = [super initWithCoder:decoder])) {
    [self sharedInit];
  }
  return self;
}

// For programmatic instantiations
- (id)initTextCell:(NSString*)string {
  if ((self = [super initTextCell:string])) {
    [self sharedInit];
  }
  return self;
}

- (void)sharedInit {
  [self setHighlightsBy:NSNoCellMask];

  // We need to set this so that we can override |-mouseEntered:| and
  // |-mouseExited:| to change the button image on hover states.
  [self setShowsBorderOnlyWhileMouseInside:YES];
}

- (void)drawWithFrame:(NSRect)cellFrame inView:(NSView*)controlView {
  BOOL windowHasFocus = [[controlView window] isMainWindow] ||
                        [[controlView window] isKeyWindow];
  CGFloat alpha = windowHasFocus ? 1.0 : kImageNoFocusAlpha;

  NSImage* image = [self imageForID:imageID_[[self currentButtonState]]
                        controlView:controlView];
  NSRect imageRect;
  imageRect.size = [image size];
  imageRect.origin.x = cellFrame.origin.x +
    roundf((cellFrame.size.width - imageRect.size.width) / 2.0);
  imageRect.origin.y = cellFrame.origin.y +
    roundf((cellFrame.size.height - imageRect.size.height) / 2.0);

  [image drawInRect:imageRect
           fromRect:NSZeroRect
          operation:NSCompositeSourceOver
           fraction:alpha
       neverFlipped:YES];

  if (overlayImageID_) {
    NSImage* overlayImage = [self imageForID:overlayImageID_
                                 controlView:controlView];
    NSRect overlayRect;
    overlayRect.size = [overlayImage size];
    overlayRect.origin.x = NSMaxX(imageRect) - overlayRect.size.width +
                           kOverlayOffsetX;
    overlayRect.origin.y = NSMinY(imageRect) + kOverlayOffsetY;

    [overlayImage drawInRect:overlayRect
                    fromRect:NSZeroRect
                   operation:NSCompositeSourceOver
                    fraction:1.0
                neverFlipped:YES];
  }
}

- (void)setImageID:(NSInteger)imageID
    forButtonState:(image_button_cell::ButtonState)state {
  DCHECK_GE(state, 0);
  DCHECK_LT(state, image_button_cell::kButtonStateCount);
  if (imageID_[state] != imageID) {
    imageID_[state] = imageID;
    [[self controlView] setNeedsDisplay:YES];
  }
}

- (void)setOverlayImageID:(NSInteger)imageID {
  if (overlayImageID_ != imageID) {
    overlayImageID_ = imageID;
    [[self controlView] setNeedsDisplay:YES];
  }
}

- (image_button_cell::ButtonState)currentButtonState {
  if (![self isEnabled] && imageID_[image_button_cell::kDisabledState])
    return image_button_cell::kDisabledState;
  else if ([self isHighlighted] && imageID_[image_button_cell::kPressedState])
    return image_button_cell::kPressedState;
  else if ([self isMouseInside] && imageID_[image_button_cell::kHoverState])
    return image_button_cell::kHoverState;
  else
    return image_button_cell::kDefaultState;
}

- (NSImage*)imageForID:(NSInteger)imageID
           controlView:(NSView*)controlView {
  if (!imageID)
    return nil;

  ui::ThemeProvider* themeProvider = [[controlView window] themeProvider];
  if (!themeProvider)
    return nil;

  return themeProvider->GetNSImageNamed(imageID, true);
}

- (void)setIsMouseInside:(BOOL)isMouseInside {
  if (isMouseInside_ != isMouseInside) {
    isMouseInside_ = isMouseInside;
    NSView<ImageButton>* control =
        static_cast<NSView<ImageButton>*>([self controlView]);
    if ([control respondsToSelector:@selector(mouseInsideStateDidChange:)]) {
      [control mouseInsideStateDidChange:isMouseInside];
    }
    [control setNeedsDisplay:YES];
  }
}

- (void)setShowsBorderOnlyWhileMouseInside:(BOOL)show {
  VLOG_IF(1, !show) << "setShowsBorderOnlyWhileMouseInside:NO ignored";
}

- (BOOL)showsBorderOnlyWhileMouseInside {
  // Always returns YES so that buttons always get mouse tracking even when
  // disabled. The reload button (and possibly others) depend on this.
  return YES;
}

- (void)mouseEntered:(NSEvent*)theEvent {
  [self setIsMouseInside:YES];
}

- (void)mouseExited:(NSEvent*)theEvent {
  [self setIsMouseInside:NO];
}

@end
