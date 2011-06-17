// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/bookmarks/bookmark_button_cell.h"

#include "app/mac/nsimage_cache.h"
#include "base/logging.h"
#include "base/sys_string_conversions.h"
#import "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/metrics/user_metrics.h"
#import "chrome/browser/ui/cocoa/bookmarks/bookmark_menu.h"
#import "chrome/browser/ui/cocoa/bookmarks/bookmark_button.h"
#import "chrome/browser/ui/cocoa/image_utils.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util_mac.h"


@interface BookmarkButtonCell(Private)
- (void)configureBookmarkButtonCell;
@end


@implementation BookmarkButtonCell

@synthesize startingChildIndex = startingChildIndex_;
@synthesize drawFolderArrow = drawFolderArrow_;

+ (id)buttonCellForNode:(const BookmarkNode*)node
            contextMenu:(NSMenu*)contextMenu
               cellText:(NSString*)cellText
              cellImage:(NSImage*)cellImage {
  id buttonCell =
      [[[BookmarkButtonCell alloc] initForNode:node
                                   contextMenu:contextMenu
                                      cellText:cellText
                                     cellImage:cellImage]
       autorelease];
  return buttonCell;
}

- (id)initForNode:(const BookmarkNode*)node
      contextMenu:(NSMenu*)contextMenu
         cellText:(NSString*)cellText
        cellImage:(NSImage*)cellImage {
  if ((self = [super initTextCell:cellText])) {
    [self configureBookmarkButtonCell];

    [self setBookmarkNode:node];

    if (node) {
      NSString* title = base::SysUTF16ToNSString(node->GetTitle());
      [self setBookmarkCellText:title image:cellImage];
      [self setMenu:contextMenu];
    } else {
      [self setEmpty:YES];
      [self setBookmarkCellText:l10n_util::GetNSString(IDS_MENU_EMPTY_SUBMENU)
                          image:nil];
    }
  }

  return self;
}

- (id)initTextCell:(NSString*)string {
  return [self initForNode:nil contextMenu:nil cellText:string cellImage:nil];
}

// Used by the off-the-side menu, the only case where a
// BookmarkButtonCell is loaded from a nib.
- (void)awakeFromNib {
  [self configureBookmarkButtonCell];
}

- (BOOL)isFolderButtonCell {
  return NO;
}

// Perform all normal init routines specific to the BookmarkButtonCell.
- (void)configureBookmarkButtonCell {
  [self setButtonType:NSMomentaryPushInButton];
  [self setBezelStyle:NSShadowlessSquareBezelStyle];
  [self setShowsBorderOnlyWhileMouseInside:YES];
  [self setControlSize:NSSmallControlSize];
  [self setAlignment:NSLeftTextAlignment];
  [self setFont:[NSFont systemFontOfSize:[NSFont smallSystemFontSize]]];
  [self setWraps:NO];
  // NSLineBreakByTruncatingMiddle seems more common on OSX but let's
  // try to match Windows for a bit to see what happens.
  [self setLineBreakMode:NSLineBreakByTruncatingTail];

  // Theming doesn't work for bookmark buttons yet (cell text is chucked).
  [super setShouldTheme:NO];
}

- (BOOL)empty {
  return empty_;
}

- (void)setEmpty:(BOOL)empty {
  empty_ = empty;
  [self setShowsBorderOnlyWhileMouseInside:!empty];
}

- (NSSize)cellSizeForBounds:(NSRect)aRect {
  NSSize size = [super cellSizeForBounds:aRect];
  // Cocoa seems to slightly underestimate how much space we need, so we
  // compensate here to avoid a clipped rendering.
  size.width += 2;
  size.height += 4;
  return size;
}

- (void)setBookmarkCellText:(NSString*)title
                      image:(NSImage*)image {
  title = [title stringByReplacingOccurrencesOfString:@"\n"
                                           withString:@" "];
  title = [title stringByReplacingOccurrencesOfString:@"\r"
                                           withString:@" "];
  // If there is no title, squeeze things tight by displaying only the image; by
  // default, Cocoa leaves extra space in an attempt to display an empty title.
  if ([title length]) {
    [self setImagePosition:NSImageLeft];
    [self setTitle:title];
  } else {
    [self setImagePosition:NSImageOnly];
  }

  if (image)
    [self setImage:image];
}

- (void)setBookmarkNode:(const BookmarkNode*)node {
  [self setRepresentedObject:[NSValue valueWithPointer:node]];
}

- (const BookmarkNode*)bookmarkNode {
  return static_cast<const BookmarkNode*>([[self representedObject]
                                            pointerValue]);
}

