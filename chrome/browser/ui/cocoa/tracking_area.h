// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_TRACKING_AREA_H_
#define CHROME_BROWSER_UI_COCOA_TRACKING_AREA_H_

#import <AppKit/AppKit.h>

#include "base/memory/scoped_nsobject.h"

@class CrTrackingAreaOwnerProxy;

// The CrTrackingArea can be used in place of an NSTrackingArea to shut off
// messaging to the |owner| at a specific point in time.
@interface CrTrackingArea : NSTrackingArea {
 @private
  scoped_nsobject<CrTrackingAreaOwnerProxy> ownerProxy_;
}

// Designated initializer. Forwards all arguments to the superclass, but wraps
// |owner| in a proxy object.
- (id)initWithRect:(NSRect)rect
           options:(NSTrackingAreaOptions)options
             owner:(id)owner
          userInfo:(NSDictionary*)userInfo;

// Prevents any future messages from being delivered to the |owner|.
- (void)clearOwner;

// Watches |window| for its NSWindowWillCloseNotification and calls
// |-clearOwner| when the notification is observed.
- (void)clearOwnerWhenWindowWillClose:(NSWindow*)window;

@end

// Scoper //////////////////////////////////////////////////////////////////////

// Use an instance of this class to call |-clearOwner| on the |tracking_area_|
// when this goes out of scope.
class ScopedCrTrackingArea {
 public:
  // Takes ownership of |tracking_area| without retaining it.
  explicit ScopedCrTrackingArea(CrTrackingArea* tracking_area = nil);
  ~ScopedCrTrackingArea();

  // This will call |scoped_nsobject<>::reset()| to take ownership of the new
  // tracking area.  Note that -clearOwner is NOT called on the existing
  // tracking area.
  void reset(CrTrackingArea* tracking_area = nil);

  CrTrackingArea* get() const;

 private:
  scoped_nsobject<CrTrackingArea> tracking_area_;
  DISALLOW_COPY_AND_ASSIGN(ScopedCrTrackingArea);
};

#endif  // CHROME_BROWSER_UI_COCOA_TRACKING_AREA_H_
