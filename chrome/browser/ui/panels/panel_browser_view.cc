// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/panels/panel_browser_view.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "chrome/browser/ui/panels/panel.h"
#include "chrome/browser/ui/panels/panel_bounds_animation.h"
#include "chrome/browser/ui/panels/panel_browser_frame_view.h"
#include "chrome/browser/ui/panels/panel_manager.h"
#include "chrome/browser/ui/views/frame/browser_frame.h"
#include "chrome/browser/ui/webui/chrome_web_ui.h"
#include "chrome/browser/ui/webui/task_manager_dialog.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/notification_service.h"
#include "grit/chromium_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"

using content::WebContents;

namespace {
// The threshold to differentiate the short click and long click.
const int kShortClickThresholdMs = 200;

// Delay before click-to-minimize is allowed after the attention has been
// cleared.
const int kSuspendMinimizeOnClickIntervalMs = 500;
}

NativePanel* Panel::CreateNativePanel(Browser* browser, Panel* panel,
                                      const gfx::Rect& bounds) {
  PanelBrowserView* view = new PanelBrowserView(browser, panel, bounds);
  (new BrowserFrame(view))->InitBrowserFrame();
  return view;
}

PanelBrowserView::PanelBrowserView(Browser* browser, Panel* panel,
                                   const gfx::Rect& bounds)
  : BrowserView(browser),
    panel_(panel),
    bounds_(bounds),
    closed_(false),
    focused_(false),
    mouse_pressed_(false),
    mouse_dragging_state_(NO_DRAGGING),
    is_drawing_attention_(false),
    old_focused_view_(NULL) {
}

PanelBrowserView::~PanelBrowserView() {
  panel_->OnNativePanelClosed();
}

void PanelBrowserView::Init() {
  if (!panel_->manager()->is_full_screen()) {
    // TODO(prasadt): Implement this code.
    // HideThePanel.
  }

  BrowserView::Init();

  GetWidget()->non_client_view()->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_PRODUCT_NAME));
}

void PanelBrowserView::Show() {
  if (!panel_->manager()->is_full_screen())
    BrowserView::Show();
}

void PanelBrowserView::ShowInactive() {
  if (!panel_->manager()->is_full_screen())
    BrowserView::ShowInactive();
}

void PanelBrowserView::Close() {
  GetWidget()->RemoveObserver(this);
  closed_ = true;

  // Cancel any currently running animation since we're closing down.
  if (bounds_animator_.get())
    bounds_animator_.reset();

  ::BrowserView::Close();
}

void PanelBrowserView::Deactivate() {
  if (!IsActive())
    return;

#if defined(OS_WIN) && !defined(USE_AURA)
  gfx::NativeWindow native_window = NULL;
  BrowserWindow* browser_window =
      panel_->manager()->GetNextBrowserWindowToActivate(panel_.get());
  if (browser_window)
    native_window = browser_window->GetNativeHandle();
  else
    native_window = ::GetDesktopWindow();
  if (native_window)
    ::SetForegroundWindow(native_window);
  else
    ::SetFocus(NULL);
#else
  NOTIMPLEMENTED();
  BrowserView::Deactivate();
#endif
}

bool PanelBrowserView::CanResize() const {
  return false;
}

bool PanelBrowserView::CanMaximize() const {
  return false;
}

void PanelBrowserView::SetBounds(const gfx::Rect& bounds) {
  SetBoundsInternal(bounds, true);
}

void PanelBrowserView::SetBoundsInternal(const gfx::Rect& new_bounds,
                                         bool animate) {
  if (bounds_ == new_bounds)
    return;

  bounds_ = new_bounds;

  // No animation if the panel is being dragged.
  if (!animate || mouse_dragging_state_ == DRAGGING_STARTED) {
    ::BrowserView::SetBounds(new_bounds);
    return;
  }

  animation_start_bounds_ = GetBounds();

  bounds_animator_.reset(new PanelBoundsAnimation(
      this, panel(), animation_start_bounds_, new_bounds));
  bounds_animator_->Start();
}

