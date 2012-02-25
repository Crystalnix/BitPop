// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/dock_icon.h"

#include <string>

#include "base/memory/scoped_nsobject.h"
#include "ui/gfx/scoped_ns_graphics_context_save_gstate_mac.h"

// The fraction of the size of the dock icon that the badge is.
static const float kBadgeFraction = 0.4f;

// The indentation of the badge.
static const float kBadgeIndent = 5.0f;

static const float kProfileImageFrameFraction = 0.45f;

// A view that draws our dock tile.
@interface DockTileView : NSView {
 @private
  int downloads_;
  BOOL indeterminate_;
  float progress_;
  NSImage *profilePhoto_;
  int messages_;
}

// Indicates how many downloads are in progress.
@property (nonatomic) int downloads;

// Indicates whether the progress indicator should be in an indeterminate state
// or not.
@property (nonatomic) BOOL indeterminate;

// Indicates the amount of progress made of the download. Ranges from [0..1].
@property (nonatomic) float progress;

@property (nonatomic,retain) NSImage* profilePhoto;

@property (nonatomic) int messages;

@end

@implementation DockTileView

@synthesize downloads = downloads_;
@synthesize indeterminate = indeterminate_;
@synthesize progress = progress_;
@synthesize profilePhoto = profilePhoto_;
@synthesize messages = messages_;

- (void)dealloc {
  if (profilePhoto_)
    [profilePhoto_ release];

  [super dealloc];
}

