// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include "base/file_path.h"
#include "base/memory/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#include "googleurl/src/gurl.h"

class WebContentsImpl;
struct WebDropData;

// A class that handles tracking and event processing for a drag and drop
// originating from the content area.
@interface WebDragSource : NSObject {
 @private
  // Our contents. Weak reference (owns or co-owns us).
  WebContentsImpl* contents_;

  // The view from which the drag was initiated. Weak reference.
  NSView* contentsView_;

  // Our drop data. Should only be initialized once.
  scoped_ptr<WebDropData> dropData_;

  // The image to show as drag image. Can be nil.
  scoped_nsobject<NSImage> dragImage_;

  // The offset to draw |dragImage_| at.
  NSPoint imageOffset_;

  // Our pasteboard.
  scoped_nsobject<NSPasteboard> pasteboard_;

  // A mask of the allowed drag operations.
  NSDragOperation dragOperationMask_;

  // The file name to be saved to for a drag-out download.
  FilePath downloadFileName_;

  // The URL to download from for a drag-out download.
  GURL downloadURL_;

  // The file extension associated with the file drag, if any.
  NSString* fileExtension_;
}

// Initialize a WebDragSource object for a drag (originating on the given
// contentsView and with the given dropData and pboard). Fill the pasteboard
// with data types appropriate for dropData.
- (id)initWithContents:(WebContentsImpl*)contents
                  view:(NSView*)contentsView
              dropData:(const WebDropData*)dropData
                 image:(NSImage*)image
                offset:(NSPoint)offset
            pasteboard:(NSPasteboard*)pboard
     dragOperationMask:(NSDragOperation)dragOperationMask;

// Returns a mask of the allowed drag operations.
- (NSDragOperation)draggingSourceOperationMaskForLocal:(BOOL)isLocal;

// Call when asked to do a lazy write to the pasteboard; hook up to
// -pasteboard:provideDataForType: (on the contentsView).
- (void)lazyWriteToPasteboard:(NSPasteboard*)pboard
                      forType:(NSString*)type;

// Start the drag (on the originally provided contentsView); can do this right
// after -initWithContentsView:....
- (void)startDrag;

// End the drag and clear the pasteboard; hook up to
// -draggedImage:endedAt:operation:.
- (void)endDragAt:(NSPoint)screenPoint
        operation:(NSDragOperation)operation;

// Drag moved; hook up to -draggedImage:movedTo:.
- (void)moveDragTo:(NSPoint)screenPoint;

// Call to drag a promised file to the given path (should be called before
// -endDragAt:...); hook up to -namesOfPromisedFilesDroppedAtDestination:.
// Returns the file name (not including path) of the file deposited (or which
// will be deposited).
- (NSString*)dragPromisedFileTo:(NSString*)path;

@end
