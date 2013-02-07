// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chat_item_controller.h"

#include <string>

#include "base/mac/bundle_locations.h"
#include "base/mac/mac_util.h"
#include "base/sys_string_conversions.h"
#include "chrome/browser/facebook_chat/facebook_chatbar.h"
#include "chrome/browser/facebook_chat/facebook_chat_manager.h"
#include "chrome/browser/facebook_chat/facebook_chat_manager_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chatbar_controller.h"
#import "chrome/browser/ui/cocoa/facebook_chat/facebook_popup_controller.h"
#import "chrome/browser/ui/cocoa/facebook_chat/facebook_notification_controller.h"
#include "chrome/common/badge_util.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#include "skia/ext/skia_utils_mac.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "third_party/skia/include/effects/SkGradientShader.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/skia_util.h"

namespace {
  const int kButtonWidth = 178;
  const int kButtonHeight = 36;

  const CGFloat kNotificationWindowAnchorPointXOffset = 13.0;

  const CGFloat kChatWindowAnchorPointYOffset = 3.0;

  //const CGFloat kBadgeImageDim = 14;

  NSImage *availableImage = nil;
  NSImage *idleImage = nil;
  NSImage *composingImage = nil;

  const int kNotifyIconDimX = 16;
  const int kNotifyIconDimY = 11;

  const float kTextSize = 10;
  const int kBottomMargin = 0;
  const int kPadding = 2;
  // The padding between the top of the badge and the top of the text.
  const int kTopTextPadding = -1;
  const int kBadgeHeight = 11;
  const int kMaxTextWidth = 23;
  // The minimum width for center-aligning the badge.
  const int kCenterAlignThreshold = 20;

// duplicate methods (ui/gfx/canvas_skia.cc)
bool IntersectsClipRectInt(const SkCanvas& canvas, int x, int y, int w, int h) {
  SkRect clip;
  return canvas.getClipBounds(&clip) &&
      clip.intersect(SkIntToScalar(x), SkIntToScalar(y), SkIntToScalar(x + w),
                     SkIntToScalar(y + h));
}

bool ClipRectInt(SkCanvas& canvas, int x, int y, int w, int h) {
  SkRect new_clip;
  new_clip.set(SkIntToScalar(x), SkIntToScalar(y),
               SkIntToScalar(x + w), SkIntToScalar(y + h));
  return canvas.clipRect(new_clip);
}

void TileImageInt(SkCanvas& canvas, const SkBitmap& bitmap,
                  int src_x, int src_y,
                  int dest_x, int dest_y, int w, int h) {
  if (!IntersectsClipRectInt(canvas, dest_x, dest_y, w, h))
    return;

  SkPaint paint;

  SkShader* shader = SkShader::CreateBitmapShader(bitmap,
                                                  SkShader::kRepeat_TileMode,
                                                  SkShader::kRepeat_TileMode);
  paint.setShader(shader);
  paint.setXfermodeMode(SkXfermode::kSrcOver_Mode);

  // CreateBitmapShader returns a Shader with a reference count of one, we
  // need to unref after paint takes ownership of the shader.
  shader->unref();
  canvas.save();
  canvas.translate(SkIntToScalar(dest_x - src_x), SkIntToScalar(dest_y - src_y));
  ClipRectInt(canvas, src_x, src_y, w, h);
  canvas.drawPaint(paint);
  canvas.restore();
}

void TileImageInt(SkCanvas& canvas, const SkBitmap& bitmap,
                  int x, int y, int w, int h) {
  TileImageInt(canvas, bitmap, 0, 0, x, y, w, h);
}

}

@interface FacebookChatItemController(Private)
+ (NSImage*)imageForNotificationBadgeWithNumber:(int)number;
@end


@implementation FacebookChatItemController

- (id)initWithModel:(FacebookChatItem*)downloadModel
            chatbar:(FacebookChatbarController*)chatbar {
  if ((self = [super initWithNibName:@"FacebookChatItem"
                              bundle:base::mac::FrameworkBundle()])) {
    bridge_.reset(new FacebookChatItemMac(downloadModel, self));

    chatbarController_ = chatbar;
    active_ = downloadModel->needs_activation() ? YES : NO;

    showMouseEntered_ = NO;

    NSNotificationCenter* defaultCenter = [NSNotificationCenter defaultCenter];
    [[self view] setPostsFrameChangedNotifications:YES];
    [defaultCenter addObserver:self
                      selector:@selector(viewFrameDidChange:)
                          name:NSViewFrameDidChangeNotification
                        object:[self view]];
  }
  return self;
}