- (void)drawRect:(NSRect)dirtyRect {
  // Not -[NSApplication applicationIconImage]; that fails to return a pasted
  // custom icon.
  NSString* appPath = [[NSBundle mainBundle] bundlePath];
  NSImage* appIcon = [[NSWorkspace sharedWorkspace] iconForFile:appPath];
  [appIcon drawInRect:[self bounds]
             fromRect:NSZeroRect
            operation:NSCompositeSourceOver
             fraction:1.0];

  NSRect badgeRect;

  if (downloads_ != 0) {
    badgeRect = [self bounds];
    badgeRect.size.height = (int)(kBadgeFraction * badgeRect.size.height);
    int newWidth = kBadgeFraction * badgeRect.size.width;
    badgeRect.origin.x = badgeRect.size.width - newWidth;
    badgeRect.size.width = newWidth;

    CGFloat badgeRadius = NSMidY(badgeRect);

    badgeRect.origin.x -= kBadgeIndent;
    badgeRect.origin.y += kBadgeIndent;

    NSPoint badgeCenter = NSMakePoint(NSMidX(badgeRect),
                                      NSMidY(badgeRect));

    // Background
    NSColor* backgroundColor = [NSColor colorWithCalibratedRed:0.85
                                                         green:0.85
                                                          blue:0.85
                                                         alpha:1.0];
    NSColor* backgroundHighlight =
        [backgroundColor blendedColorWithFraction:0.85
                                          ofColor:[NSColor whiteColor]];
    scoped_nsobject<NSGradient> backgroundGradient(
        [[NSGradient alloc] initWithStartingColor:backgroundHighlight
                                      endingColor:backgroundColor]);
    NSBezierPath* badgeEdge = [NSBezierPath bezierPathWithOvalInRect:badgeRect];
    {
      gfx::ScopedNSGraphicsContextSaveGState scopedGState;
      [badgeEdge addClip];
      [backgroundGradient drawFromCenter:badgeCenter
                                  radius:0.0
                                toCenter:badgeCenter
                                  radius:badgeRadius
                                 options:0];
    }

    // Slice
    if (!indeterminate_) {
      NSColor* sliceColor = [NSColor colorWithCalibratedRed:0.45
                                                      green:0.8
                                                       blue:0.25
                                                      alpha:1.0];
      NSColor* sliceHighlight =
          [sliceColor blendedColorWithFraction:0.4
                                       ofColor:[NSColor whiteColor]];
      scoped_nsobject<NSGradient> sliceGradient(
          [[NSGradient alloc] initWithStartingColor:sliceHighlight
                                        endingColor:sliceColor]);
      NSBezierPath* progressSlice;
      if (progress_ >= 1.0) {
        progressSlice = [NSBezierPath bezierPathWithOvalInRect:badgeRect];
      } else {
        CGFloat endAngle = 90.0 - 360.0 * progress_;
        if (endAngle < 0.0)
          endAngle += 360.0;
        progressSlice = [NSBezierPath bezierPath];
        [progressSlice moveToPoint:badgeCenter];
        [progressSlice appendBezierPathWithArcWithCenter:badgeCenter
                                                  radius:badgeRadius
                                              startAngle:90.0
                                                endAngle:endAngle
                                               clockwise:YES];
        [progressSlice closePath];
      }
      gfx::ScopedNSGraphicsContextSaveGState scopedGState;
      [progressSlice addClip];
      [sliceGradient drawFromCenter:badgeCenter
                             radius:0.0
                           toCenter:badgeCenter
                             radius:badgeRadius
                            options:0];
    }

    // Edge
    {
      gfx::ScopedNSGraphicsContextSaveGState scopedGState;
      [[NSColor whiteColor] set];
      scoped_nsobject<NSShadow> shadow([[NSShadow alloc] init]);
      [shadow.get() setShadowOffset:NSMakeSize(0, -2)];
      [shadow setShadowBlurRadius:2];
      [shadow set];
      [badgeEdge setLineWidth:2];
      [badgeEdge stroke];
    }

    // Download count
    scoped_nsobject<NSNumberFormatter> formatter(
        [[NSNumberFormatter alloc] init]);
    NSString* countString =
        [formatter stringFromNumber:[NSNumber numberWithInt:downloads_]];

    scoped_nsobject<NSShadow> countShadow([[NSShadow alloc] init]);
    [countShadow setShadowBlurRadius:3.0];
    [countShadow.get() setShadowColor:[NSColor whiteColor]];
    [countShadow.get() setShadowOffset:NSMakeSize(0.0, 0.0)];
    NSMutableDictionary* countAttrsDict =
        [NSMutableDictionary dictionaryWithObjectsAndKeys:
            [NSColor blackColor], NSForegroundColorAttributeName,
            countShadow.get(), NSShadowAttributeName,
            nil];
    CGFloat countFontSize = badgeRadius;
    NSSize countSize = NSZeroSize;
    scoped_nsobject<NSAttributedString> countAttrString;
    while (1) {
      NSFont* countFont = [NSFont fontWithName:@"Helvetica-Bold"
                                          size:countFontSize];
      [countAttrsDict setObject:countFont forKey:NSFontAttributeName];
      countAttrString.reset(
          [[NSAttributedString alloc] initWithString:countString
                                          attributes:countAttrsDict]);
      countSize = [countAttrString size];
      if (countSize.width > badgeRadius * 1.5) {
        countFontSize -= 1.0;
      } else {
        break;
      }
    }

    NSPoint countOrigin = badgeCenter;
    countOrigin.x -= countSize.width / 2;
    countOrigin.y -= countSize.height / 2.2;  // tweak; otherwise too low

    [countAttrString.get() drawAtPoint:countOrigin];
  }  // if (downloads_ != 0) ...

  if (profilePhoto_ != nil && messages_ != 0) {
    if (downloads_ != 0) {
      NSColor* badgeFillColor = [NSColor colorWithCalibratedRed:0
                                                          green:0
                                                           blue:0
                                                          alpha:0.5];
      NSBezierPath* downloadsBadgeEdge =
          [NSBezierPath bezierPathWithOvalInRect:badgeRect];
      {
        gfx::ScopedNSGraphicsContextSaveGState scopedGState;
        [badgeFillColor set];
        [downloadsBadgeEdge fill];
      }
    }

    NSRect frameRect = [self bounds];
    CGFloat oldWidth = frameRect.size.width;
    CGFloat oldHeight = frameRect.size.height;
    frameRect.size.height =
        (int)(kProfileImageFrameFraction * frameRect.size.height);
    frameRect.size.width = (int)(kProfileImageFrameFraction * frameRect.size.width);
    frameRect.origin.x = (oldWidth - frameRect.size.width) / 2;
    frameRect.origin.y = (oldHeight - frameRect.size.height) / 2;

    NSColor* frameStrokeColor = [NSColor colorWithCalibratedRed:0
                                                          green:0.7
                                                           blue:0.7
                                                          alpha:1.0];
    NSBezierPath* profileImageFrame =
        [NSBezierPath bezierPathWithRoundedRect:frameRect
                                        xRadius:4.0
                                        yRadius:4.0];
    {
      gfx::ScopedNSGraphicsContextSaveGState scopedGState;
      [profileImageFrame setClip];
      [profilePhoto_ drawInRect:frameRect fromRect:NSZeroRect
          operation:NSCompositeCopy fraction:1.0];
    }
    {
      gfx::ScopedNSGraphicsContextSaveGState scopedGState;
      [frameStrokeColor set];
      [profileImageFrame setLineWidth:3.0];
      [profileImageFrame stroke];
    }

/*
    NSRect badgeFrameRect;
    badgeFrameRect.size.width = (int)(frameRect.size.width / 4.0);
    badgeFrameRect.size.height = (int)(frameRect.size.width * 3.0 / 8.0);
    badgeFrameRect.origin.x = frameRect.origin.x + frameRect.size.width -
        badgeFrameRect.size.width + 1.0;
    badgeFrameRect.origin.y = frameRect.origin.y - 1.0;

    // Background
    NSColor* badgeBgrColor = [NSColor colorWithCalibratedRed:0.85
                                                       green:0.85
                                                        blue:0.1
                                                       alpha:1.0];
    NSColor* badgeBgrHighlight =
        [backgroundColor blendedColorWithFraction:0.85
                                          ofColor:[NSColor whiteColor]];
    scoped_nsobject<NSGradient> badgeBackgroundGradient(
        [[NSGradient alloc] initWithStartingColor:badgeBgrHighlight
                                      endingColor:badgeBgrColor]);
    NSBezierPath* badgePath = [NSBezierPath bezierPathWithRondedRect:badgeFrameRect
                                                             xRadius:2.0
                                                             yRadius:2.0];
    {
      gfx::ScopedNSGraphicsContextSaveGState scopedGState;
      [badgeBackgroundGradient drawInBezierPath:badgePath angle:-90];
      [badgeBgrColor set];
      [badgePath setLineWidth:2.0];
      [badgePath stroke];
    }

    scoped_nsobject<NSNumberFormatter> formatter(
        [[NSNumberFormatter alloc] init]);
    NSString* countString =
        [formatter stringFromNumber:[NSNumber numberWithInt:messages_]];

    NSMutableDictionary* countAttrsDict =
        [NSMutableDictionary dictionaryWithObjectsAndKeys:
            [NSColor blackColor], NSForegroundColorAttributeName,
            nil];
    CGFloat countFontSize = badgeFrameRect.size.height;
    NSSize countSize = NSZeroSize;
    scoped_nsobject<NSAttributedString> countAttrString;
    while (1) {
      NSFont* countFont = [NSFont fontWithName:@"Helvetica-Bold"
                                          size:countFontSize];
      [countAttrsDict setObject:countFont forKey:NSFontAttributeName];
      countAttrString.reset(
          [[NSAttributedString alloc] initWithString:countString
                                          attributes:countAttrsDict]);
      countSize = [countAttrString size];
      if (countSize.width > badgeFrameRect.size.width * 1.5) {
        countFontSize -= 1.0;
      } else {
        break;
      }
    }

    NSPoint countOrigin =
        NSMakePoint(NSMidX(badgeFrameRect), NSMidY(badgeFrameRect));
    countOrigin.x -= countSize.width / 2;
    countOrigin.y -= countSize.height / 2.2;  // tweak; otherwise too low

    [countAttrString.get() drawAtPoint:countOrigin];
*/
  }
}

