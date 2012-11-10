// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/extensions/shell_window_views.h"

#include "base/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/ui_resources.h"
#include "grit/ui_strings.h"  // Accessibility names
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/base/hit_test.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/path.h"
#include "ui/gfx/scoped_sk_region.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

#if defined(OS_WIN) && !defined(USE_AURA)
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/web_applications/web_app.h"
#include "ui/base/win/shell.h"
#endif

#if defined(USE_ASH)
#include "ash/wm/custom_frame_view_ash.h"
#include "ui/aura/window.h"
#endif

namespace {
// TODO(jeremya): these are copy/pasted from ash/wm/frame_painter.cc, and I'd
// like to find a way to avoid duplicating the constants.
#if defined(USE_ASH)
const int kResizeOutsideBoundsSizeTouch = 30;
const int kResizeOutsideBoundsSize = 6;
const int kResizeInsideBoundsSize = 1;
const int kResizeAreaCornerSize = 16;
#else
const int kResizeOutsideBoundsSizeTouch = 0;
const int kResizeOutsideBoundsSize = 0;
const int kResizeInsideBoundsSize = 5;
const int kResizeAreaCornerSize = 16;
#endif

// Height of the chrome-style caption, in pixels.
const int kCaptionHeight = 25;
}  // namespace

class ShellWindowFrameView : public views::NonClientFrameView,
                             public views::ButtonListener {
 public:
  static const char kViewClassName[];

  explicit ShellWindowFrameView(bool frameless);
  virtual ~ShellWindowFrameView();

  void Init(views::Widget* frame);

  // views::NonClientFrameView implementation.
  virtual gfx::Rect GetBoundsForClientView() const OVERRIDE;
  virtual gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const OVERRIDE;
  virtual int NonClientHitTest(const gfx::Point& point) OVERRIDE;
  virtual void GetWindowMask(const gfx::Size& size,
                             gfx::Path* window_mask) OVERRIDE;
  virtual void ResetWindowControls() OVERRIDE {}
  virtual void UpdateWindowIcon() OVERRIDE {}

  // views::View implementation.
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual void Layout() OVERRIDE;
  virtual std::string GetClassName() const OVERRIDE;
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;
  virtual gfx::Size GetMinimumSize() OVERRIDE;
  virtual gfx::Size GetMaximumSize() OVERRIDE;

 private:
  // views::ButtonListener implementation.
  virtual void ButtonPressed(views::Button* sender, const views::Event& event)
      OVERRIDE;

  views::Widget* frame_;
  views::ImageButton* close_button_;

  bool is_frameless_;

  DISALLOW_COPY_AND_ASSIGN(ShellWindowFrameView);
};

const char ShellWindowFrameView::kViewClassName[] =
    "browser/ui/views/extensions/ShellWindowFrameView";

ShellWindowFrameView::ShellWindowFrameView(bool frameless)
    : frame_(NULL),
      close_button_(NULL),
      is_frameless_(frameless) {
}

ShellWindowFrameView::~ShellWindowFrameView() {
}

void ShellWindowFrameView::Init(views::Widget* frame) {
  frame_ = frame;

  if (!is_frameless_) {
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    close_button_ = new views::ImageButton(this);
    close_button_->SetImage(views::CustomButton::BS_NORMAL,
        rb.GetNativeImageNamed(IDR_CLOSE_BAR).ToImageSkia());
    close_button_->SetImage(views::CustomButton::BS_HOT,
        rb.GetNativeImageNamed(IDR_CLOSE_BAR_H).ToImageSkia());
    close_button_->SetImage(views::CustomButton::BS_PUSHED,
        rb.GetNativeImageNamed(IDR_CLOSE_BAR_P).ToImageSkia());
    close_button_->SetAccessibleName(
        l10n_util::GetStringUTF16(IDS_APP_ACCNAME_CLOSE));
    AddChildView(close_button_);
  }

#if defined(USE_ASH)
  aura::Window* window = frame->GetNativeWindow();
  // Ensure we get resize cursors for a few pixels outside our bounds.
  int outside_bounds = ui::GetDisplayLayout() == ui::LAYOUT_TOUCH ?
      kResizeOutsideBoundsSizeTouch :
      kResizeOutsideBoundsSize;
  window->set_hit_test_bounds_override_outer(
      gfx::Insets(-outside_bounds, -outside_bounds,
                  -outside_bounds, -outside_bounds));
  // Ensure we get resize cursors just inside our bounds as well.
  // TODO(jeremya): do we need to update these when in fullscreen/maximized?
  window->set_hit_test_bounds_override_inner(
      gfx::Insets(kResizeInsideBoundsSize, kResizeInsideBoundsSize,
                  kResizeInsideBoundsSize, kResizeInsideBoundsSize));
#endif
}