// We share the context menu among all bookmark buttons.  To allow us
// to disambiguate when needed (e.g. "open bookmark"), we set the
// menu's associated bookmark node ID to be our represented object.
- (NSMenu*)menu {
  if (empty_)
    return nil;
  BookmarkMenu* menu = (BookmarkMenu*)[super menu];
  const BookmarkNode* node =
      static_cast<const BookmarkNode*>([[self representedObject] pointerValue]);

  if (node->parent() && node->parent()->type() == BookmarkNode::FOLDER) {
    UserMetrics::RecordAction(UserMetricsAction("BookmarkBarFolder_CtxMenu"));
  } else {
    UserMetrics::RecordAction(UserMetricsAction("BookmarkBar_CtxMenu"));
  }

  [menu setRepresentedObject:[NSNumber numberWithLongLong:node->id()]];

  return menu;
}

// Unfortunately, NSCell doesn't already have something like this.
// TODO(jrg): consider placing in GTM.
- (void)setTextColor:(NSColor*)color {

  // We can't properly set the cell's text color without a control.
  // In theory we could just save the next for later and wait until
  // the cell is moved to a control, but there is no obvious way to
  // accomplish that (e.g. no "cellDidMoveToControl" notification.)
  DCHECK([self controlView]);

  scoped_nsobject<NSMutableParagraphStyle> style([NSMutableParagraphStyle new]);
  [style setAlignment:NSLeftTextAlignment];
  NSDictionary* dict = [NSDictionary
                         dictionaryWithObjectsAndKeys:color,
                         NSForegroundColorAttributeName,
                         [self font], NSFontAttributeName,
                         style.get(), NSParagraphStyleAttributeName,
                         nil];
  scoped_nsobject<NSAttributedString> ats([[NSAttributedString alloc]
                                            initWithString:[self title]
                                                attributes:dict]);
  NSButton* button = static_cast<NSButton*>([self controlView]);
  if (button) {
    DCHECK([button isKindOfClass:[NSButton class]]);
    [button setAttributedTitle:ats.get()];
  }
}

// To implement "hover open a bookmark button to open the folder"
// which feels like menus, we override NSButtonCell's mouseEntered:
// and mouseExited:, then and pass them along to our owning control.
// Note: as verified in a debugger, mouseEntered: does NOT increase
// the retainCount of the cell or its owning control.
- (void)mouseEntered:(NSEvent*)event {
  [super mouseEntered:event];
  [[self controlView] mouseEntered:event];
}

// See comment above mouseEntered:, above.
- (void)mouseExited:(NSEvent*)event {
  [[self controlView] mouseExited:event];
  [super mouseExited:event];
}

- (void)setDrawFolderArrow:(BOOL)draw {
  drawFolderArrow_ = draw;
  if (draw && !arrowImage_) {
    arrowImage_.reset(
        [app::mac::GetCachedImageWithName(@"menu_hierarchy_arrow.pdf") retain]);
  }
}

// Add extra size for the arrow so it doesn't overlap the text.
// Does not sanity check to be sure this is actually a folder node.
- (NSSize)cellSize {
  NSSize cellSize = [super cellSize];
  if (drawFolderArrow_) {
    cellSize.width += [arrowImage_ size].width;  // plus margin?
  }
  return cellSize;
}

// Override cell drawing to add a submenu arrow like a real menu.
- (void)drawInteriorWithFrame:(NSRect)cellFrame inView:(NSView*)controlView {
  // First draw "everything else".
  [super drawInteriorWithFrame:cellFrame inView:controlView];

  // If asked to do so, and if a folder, draw the arrow.
  if (!drawFolderArrow_)
    return;
  BookmarkButton* button = static_cast<BookmarkButton*>([self controlView]);
  DCHECK([button respondsToSelector:@selector(isFolder)]);
  if ([button isFolder]) {
    NSRect imageRect = NSZeroRect;
    imageRect.size = [arrowImage_ size];
    const CGFloat kArrowOffset = 1.0;  // Required for proper centering.
    CGFloat dX = NSWidth(cellFrame) - NSWidth(imageRect);
    CGFloat dY = (NSHeight(cellFrame) / 2.0) - (NSHeight(imageRect) / 2.0) +
        kArrowOffset;
    NSRect drawRect = NSOffsetRect(imageRect, dX, dY);
    [arrowImage_ drawInRect:drawRect
                    fromRect:imageRect
                   operation:NSCompositeSourceOver
                    fraction:[self isEnabled] ? 1.0 : 0.5
                neverFlipped:YES];
  }
}

@end