@end

@implementation DockIcon

+ (DockIcon*)sharedDockIcon {
  static DockIcon* icon;
  if (!icon) {
    NSDockTile* dockTile = [[NSApplication sharedApplication] dockTile];

    scoped_nsobject<DockTileView> dockTileView([[DockTileView alloc] init]);
    [dockTile setContentView:dockTileView];

    icon = [[DockIcon alloc] init];
  }

  return icon;
}

- (void)updateIcon {
  NSDockTile* dockTile = [[NSApplication sharedApplication] dockTile];

  [dockTile display];
}

- (void)setDownloads:(int)downloads {
  NSDockTile* dockTile = [[NSApplication sharedApplication] dockTile];
  DockTileView* dockTileView = (DockTileView*)([dockTile contentView]);

  [dockTileView setDownloads:downloads];
}

- (void)setIndeterminate:(BOOL)indeterminate {
  NSDockTile* dockTile = [[NSApplication sharedApplication] dockTile];
  DockTileView* dockTileView = (DockTileView*)([dockTile contentView]);

  [dockTileView setIndeterminate:indeterminate];
}

- (void)setProgress:(float)progress {
  NSDockTile* dockTile = [[NSApplication sharedApplication] dockTile];
  DockTileView* dockTileView = (DockTileView*)([dockTile contentView]);

  [dockTileView setProgress:progress];
}

- (void)setUnreadNumber:(int)unread withProfileImage:(NSImage*)image {
  NSDockTile* dockTile = [[NSApplication sharedApplication] dockTile];
  DockTileView* dockTileView = (DockTileView*)([dockTile contentView]);

  [dockTileView setMessages:unread];
  [dockTileView setProfilePhoto:image];

  if (unread != 0) {
    scoped_nsobject<NSNumberFormatter> formatter(
        [[NSNumberFormatter alloc] init]);
    NSString* countString =
        [formatter stringFromNumber:[NSNumber numberWithInt:unread]];

    [[NSApp dockTile] setBadgeLabel:countString];
    [[NSApp dockTile] setShowsApplicationBadge:YES];
  }
  else {
    [[NSApp dockTile] setShowsApplicationBadge:NO];
  }
}

@end