void PanelBrowserView::UpdateTitleBar() {
  ::BrowserView::UpdateTitleBar();
  GetFrameView()->UpdateTitleBar();
}

bool PanelBrowserView::IsPanel() const {
  return true;
}

bool PanelBrowserView::GetSavedWindowPlacement(
    gfx::Rect* bounds,
    ui::WindowShowState* show_state) const {
  *bounds = bounds_;
  *show_state = ui::SHOW_STATE_NORMAL;
  return true;
}

void PanelBrowserView::OnWidgetActivationChanged(views::Widget* widget,
                                                 bool active) {
  ::BrowserView::OnWidgetActivationChanged(widget, active);

#if defined(OS_WIN) && !defined(USE_AURA)
  // The panel window is in focus (actually accepting keystrokes) if it is
  // active and belongs to a foreground application.
  bool focused = active &&
      GetFrameView()->GetWidget()->GetNativeView() == ::GetForegroundWindow();
#else
  NOTIMPLEMENTED();
  bool focused = active;
#endif

  if (focused_ == focused)
    return;
  focused_ = focused;

  GetFrameView()->OnFocusChanged(focused);

  if (focused_) {
    // Expand the panel if needed. Do NOT expand a TITLE_ONLY panel
    // otherwise it will be impossible to drag a title without
    // expanding it.
    if (panel_->expansion_state() == Panel::MINIMIZED)
      panel_->SetExpansionState(Panel::EXPANDED);

    if (is_drawing_attention_) {
      DrawAttention(false);

      // Restore the panel from title-only mode here. Could not do this in the
      // code above.
      if (panel_->expansion_state() == Panel::TITLE_ONLY)
        panel_->SetExpansionState(Panel::EXPANDED);

      // This function is called per one of the following user interactions:
      // 1) clicking on the title-bar
      // 2) clicking on the client area
      // 3) switching to the panel via keyboard
      // For case 1, we do not want the expanded panel to be minimized since the
      // user clicks on it to mean to clear the attention.
      attention_cleared_time_ = base::TimeTicks::Now();
    }
  }

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PANEL_CHANGED_ACTIVE_STATUS,
      content::Source<Panel>(panel()),
      content::NotificationService::NoDetails());
}

bool PanelBrowserView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (mouse_pressed_ && accelerator.key_code() == ui::VKEY_ESCAPE) {
    OnTitlebarMouseCaptureLost();
    return true;
  }

  // No other accelerator is allowed when the drag begins.
  if (mouse_dragging_state_ == DRAGGING_STARTED)
    return true;

  return BrowserView::AcceleratorPressed(accelerator);
}

void PanelBrowserView::AnimationEnded(const ui::Animation* animation) {
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PANEL_BOUNDS_ANIMATIONS_FINISHED,
      content::Source<Panel>(panel()),
      content::NotificationService::NoDetails());
}

void PanelBrowserView::AnimationProgressed(const ui::Animation* animation) {
  gfx::Rect new_bounds = bounds_animator_->CurrentValueBetween(
      animation_start_bounds_, bounds_);
  ::BrowserView::SetBounds(new_bounds);
}

void PanelBrowserView::OnDisplayChanged() {
  BrowserView::OnDisplayChanged();
  panel_->manager()->OnDisplayChanged();
}

void PanelBrowserView::OnWorkAreaChanged() {
  BrowserView::OnWorkAreaChanged();
  panel_->manager()->OnDisplayChanged();
}

bool PanelBrowserView::WillProcessWorkAreaChange() const {
  return true;
}

void PanelBrowserView::ShowPanel() {
  Show();
}

void PanelBrowserView::ShowPanelInactive() {
  ShowInactive();
}

gfx::Rect PanelBrowserView::GetPanelBounds() const {
  return bounds_;
}

void PanelBrowserView::SetPanelBounds(const gfx::Rect& bounds) {
  SetBoundsInternal(bounds, true);
}

void PanelBrowserView::SetPanelBoundsInstantly(const gfx::Rect& bounds) {
  SetBoundsInternal(bounds, false);
}