gfx::Rect ShellWindowFrameView::GetBoundsForClientView() const {
  if (is_frameless_ || frame_->IsFullscreen())
    return bounds();
  return gfx::Rect(0, kCaptionHeight, width(),
      std::max(0, height() - kCaptionHeight));
}

gfx::Rect ShellWindowFrameView::GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const {
  if (is_frameless_)
    return client_bounds;

  int closeButtonOffsetX =
      (kCaptionHeight - close_button_->height()) / 2;
  int header_width = close_button_->width() + closeButtonOffsetX * 2;
  return gfx::Rect(client_bounds.x(),
                   std::max(0, client_bounds.y() - kCaptionHeight),
                   std::max(header_width, client_bounds.width()),
                   client_bounds.height() + kCaptionHeight);
}

int ShellWindowFrameView::NonClientHitTest(const gfx::Point& point) {
  if (frame_->IsFullscreen())
    return HTCLIENT;

#if defined(USE_ASH)
  gfx::Rect expanded_bounds = bounds();
  int outside_bounds = ui::GetDisplayLayout() == ui::LAYOUT_TOUCH ?
      kResizeOutsideBoundsSizeTouch :
      kResizeOutsideBoundsSize;
  expanded_bounds.Inset(-outside_bounds, -outside_bounds);
  if (!expanded_bounds.Contains(point))
    return HTNOWHERE;
#endif

  // Check the frame first, as we allow a small area overlapping the contents
  // to be used for resize handles.
  bool can_ever_resize = frame_->widget_delegate() ?
      frame_->widget_delegate()->CanResize() :
      false;
  // Don't allow overlapping resize handles when the window is maximized or
  // fullscreen, as it can't be resized in those states.
  int resize_border =
      frame_->IsMaximized() || frame_->IsFullscreen() ? 0 :
      kResizeInsideBoundsSize;
  int frame_component = GetHTComponentForFrame(point,
                                               resize_border,
                                               resize_border,
                                               kResizeAreaCornerSize,
                                               kResizeAreaCornerSize,
                                               can_ever_resize);
  if (frame_component != HTNOWHERE)
    return frame_component;

  int client_component = frame_->client_view()->NonClientHitTest(point);
  if (client_component != HTNOWHERE)
    return client_component;

  // Then see if the point is within any of the window controls.
  if (close_button_->visible() &&
      close_button_->GetMirroredBounds().Contains(point))
    return HTCLOSE;

  // Caption is a safe default.
  return HTCAPTION;
}

void ShellWindowFrameView::GetWindowMask(const gfx::Size& size,
                                         gfx::Path* window_mask) {
  // We got nothing to say about no window mask.
}

gfx::Size ShellWindowFrameView::GetPreferredSize() {
  gfx::Size pref = frame_->client_view()->GetPreferredSize();
  gfx::Rect bounds(0, 0, pref.width(), pref.height());
  return frame_->non_client_view()->GetWindowBoundsForClientBounds(
      bounds).size();
}

void ShellWindowFrameView::Layout() {
  if (is_frameless_)
    return;
  gfx::Size close_size = close_button_->GetPreferredSize();
  int closeButtonOffsetY =
      (kCaptionHeight - close_size.height()) / 2;
  int closeButtonOffsetX = closeButtonOffsetY;
  close_button_->SetBounds(
      width() - closeButtonOffsetX - close_size.width(),
      closeButtonOffsetY,
      close_size.width(),
      close_size.height());
}

void ShellWindowFrameView::OnPaint(gfx::Canvas* canvas) {
  if (is_frameless_)
    return;
  // TODO(jeremya): different look for inactive?
  SkPaint paint;
  paint.setAntiAlias(false);
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(SK_ColorWHITE);
  gfx::Path path;
  const int radius = 1;
  path.moveTo(0, radius);
  path.lineTo(radius, 0);
  path.lineTo(width() - radius - 1, 0);
  path.lineTo(width(), radius + 1);
  path.lineTo(width(), kCaptionHeight);
  path.lineTo(0, kCaptionHeight);
  path.close();
  canvas->DrawPath(path, paint);
}

std::string ShellWindowFrameView::GetClassName() const {
  return kViewClassName;
}

