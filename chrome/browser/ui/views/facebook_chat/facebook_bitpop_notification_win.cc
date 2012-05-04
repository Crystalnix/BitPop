// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/facebook_chat/facebook_bitpop_notification_win.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/common/badge_util.h"
#include "grit/ui_resources.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "third_party/skia/include/effects/SkGradientShader.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/skia_util.h"

#if defined(OS_WIN)
#include <shobjidl.h>
#include "base/win/scoped_comptr.h"
#include "base/win/windows_version.h"
#include "skia/ext/image_operations.h"
#include "ui/gfx/icon_util.h"
#endif

namespace {

const int kNotifyIconDimX = 16;
const int kNotifyIconDimY = 16;

const float kTextSize = 10;
const int kBottomMargin = 0;
const int kPadding = 2;
// The padding between the top of the badge and the top of the text.
const int kTopTextPadding = -1;
const int kBadgeHeight = 11;
const int kMaxTextWidth = 14;
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
FacebookBitpopNotificationWin::FacebookBitpopNotificationWin(Profile* profile)
  : profile_(profile), notified_hwnd_(NULL) {
}

FacebookBitpopNotificationWin::~FacebookBitpopNotificationWin() {
}

void FacebookBitpopNotificationWin::ClearNotification() {
  if (base::win::GetVersion() < base::win::VERSION_WIN7)
    return;

  if (!notified_hwnd_)
    return;

  base::win::ScopedComPtr<ITaskbarList3> taskbar;
  HRESULT result = taskbar.CreateInstance(CLSID_TaskbarList, NULL,
                                          CLSCTX_INPROC_SERVER);
  if (FAILED(result) || FAILED(taskbar->HrInit()))
    return;

  taskbar->SetOverlayIcon(notified_hwnd_, NULL, L"");
  notified_hwnd_ = NULL;
}

void FacebookBitpopNotificationWin::NotifyUnreadMessagesWithLastUser(int num_unread,
                                                const std::string& user_id) {

  Browser* browser = Browser::GetTabbedBrowser(profile_, false);
  if (browser == NULL)
    return;

  HWND hwnd = browser->window()->GetNativeHandle();

  FLASHWINFO fwInfo;
  ::ZeroMemory(&fwInfo, sizeof(FLASHWINFO));
  fwInfo.cbSize = sizeof(FLASHWINFO);
  fwInfo.hwnd = hwnd;
  fwInfo.dwFlags = FLASHW_TIMERNOFG | FLASHW_TRAY;
  ::FlashWindowEx(&fwInfo);

  if (base::win::GetVersion() < base::win::VERSION_WIN7)
    return;

  base::win::ScopedComPtr<ITaskbarList3> taskbar;
  HRESULT result = taskbar.CreateInstance(CLSID_TaskbarList, NULL,
                                          CLSCTX_INPROC_SERVER);
  if (FAILED(result) || FAILED(taskbar->HrInit()))
    return;
  HICON icon = NULL;

  SkBitmap* notification_icon = new SkBitmap();
  notification_icon->setConfig(SkBitmap::kARGB_8888_Config, kNotifyIconDimX, kNotifyIconDimY);
  notification_icon->allocPixels();

  SkCanvas canvas(*notification_icon);
  canvas.clear(SkColorSetARGB(0, 0, 0, 0));

  // ----------------------------------------------------------------------
  gfx::Rect bounds(0, 0, 16, 11);

  char text_s[4] = { '\0', '\0', '\0', '\0' };
  char *p = text_s;
  int num = num_unread;
  if (num > 99)
    num = 99;
  if (num > 9)
    *p++ = num / 10 + '0';
  *p = num % 10 + '0';

  std::string text(text_s);
  if (text.empty())
    return;

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

  icon = IconUtil::CreateHICONFromSkBitmap(*notification_icon);
  if (!icon)
    return;
  taskbar->SetOverlayIcon(hwnd, icon, L"");
  notified_hwnd_ = hwnd;
  if (icon)
    ::DestroyIcon(icon);
  
  if (notification_icon)
    delete notification_icon;
}