- (void)awakeFromNib {
  if (!availableImage || !idleImage || !composingImage) {
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    availableImage = rb.GetNativeImageNamed(IDR_FACEBOOK_ONLINE_ICON_14);
    idleImage = rb.GetNativeImageNamed(IDR_FACEBOOK_IDLE_ICON_14);
    composingImage = rb.GetNativeImageNamed(IDR_FACEBOOK_COMPOSING_ICON_14);
  }

  NSFont* font = [NSFont controlContentFontOfSize:11];
  [button_ setFont:font];

  [button_ setTitle:
        [NSString stringWithUTF8String:bridge_->chat()->username().c_str()]];
  [button_ setImagePosition:NSImageLeft];
  [self statusChanged];
  // int nNotifications = [self chatItem]->num_notifications();
  // if (nNotifications > 0)
  //   [self setUnreadMessagesNumber:nNotifications];
  buttonTrackingArea_.reset([[NSTrackingArea alloc] initWithRect:[button_ bounds]
            options: (NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow )
            owner:self userInfo:nil]);
  [button_ addTrackingArea:buttonTrackingArea_];
  [button_ setFrame:[[self view] bounds]];
}

- (void)dealloc {
  if (notificationController_.get()) {
    [notificationController_ parentControllerWillDie];
  }

  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [super dealloc];
}

- (IBAction)activateItemAction:(id)sender {
  [chatbarController_ activateItem:self];
}

- (IBAction)removeAction:(id)sender {
  [self chatItem]->Remove();
}

- (void)openChatWindow {
  GURL popupUrl = [self getPopupURL];
  NSPoint arrowPoint = [self popupPointForChatWindow];
  FacebookPopupController *fbpc =
    [FacebookPopupController showURL:popupUrl
                           inBrowser:[chatbarController_ bridge]->browser()
                          anchoredAt:arrowPoint
                       arrowLocation:fb_bubble::kBottomCenter
                             devMode:NO];
  NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
  [center addObserver:self
             selector:@selector(chatWindowWillClose:)
                 name:NSWindowWillCloseNotification
               object:[fbpc window]];

}

- (void)chatWindowWillClose:(NSNotification*)notification {
  [[NSNotificationCenter defaultCenter] removeObserver:self];

  [self setActive:NO];
}

- (NSSize)preferredSize {
  NSSize res;
  res.width = kButtonWidth;
  res.height = kButtonHeight;
  return res;
}

- (NSPoint)popupPointForChatWindow {
  if (!button_)
    return NSZeroPoint;
  if (![button_ isDescendantOf:[chatbarController_ view]])
    return NSZeroPoint;

  // Anchor point just above the center of the bottom.
  const NSRect bounds = [button_ bounds];
  DCHECK([button_ isFlipped]);
  NSPoint anchor = NSMakePoint(NSMidX(bounds),
                               NSMinY(bounds) - kChatWindowAnchorPointYOffset);
  return [button_ convertPoint:anchor toView:nil];
}

- (NSPoint)popupPointForNotificationWindow {
if (!button_)
    return NSZeroPoint;
  if (![button_ isDescendantOf:[chatbarController_ view]])
    return NSZeroPoint;

  // Anchor point just above the center of the bottom.
  const NSRect bounds = [button_ bounds];
  DCHECK([button_ isFlipped]);
  NSPoint anchor = NSMakePoint(NSMinX(bounds) +
                                 kNotificationWindowAnchorPointXOffset,
                               NSMinY(bounds) - kChatWindowAnchorPointYOffset);
  return [button_ convertPoint:anchor toView:nil];

}

- (GURL)getPopupURL {
  Profile *profile = [chatbarController_ bridge]->browser()->profile();
  FacebookChatManager *mgr =
      FacebookChatManagerServiceFactory::GetForProfile(profile);
  std::string urlString(chrome::kFacebookChatExtensionPrefixURL);
  urlString += chrome::kFacebookChatExtensionChatPage;
  urlString += "#";
  urlString += bridge_->chat()->jid();
  urlString += "&";
  urlString += mgr->global_my_uid();
  return GURL(urlString);
}

- (FacebookChatItem*)chatItem {
  return bridge_->chat();
}

- (void)remove {
  [self closeAllPopups];

  [chatbarController_ remove:self];
}