gfx::Size ShellWindowFrameView::GetMinimumSize() {
  gfx::Size min_size = frame_->client_view()->GetMinimumSize();
  if (is_frameless_)
    return min_size;

  // Ensure we can display the top of the caption area.
  gfx::Rect client_bounds = GetBoundsForClientView();
  min_size.Enlarge(0, client_bounds.y());
  // Ensure we have enough space for the window icon and buttons.  We allow
  // the title string to collapse to zero width.
  int closeButtonOffsetX =
      (kCaptionHeight - close_button_->height()) / 2;
  int header_width = close_button_->width() + closeButtonOffsetX * 2;
  if (header_width > min_size.width())
    min_size.set_width(header_width);
  return min_size;
}

gfx::Size ShellWindowFrameView::GetMaximumSize() {
  gfx::Size max_size = frame_->client_view()->GetMaximumSize();
  if (is_frameless_)
    return max_size;

  if (!max_size.IsEmpty()) {
    gfx::Rect client_bounds = GetBoundsForClientView();
    max_size.Enlarge(0, client_bounds.y());
  }
  return max_size;
}

void ShellWindowFrameView::ButtonPressed(views::Button* sender,
                                         const views::Event& event) {
  DCHECK(!is_frameless_);
  if (sender == close_button_)
    frame_->Close();
}

ShellWindowViews::ShellWindowViews(Profile* profile,
                                   const extensions::Extension* extension,
                                   const GURL& url,
                                   const ShellWindow::CreateParams& win_params)
    : ShellWindow(profile, extension, url),
      web_view_(NULL),
      is_fullscreen_(false),
      use_custom_frame_(
          win_params.frame == ShellWindow::CreateParams::FRAME_NONE) {
  window_ = new views::Widget;
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = this;
  params.remove_standard_frame = true;
  minimum_size_ = win_params.minimum_size;
  maximum_size_ = win_params.maximum_size;
  window_->Init(params);
  gfx::Rect window_bounds =
      window_->non_client_view()->GetWindowBoundsForClientBounds(
          win_params.bounds);
  window_->SetBounds(window_bounds);
#if defined(OS_WIN) && !defined(USE_AURA)
  std::string app_name = web_app::GenerateApplicationNameFromExtensionId(
      extension->id());
  ui::win::SetAppIdForWindow(
      ShellIntegration::GetAppModelIdForProfile(UTF8ToWide(app_name),
                                                profile->GetPath()),
      GetWidget()->GetTopLevelWidget()->GetNativeWindow());
#endif
  OnViewWasResized();

  window_->Show();
}

views::View* ShellWindowViews::GetInitiallyFocusedView() {
  return web_view_;
}

void ShellWindowViews::OnFocus() {
  web_view_->RequestFocus();
}

void ShellWindowViews::ViewHierarchyChanged(
    bool is_add, views::View *parent, views::View *child) {
  if (is_add && child == this) {
    web_view_ = new views::WebView(NULL);
    AddChildView(web_view_);
    web_view_->SetWebContents(web_contents());
  }
}

gfx::Size ShellWindowViews::GetMinimumSize() {
  return minimum_size_;
}

gfx::Size ShellWindowViews::GetMaximumSize() {
  return maximum_size_;
}

void ShellWindowViews::SetFullscreen(bool fullscreen) {
  is_fullscreen_ = fullscreen;
  window_->SetFullscreen(fullscreen);
  // TODO(jeremya) we need to call RenderViewHost::ExitFullscreen() if we
  // ever drop the window out of fullscreen in response to something that
  // wasn't the app calling webkitCancelFullScreen().
}

bool ShellWindowViews::IsFullscreenOrPending() const {
  return is_fullscreen_;
}

ShellWindowViews::~ShellWindowViews() {
  web_view_->SetWebContents(NULL);
}

bool ShellWindowViews::IsActive() const {
  return window_->IsActive();
}

bool ShellWindowViews::IsMaximized() const {
  return window_->IsMaximized();
}

bool ShellWindowViews::IsMinimized() const {
  return window_->IsMinimized();
}

bool ShellWindowViews::IsFullscreen() const {
  return window_->IsFullscreen();
}

gfx::NativeWindow ShellWindowViews::GetNativeWindow() {
  return window_->GetNativeWindow();
}

gfx::Rect ShellWindowViews::GetRestoredBounds() const {
  return window_->GetRestoredBounds();
}

gfx::Rect ShellWindowViews::GetBounds() const {
  return window_->GetWindowBoundsInScreen();
}

void ShellWindowViews::Show() {
  if (window_->IsVisible()) {
    window_->Activate();
    return;
  }

  window_->Show();
}