void PanelBrowserView::ClosePanel() {
  Close();
}

void PanelBrowserView::ActivatePanel() {
  Activate();
}

void PanelBrowserView::DeactivatePanel() {
  Deactivate();
}

bool PanelBrowserView::IsPanelActive() const {
  return IsActive();
}

gfx::NativeWindow PanelBrowserView::GetNativePanelHandle() {
  return GetNativeHandle();
}

void PanelBrowserView::UpdatePanelTitleBar() {
  UpdateTitleBar();
}

void PanelBrowserView::UpdatePanelLoadingAnimations(bool should_animate) {
  UpdateLoadingAnimations(should_animate);
}

void PanelBrowserView::ShowTaskManagerForPanel() {
#if defined(WEBUI_TASK_MANAGER)
  TaskManagerDialog::Show();
#else
  // Uses WebUI TaskManager when swiches is set. It is beta feature.
  if (chrome_web_ui::IsMoreWebUI()) {
    TaskManagerDialog::Show();
  } else {
    ShowTaskManager();
  }
#endif  // defined(WEBUI_TASK_MANAGER)
}

FindBar* PanelBrowserView::CreatePanelFindBar() {
  return CreateFindBar();
}

void PanelBrowserView::NotifyPanelOnUserChangedTheme() {
  UserChangedTheme();
}

void PanelBrowserView::PanelWebContentsFocused(WebContents* contents) {
  WebContentsFocused(contents);
}

void PanelBrowserView::PanelCut() {
  Cut();
}

void PanelBrowserView::PanelCopy() {
  Copy();
}

void PanelBrowserView::PanelPaste() {
  Paste();
}

void PanelBrowserView::DrawAttention(bool draw_attention) {
  if (is_drawing_attention_ == draw_attention)
    return;
  is_drawing_attention_ = draw_attention;
  GetFrameView()->SchedulePaint();
}

bool PanelBrowserView::IsDrawingAttention() const {
  return is_drawing_attention_;
}

bool PanelBrowserView::PreHandlePanelKeyboardEvent(
    const NativeWebKeyboardEvent& event,
    bool* is_keyboard_shortcut) {
  return PreHandleKeyboardEvent(event, is_keyboard_shortcut);
}

void PanelBrowserView::FullScreenModeChanged(bool is_full_screen) {
  if (is_full_screen) {
    if (frame()->IsVisible()) {
        frame()->Hide();
    }
  } else {
    ShowInactive();
  }
}

void PanelBrowserView::HandlePanelKeyboardEvent(
    const NativeWebKeyboardEvent& event) {
  HandleKeyboardEvent(event);
}

gfx::Size PanelBrowserView::WindowSizeFromContentSize(
    const gfx::Size& content_size) const {
  gfx::Size frame = GetFrameView()->NonClientAreaSize();
  return gfx::Size(content_size.width() + frame.width(),
                   content_size.height() + frame.height());
}

gfx::Size PanelBrowserView::ContentSizeFromWindowSize(
    const gfx::Size& window_size) const {
  gfx::Size frame = GetFrameView()->NonClientAreaSize();
  return gfx::Size(window_size.width() - frame.width(),
                   window_size.height() - frame.height());
}

int PanelBrowserView::TitleOnlyHeight() const {
  return GetFrameView()->NonClientTopBorderHeight();
}

Browser* PanelBrowserView::GetPanelBrowser() const {
  return browser();
}

void PanelBrowserView::DestroyPanelBrowser() {
  DestroyBrowser();
}

gfx::Size PanelBrowserView::IconOnlySize() const {
  return GetFrameView()->IconOnlySize();
}

