// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/location_bar/location_icon_decoration.h"

#include "base/sys_string_conversions.h"
#import "chrome/browser/bookmarks/bookmark_pasteboard_helper_mac.h"
#import "chrome/browser/ui/cocoa/location_bar/location_bar_view_mac.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "grit/generated_resources.h"
#include "skia/ext/skia_utils_mac.h"
#import "third_party/mozilla/NSPasteboard+Utils.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/l10n/l10n_util_mac.h"

using content::NavigationEntry;
using content::WebContents;

// The info-bubble point should look like it points to the bottom of the lock
// icon. Determined with Pixie.app.
const CGFloat kBubblePointYOffset = 2.0;

LocationIconDecoration::LocationIconDecoration(LocationBarViewMac* owner)
    : drag_frame_(NSZeroRect), owner_(owner) {
}

LocationIconDecoration::~LocationIconDecoration() {
}

bool LocationIconDecoration::IsDraggable() {
  // Without a tab it will be impossible to get the information needed
  // to perform a drag.
  if (!owner_->GetWebContents())
    return false;

  // Do not drag if the user has been editing the location bar, or the
  // location bar is at the NTP.
  if (owner_->location_entry()->IsEditingOrEmpty())
    return false;

  return true;
}

NSPasteboard* LocationIconDecoration::GetDragPasteboard() {
  WebContents* tab = owner_->GetWebContents();
  DCHECK(tab);  // See |IsDraggable()|.

  NSString* url = base::SysUTF8ToNSString(tab->GetURL().spec());
  NSString* title = base::SysUTF16ToNSString(tab->GetTitle());

  NSPasteboard* pboard = [NSPasteboard pasteboardWithName:NSDragPboard];
  [pboard declareURLPasteboardWithAdditionalTypes:
        [NSArray arrayWithObject:NSFilesPromisePboardType]
                                            owner:nil];
  [pboard setDataForURL:url title:title];

  [pboard setPropertyList:[NSArray arrayWithObject:@"webloc"]
                  forType:NSFilesPromisePboardType];

  return pboard;
}

NSImage* LocationIconDecoration::GetDragImage() {
  const SkBitmap& favicon = owner_->GetFavicon();
  NSImage* iconImage = (!favicon.isNull()) ?
      gfx::SkBitmapToNSImage(favicon) : GetImage();

  NSImage* image = bookmark_pasteboard_helper_mac::DragImageForBookmark(
      iconImage, owner_->GetTitle());
  NSSize imageSize = [image size];
  drag_frame_ = NSMakeRect(0, 0, imageSize.width, imageSize.height);
  return image;
}

NSRect LocationIconDecoration::GetDragImageFrame(NSRect frame) {
  // If GetDragImage has never been called, drag_frame_ has not been calculated.
  if (NSIsEmptyRect(drag_frame_))
    GetDragImage();
  return drag_frame_;
}

NSPoint LocationIconDecoration::GetBubblePointInFrame(NSRect frame) {
  const NSRect draw_frame = GetDrawRectInFrame(frame);
  return NSMakePoint(NSMidX(draw_frame),
                     NSMaxY(draw_frame) - kBubblePointYOffset);
}

bool LocationIconDecoration::AcceptsMousePress() {
  return true;
}

bool LocationIconDecoration::OnMousePressed(NSRect frame) {
  // Do not show page info if the user has been editing the location
  // bar, or the location bar is at the NTP.
  if (owner_->location_entry()->IsEditingOrEmpty())
    return true;

  WebContents* tab = owner_->GetWebContents();
  NavigationEntry* nav_entry = tab->GetController().GetActiveEntry();
  if (!nav_entry) {
    NOTREACHED();
    return true;
  }
  tab->ShowPageInfo(nav_entry->GetURL(), nav_entry->GetSSL(), true);
  return true;
}

NSString* LocationIconDecoration::GetToolTip() {
  if (owner_->location_entry()->IsEditingOrEmpty())
    return nil;
  else
    return l10n_util::GetNSStringWithFixup(IDS_TOOLTIP_LOCATION_ICON);
}