+ (NSImage*)imageForNotificationBadgeWithNumber:(int)number {
  if (number > 0) {
    SkBitmap* notification_icon = new SkBitmap();
    notification_icon->setConfig(SkBitmap::kARGB_8888_Config, kNotifyIconDimX, kNotifyIconDimY);
    notification_icon->allocPixels();

    SkCanvas canvas(*notification_icon);
    canvas.clear(SkColorSetARGB(0, 0, 0, 0));

    // ----------------------------------------------------------------------
    gfx::Rect bounds(0, 0, kNotifyIconDimX, kNotifyIconDimY);

    char text_s[4] = { '\0', '\0', '\0', '\0' };
    char *p = text_s;
    int num = number;
    if (num > 99)
      num = 99;
    if (num > 9)
      *p++ = num / 10 + '0';
    *p = num % 10 + '0';

    std::string text(text_s);
    if (text.empty())
      return NULL;

    SkColor text_color = SK_ColorWHITE;
    SkColor background_color = SkColorSetARGB(255, 218, 0, 24);

    //canvas->Save();

    SkPaint* text_paint = badge_util::GetBadgeTextPaintSingleton();
    text_paint->setTextSize(SkFloatToScalar(kTextSize));
    text_paint->setColor(text_color);

    // Calculate text width. We clamp it to a max size.
    SkScalar text_width = text_paint->measureText(text.c_str(), text.size());
    text_width = SkIntToScalar(
        std::min(kMaxTextWidth, SkScalarFloor(text_width)));

    // Calculate badge size. It is clamped to a min width just because it looks
    // silly if it is too skinny.
    int badge_width = SkScalarFloor(text_width) + kPadding * 2;
    int icon_width = kNotifyIconDimX;
    // Force the pixel width of badge to be either odd (if the icon width is odd)
    // or even otherwise. If there is a mismatch you get http://crbug.com/26400.
    if (icon_width != 0 && (badge_width % 2 != kNotifyIconDimX % 2))
      badge_width += 1;
    badge_width = std::max(kBadgeHeight, badge_width);

    // Paint the badge background color in the right location. It is usually
    // right-aligned, but it can also be center-aligned if it is large.
    SkRect rect;
    rect.fBottom = SkIntToScalar(bounds.bottom() - kBottomMargin);
    rect.fTop = rect.fBottom - SkIntToScalar(kBadgeHeight);
    if (badge_width >= kCenterAlignThreshold) {
      rect.fLeft = SkIntToScalar(
                       SkScalarFloor(SkIntToScalar(bounds.x()) +
                                     SkIntToScalar(bounds.width()) / 2 -
                                     SkIntToScalar(badge_width) / 2));
      rect.fRight = rect.fLeft + SkIntToScalar(badge_width);
    } else {
      rect.fRight = SkIntToScalar(bounds.right());
      rect.fLeft = rect.fRight - badge_width;
    }

    SkPaint rect_paint;
    rect_paint.setStyle(SkPaint::kFill_Style);
    rect_paint.setAntiAlias(true);
    rect_paint.setColor(background_color);
    canvas.drawRoundRect(rect, SkIntToScalar(2),
                                          SkIntToScalar(2), rect_paint);

    // Overlay the gradient. It is stretchy, so we do this in three parts.
    ResourceBundle& resource_bundle = ResourceBundle::GetSharedInstance();
    SkBitmap* gradient_left = resource_bundle.GetBitmapNamed(
        IDR_BROWSER_ACTION_BADGE_LEFT);
    SkBitmap* gradient_right = resource_bundle.GetBitmapNamed(
        IDR_BROWSER_ACTION_BADGE_RIGHT);
    SkBitmap* gradient_center = resource_bundle.GetBitmapNamed(
        IDR_BROWSER_ACTION_BADGE_CENTER);

    canvas.drawBitmap(*gradient_left, rect.fLeft, rect.fTop);

    TileImageInt(canvas,
        *gradient_center,
        SkScalarFloor(rect.fLeft) + gradient_left->width(),
        SkScalarFloor(rect.fTop),
        SkScalarFloor(rect.width()) - gradient_left->width() -
                      gradient_right->width(),
        SkScalarFloor(rect.height()));
    canvas.drawBitmap(*gradient_right,
        rect.fRight - SkIntToScalar(gradient_right->width()), rect.fTop);

    // Finally, draw the text centered within the badge. We set a clip in case the
    // text was too large.
    rect.fLeft += kPadding;
    rect.fRight -= kPadding;
    canvas.clipRect(rect);
    canvas.drawText(text.c_str(), text.size(),
                                     rect.fLeft + (rect.width() - text_width) / 2,
                                     rect.fTop + kTextSize + kTopTextPadding,
                                     *text_paint);

    NSImage* img = gfx::SkBitmapToNSImage(*notification_icon);
    delete notification_icon;

    return [img retain];
  }

  return NULL;
}