void ShellWindowViews::ShowInactive() {
  if (window_->IsVisible())
    return;
  window_->ShowInactive();
}

void ShellWindowViews::Close() {
  window_->Close();
}

void ShellWindowViews::Activate() {
  window_->Activate();
}

void ShellWindowViews::Deactivate() {
  window_->Deactivate();
}

void ShellWindowViews::Maximize() {
  window_->Maximize();
}

void ShellWindowViews::Minimize() {
  window_->Minimize();
}

void ShellWindowViews::Restore() {
  window_->Restore();
}

void ShellWindowViews::SetBounds(const gfx::Rect& bounds) {
  GetWidget()->SetBounds(bounds);
}

void ShellWindowViews::SetDraggableRegion(SkRegion* region) {
  caption_region_.Set(region);
  OnViewWasResized();
}

void ShellWindowViews::FlashFrame(bool flash) {
  window_->FlashFrame(flash);
}

bool ShellWindowViews::IsAlwaysOnTop() const {
  return false;
}

void ShellWindowViews::DeleteDelegate() {
  OnNativeClose();
}

bool ShellWindowViews::CanResize() const {
  return true;
}

bool ShellWindowViews::CanMaximize() const {
  return true;
}

views::View* ShellWindowViews::GetContentsView() {
  return this;
}

views::NonClientFrameView* ShellWindowViews::CreateNonClientFrameView(
    views::Widget* widget) {
  ShellWindowFrameView* frame_view =
      new ShellWindowFrameView(use_custom_frame_);
  frame_view->Init(window_);
  return frame_view;
}

string16 ShellWindowViews::GetWindowTitle() const {
  return GetTitle();
}

views::Widget* ShellWindowViews::GetWidget() {
  return window_;
}

const views::Widget* ShellWindowViews::GetWidget() const {
  return window_;
}

void ShellWindowViews::OnViewWasResized() {
  // TODO(jeremya): this doesn't seem like a terribly elegant way to keep the
  // window shape in sync.
#if defined(OS_WIN) && !defined(USE_AURA)
  // Set the window shape of the RWHV.
  DCHECK(window_);
  DCHECK(web_view_);
  gfx::Size sz = web_view_->size();
  int height = sz.height(), width = sz.width();
  int radius = 1;
  gfx::Path path;
  if (window_->IsMaximized() || window_->IsFullscreen()) {
    // Don't round the corners when the window is maximized or fullscreen.
    path.addRect(0, 0, width, height);
  } else {
    if (use_custom_frame_) {
      path.moveTo(0, radius);
      path.lineTo(radius, 0);
      path.lineTo(width - radius, 0);
      path.lineTo(width, radius);
    } else {
      // Don't round the top corners in chrome-style frame mode.
      path.moveTo(0, 0);
      path.lineTo(width, 0);
    }
    path.lineTo(width, height - radius - 1);
    path.lineTo(width - radius - 1, height);
    path.lineTo(radius + 1, height);
    path.lineTo(0, height - radius - 1);
    path.close();
  }
  SetWindowRgn(web_contents()->GetNativeView(), path.CreateNativeRegion(), 1);

  SkRegion* rgn = new SkRegion;
  if (!window_->IsFullscreen()) {
    if (caption_region_.Get())
      rgn->op(*caption_region_.Get(), SkRegion::kUnion_Op);
    if (!window_->IsMaximized()) {
      if (use_custom_frame_)
        rgn->op(0, 0, width, kResizeInsideBoundsSize, SkRegion::kUnion_Op);
      rgn->op(0, 0, kResizeInsideBoundsSize, height, SkRegion::kUnion_Op);
      rgn->op(width - kResizeInsideBoundsSize, 0, width, height,
          SkRegion::kUnion_Op);
      rgn->op(0, height - kResizeInsideBoundsSize, width, height,
          SkRegion::kUnion_Op);
    }
  }
  web_contents()->GetRenderViewHost()->GetView()->SetClickthroughRegion(rgn);
#endif
}

void ShellWindowViews::Layout() {
  DCHECK(web_view_);
  web_view_->SetBounds(0, 0, width(), height());
  OnViewWasResized();
}

void ShellWindowViews::UpdateWindowTitle() {
  window_->UpdateWindowTitle();
}

// static
ShellWindow* ShellWindow::CreateImpl(Profile* profile,
                                     const extensions::Extension* extension,
                                     const GURL& url,
                                     const ShellWindow::CreateParams& params) {
  return new ShellWindowViews(profile, extension, url, params);
}
