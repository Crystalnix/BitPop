// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_IMAGE_BUTTON_CELL_H_
#define CHROME_BROWSER_UI_COCOA_IMAGE_BUTTON_CELL_H_

#import <Cocoa/Cocoa.h>

#include "base/memory/scoped_nsobject.h"

namespace image_button_cell {

// Possible states
enum ButtonState {
  kDefaultState = 0,
  kHoverState,
  kPressedState,
  kDisabledState,
  // The same as above, but for non-main, non-key windows.
  kDefaultStateBackground,
  kHoverStateBackground,
  kButtonStateCount
};

} // namespace ImageButtonCell

@protocol ImageButton
@optional
// Sent from an ImageButtonCell to its view when the mouse enters or exits the
// cell.
- (void)mouseInsideStateDidChange:(BOOL)isInside;
@end

// A button cell that can disable a different image for each possible button
// state. Images are specified by image IDs.
@interface ImageButtonCell : NSButtonCell {
 @private
  scoped_nsobject<NSImage> image_[image_button_cell::kButtonStateCount];
  NSInteger overlayImageID_;
  BOOL isMouseInside_;
}

@property(assign, nonatomic) NSInteger overlayImageID;
@property(assign, nonatomic) BOOL isMouseInside;

// Sets the image for the given button state using an image ID.
// The image will be loaded from a resource pak.
- (void)setImageID:(NSInteger)imageID
    forButtonState:(image_button_cell::ButtonState)state;

// Sets the image for the given button state using an image.
- (void)setImage:(NSImage*)image
  forButtonState:(image_button_cell::ButtonState)state;

@end

#endif // CHROME_BROWSER_UI_COCOA_IMAGE_BUTTON_CELL_H_