- (void)setUnreadMessagesNumber:(int)number {
  if (number != 0) {
    NSImage *img = [FacebookChatItemController
        imageForNotificationBadgeWithNumber:number];
    [button_ setImage:img];
    [img release];

    if (!notificationController_.get()) {
      notificationController_.reset([[FacebookNotificationController alloc]
          initWithParentWindow:[chatbarController_ bridge]->
                                 browser()->window()->GetNativeWindow()
                    anchoredAt:[self popupPointForNotificationWindow]]);

    }

    [chatbarController_ placeFirstInOrder:self];

    std::string newMessage = [self chatItem]->GetMessageAtIndex(number-1);
    [notificationController_ messageReceived:
        base::SysUTF8ToNSString(newMessage)];
    //[button_ highlight:YES];
  } else {
    //[button_ highlight:NO];
    [self statusChanged];
    [notificationController_ close];
    notificationController_.reset(nil);
  }
}

- (void)statusChanged {
  int numNotifications = [self chatItem]->num_notifications();
  FacebookChatItem::Status status = [self chatItem]->status();

  if (numNotifications == 0) {
    if (status == FacebookChatItem::AVAILABLE)
      [button_ setImage:availableImage];
    else if (status == FacebookChatItem::IDLE)
      [button_ setImage:idleImage];
    else
      [button_ setImage:nil];

    [button_ setNeedsDisplay:YES];
  } else if (status != FacebookChatItem::COMPOSING) {
    NSImage *img = [FacebookChatItemController
        imageForNotificationBadgeWithNumber:numNotifications];
    [button_ setImage:img];
    [img release];

    [button_ setNeedsDisplay:YES];
  }

  if (status == FacebookChatItem::COMPOSING) {
    [button_ setImage:composingImage];

    [button_ setNeedsDisplay:YES];
  }
}

- (BOOL)active {
  return active_;
}

- (void)setActive:(BOOL)active {
  if (active) {
    if (notificationController_.get())
      [notificationController_ close];

    [self chatItem]->ClearUnreadMessages();
    [self openChatWindow];
  }

  active_ = active;
}

- (void)viewFrameDidChange:(NSNotification*)notification {
  [self layoutChildWindows];
}

- (void)layoutChildWindows {
  if ([self active] && [FacebookPopupController popup] &&
      [[FacebookPopupController popup] window] &&
      [[[FacebookPopupController popup] window] isVisible]) {
    NSPoint p = [self popupPointForChatWindow];
    [[FacebookPopupController popup] setAnchor:p];
  }

  if (notificationController_.get() && [notificationController_ window] //&&
      //[[notificationController_ window] isVisible]
      ) {
    [notificationController_ setAnchor:[self popupPointForNotificationWindow]];
  }
}

- (void)layedOutAfterAddingToChatbar {
  if ([self active])
    [self openChatWindow];

  int numNotifications = [self chatItem]->num_notifications();
  if (numNotifications > 0)
    [self setUnreadMessagesNumber:numNotifications];
}

- (void)mouseEntered:(NSEvent *)theEvent {
  if (notificationController_.get() &&
      [[notificationController_ window] isVisible] == NO) {
    [notificationController_ showWindow:self];
    showMouseEntered_ = YES;
  }
}

- (void)mouseExited:(NSEvent *)theEvent {
  if (showMouseEntered_) {
    [notificationController_ hideWindow];
    showMouseEntered_ = NO;
  }
}

- (void)switchParentWindow:(NSWindow*)window {
  if (notificationController_.get()) {
    [notificationController_ reparentWindowTo:window];
  }

  if ([self active] && [FacebookPopupController popup]) {
    [[FacebookPopupController popup] reparentWindowTo:window];
  }

  [self layoutChildWindows];
}

- (void)closeAllPopups {
  if ([self active])
    [[FacebookPopupController popup] close];
  if (notificationController_.get())
    [notificationController_ close];
}

@end