void PanelBrowserView::EnsurePanelFullyVisible() {
#if defined(OS_WIN) && !defined(USE_AURA)
  ::SetWindowPos(GetNativeHandle(), HWND_TOP, 0, 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
#else
  NOTIMPLEMENTED();
#endif
}

PanelBrowserFrameView* PanelBrowserView::GetFrameView() const {
  return static_cast<PanelBrowserFrameView*>(frame()->GetFrameView());
}

bool PanelBrowserView::OnTitlebarMousePressed(const gfx::Point& location) {
  // |location| is in the view's coordinate system. Convert it to the screen
  // coordinate system.
  mouse_location_ = location;
  views::View::ConvertPointToScreen(this, &mouse_location_);

  mouse_pressed_ = true;
  mouse_pressed_time_ = base::TimeTicks::Now();
  mouse_dragging_state_ = NO_DRAGGING;
  return true;
}

bool PanelBrowserView::OnTitlebarMouseDragged(const gfx::Point& location) {
  if (!mouse_pressed_)
    return false;

  if (!panel_->draggable())
    return true;

  gfx::Point last_mouse_location = mouse_location_;

  // |location| is in the view's coordinate system. Convert it to the screen
  // coordinate system.
  mouse_location_ = location;
  views::View::ConvertPointToScreen(this, &mouse_location_);

  int delta_x = mouse_location_.x() - last_mouse_location.x();
  int delta_y = mouse_location_.y() - last_mouse_location.y();
  if (mouse_dragging_state_ == NO_DRAGGING &&
      ExceededDragThreshold(delta_x, delta_y)) {
    // When a drag begins, we do not want to the client area to still receive
    // the focus.
    old_focused_view_ = GetFocusManager()->GetFocusedView();
    GetFocusManager()->SetFocusedView(GetFrameView());

    panel_->manager()->StartDragging(panel_.get());
    mouse_dragging_state_ = DRAGGING_STARTED;
  }
  if (mouse_dragging_state_ == DRAGGING_STARTED)
    panel_->manager()->Drag(delta_x);
  return true;
}

bool PanelBrowserView::OnTitlebarMouseReleased() {
  if (mouse_dragging_state_ == DRAGGING_STARTED) {
    // When a drag ends, restore the focus.
    if (old_focused_view_) {
      GetFocusManager()->SetFocusedView(old_focused_view_);
      old_focused_view_ = NULL;
    }

    return EndDragging(false);
  }

  // If the panel drag was cancelled before the mouse is released, do not treat
  // this as a click.
  if (mouse_dragging_state_ != NO_DRAGGING)
    return true;

  // Do not minimize the panel when we just clear the attention state. This is
  // a hack to prevent the panel from being minimized when the user clicks on
  // the title-bar to clear the attention.
  if (panel_->expansion_state() == Panel::EXPANDED &&
      base::TimeTicks::Now() - attention_cleared_time_ <
      base::TimeDelta::FromMilliseconds(kSuspendMinimizeOnClickIntervalMs)) {
    return true;
  }

  // Ignore long clicks. Treated as a canceled click to be consistent with Mac.
  if (base::TimeTicks::Now() - mouse_pressed_time_ >
      base::TimeDelta::FromMilliseconds(kShortClickThresholdMs))
    return true;

  Panel::ExpansionState new_expansion_state =
    (panel_->expansion_state() != Panel::EXPANDED) ? Panel::EXPANDED
                                                   : Panel::MINIMIZED;
  panel_->SetExpansionState(new_expansion_state);
  return true;
}

bool PanelBrowserView::OnTitlebarMouseCaptureLost() {
  if (mouse_dragging_state_ == DRAGGING_STARTED)
    return EndDragging(true);
  return true;
}

bool PanelBrowserView::EndDragging(bool cancelled) {
  // Only handle clicks that started in our window.
  if (!mouse_pressed_)
    return false;
  mouse_pressed_ = false;

  mouse_dragging_state_ = DRAGGING_ENDED;
  panel_->manager()->EndDragging(cancelled);
  return true;
}

void PanelBrowserView::SetPanelAppIconVisibility(bool visible) {
#if defined(OS_WIN) && !defined(USE_AURA)
  gfx::NativeWindow native_window = GetNativeHandle();
  ::ShowWindow(native_window, SW_HIDE);
  int style = ::GetWindowLong(native_window, GWL_EXSTYLE);
  if (visible)
    style &= (~WS_EX_TOOLWINDOW);
  else
    style |= WS_EX_TOOLWINDOW;
  ::SetWindowLong(native_window, GWL_EXSTYLE, style);
  ::ShowWindow(native_window, SW_SHOWNA);
#else
  NOTIMPLEMENTED();
#endif
}

// NativePanelTesting implementation.
class NativePanelTestingWin : public NativePanelTesting {
 public:
  explicit NativePanelTestingWin(PanelBrowserView* panel_browser_view);

 private:
  virtual void PressLeftMouseButtonTitlebar(
      const gfx::Point& point) OVERRIDE;
  virtual void ReleaseMouseButtonTitlebar() OVERRIDE;
  virtual void DragTitlebar(int delta_x, int delta_y) OVERRIDE;
  virtual void CancelDragTitlebar() OVERRIDE;
  virtual void FinishDragTitlebar() OVERRIDE;
  virtual bool VerifyDrawingAttention() const OVERRIDE;
  virtual bool VerifyActiveState(bool is_active) OVERRIDE;
  virtual bool IsWindowSizeKnown() const OVERRIDE;
  virtual bool IsAnimatingBounds() const OVERRIDE;

  PanelBrowserView* panel_browser_view_;
};

// static
NativePanelTesting* NativePanelTesting::Create(NativePanel* native_panel) {
  return new NativePanelTestingWin(static_cast<PanelBrowserView*>(
      native_panel));
}

NativePanelTestingWin::NativePanelTestingWin(
    PanelBrowserView* panel_browser_view) :
    panel_browser_view_(panel_browser_view) {
  PanelBrowserFrameView* frame_view = panel_browser_view_->GetFrameView();
  frame_view->title_label_->SetAutoColorReadabilityEnabled(false);
}

void NativePanelTestingWin::PressLeftMouseButtonTitlebar(
    const gfx::Point& point) {
  panel_browser_view_->OnTitlebarMousePressed(point);
}

void NativePanelTestingWin::ReleaseMouseButtonTitlebar() {
  panel_browser_view_->OnTitlebarMouseReleased();
}

void NativePanelTestingWin::DragTitlebar(int delta_x, int delta_y) {
  gfx::Point new_mouse_location = panel_browser_view_->mouse_location_;
  new_mouse_location.Offset(delta_x, delta_y);

  // Convert from the screen coordinate system to the view's coordinate system
  // since OnTitlebarMouseDragged takes the point in the latter.
  views::View::ConvertPointToView(NULL, panel_browser_view_,
                                  &new_mouse_location);
  panel_browser_view_->OnTitlebarMouseDragged(new_mouse_location);
}

void NativePanelTestingWin::CancelDragTitlebar() {
  panel_browser_view_->OnTitlebarMouseCaptureLost();
}

void NativePanelTestingWin::FinishDragTitlebar() {
  panel_browser_view_->OnTitlebarMouseReleased();
}

bool NativePanelTestingWin::VerifyDrawingAttention() const {
  PanelBrowserFrameView* frame_view = panel_browser_view_->GetFrameView();
  SkColor attention_color = frame_view->GetTitleColor(
      PanelBrowserFrameView::PAINT_FOR_ATTENTION);
  return attention_color == frame_view->title_label_->enabled_color();
}

bool NativePanelTestingWin::VerifyActiveState(bool is_active) {
  PanelBrowserFrameView* frame_view = panel_browser_view_->GetFrameView();

  PanelBrowserFrameView::PaintState expected_paint_state =
      is_active ? PanelBrowserFrameView::PAINT_AS_ACTIVE
                : PanelBrowserFrameView::PAINT_AS_INACTIVE;
  if (frame_view->paint_state_ != expected_paint_state)
    return false;

  SkColor expected_color = frame_view->GetTitleColor(expected_paint_state);
  return expected_color == frame_view->title_label_->enabled_color();
}

bool NativePanelTestingWin::IsWindowSizeKnown() const {
  return true;
}

bool NativePanelTestingWin::IsAnimatingBounds() const {
  return panel_browser_view_->bounds_animator_.get() &&
         panel_browser_view_->bounds_animator_->is_animating();
}
