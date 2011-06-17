// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/widget/widget_win.h"

#include <dwmapi.h>

#include "base/string_util.h"
#include "base/win/windows_version.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drag_source.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/dragdrop/os_exchange_data_provider_win.h"
#include "ui/base/keycodes/keyboard_code_conversion_win.h"
#include "ui/base/l10n/l10n_util_win.h"
#include "ui/base/system_monitor/system_monitor.h"
#include "ui/base/theme_provider.h"
#include "ui/base/view_prop.h"
#include "ui/base/win/hwnd_util.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/native_theme_win.h"
#include "ui/gfx/path.h"
#include "views/accessibility/native_view_accessibility_win.h"
#include "views/controls/native_control_win.h"
#include "views/controls/textfield/native_textfield_views.h"
#include "views/focus/accelerator_handler.h"
#include "views/focus/focus_util_win.h"
#include "views/focus/view_storage.h"
#include "views/ime/input_method_win.h"
#include "views/views_delegate.h"
#include "views/widget/aero_tooltip_manager.h"
#include "views/widget/child_window_message_processor.h"
#include "views/widget/drop_target_win.h"
#include "views/widget/native_widget_delegate.h"
#include "views/widget/root_view.h"
#include "views/widget/widget_delegate.h"
#include "views/window/window_win.h"

#pragma comment(lib, "dwmapi.lib")

using ui::ViewProp;

namespace views {

namespace {

// Returns whether the specified window is the current active window.
bool IsWindowActive(HWND hwnd) {
  WINDOWINFO info;
  return ::GetWindowInfo(hwnd, &info) &&
         ((info.dwWindowStatus & WS_ACTIVECAPTION) != 0);
}

// Get the source HWND of the specified message. Depending on the message, the
// source HWND is encoded in either the WPARAM or the LPARAM value.
HWND GetControlHWNDForMessage(UINT message, WPARAM w_param, LPARAM l_param) {
  // Each of the following messages can be sent by a child HWND and must be
  // forwarded to its associated NativeControlWin for handling.
  switch (message) {
    case WM_NOTIFY:
      return reinterpret_cast<NMHDR*>(l_param)->hwndFrom;
    case WM_COMMAND:
      return reinterpret_cast<HWND>(l_param);
    case WM_CONTEXTMENU:
      return reinterpret_cast<HWND>(w_param);
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
      return reinterpret_cast<HWND>(l_param);
  }
  return NULL;
}

// Some messages may be sent to us by a child HWND. If this is the case, this
// function will forward those messages on to the object associated with the
// source HWND and return true, in which case the window procedure must not do
// any further processing of the message. If there is no associated
// ChildWindowMessageProcessor, the return value will be false and the WndProc
// can continue processing the message normally.  |l_result| contains the result
// of the message processing by the control and must be returned by the WndProc
// if the return value is true.
bool ProcessChildWindowMessage(UINT message,
                               WPARAM w_param,
                               LPARAM l_param,
                               LRESULT* l_result) {
  *l_result = 0;

  HWND control_hwnd = GetControlHWNDForMessage(message, w_param, l_param);
  if (IsWindow(control_hwnd)) {
    ChildWindowMessageProcessor* processor =
        ChildWindowMessageProcessor::Get(control_hwnd);
    if (processor)
      return processor->ProcessMessage(message, w_param, l_param, l_result);
  }

  return false;
}

// Enumeration callback for NativeWidget::GetAllNativeWidgets(). Called for each
// child HWND beneath the original HWND.
BOOL CALLBACK EnumerateChildWindowsForNativeWidgets(HWND hwnd, LPARAM l_param) {
  NativeWidget* native_widget =
      NativeWidget::GetNativeWidgetForNativeView(hwnd);
  if (native_widget) {
    NativeWidget::NativeWidgets* native_widgets =
        reinterpret_cast<NativeWidget::NativeWidgets*>(l_param);
    native_widgets->insert(native_widget);
  }
  return TRUE;
}

// Returns true if the WINDOWPOS data provided indicates the client area of
// the window may have changed size. This can be caused by the window being
// resized or its frame changing.
bool DidClientAreaSizeChange(const WINDOWPOS* window_pos) {
  return !(window_pos->flags & SWP_NOSIZE) ||
         window_pos->flags & SWP_FRAMECHANGED;
}

// Links the HWND to its NativeWidget.
const char* const kNativeWidgetKey = "__VIEWS_NATIVE_WIDGET__";

// A custom MSAA object id used to determine if a screen reader is actively
// listening for MSAA events.
const int kCustomObjectID = 1;

}  // namespace

// static
bool WidgetWin::screen_reader_active_ = false;

////////////////////////////////////////////////////////////////////////////////
// WidgetWin, public:

WidgetWin::WidgetWin()
    : ALLOW_THIS_IN_INITIALIZER_LIST(delegate_(this)),
      close_widget_factory_(this),
      active_mouse_tracking_flags_(0),
      use_layered_buffer_(false),
      layered_alpha_(255),
      ALLOW_THIS_IN_INITIALIZER_LIST(paint_layered_window_factory_(this)),
      delete_on_destroy_(true),
      can_update_layered_window_(true),
      is_window_(false),
      restore_focus_when_enabled_(false),
      accessibility_view_events_index_(-1),
      accessibility_view_events_(kMaxAccessibilityViewEvents),
      previous_cursor_(NULL),
      is_input_method_win_(false) {
  set_native_widget(this);
}

WidgetWin::~WidgetWin() {
  // We need to delete the input method before calling DestroyRootView(),
  // because it'll set focus_manager_ to NULL.
  input_method_.reset();
  DestroyRootView();
}

// static
bool WidgetWin::IsAeroGlassEnabled() {
  if (base::win::GetVersion() < base::win::VERSION_VISTA)
    return false;
  // If composition is not enabled, we behave like on XP.
  BOOL enabled = FALSE;
  return SUCCEEDED(DwmIsCompositionEnabled(&enabled)) && enabled;
}

View* WidgetWin::GetAccessibilityViewEventAt(int id) {
  // Convert from MSAA child id.
  id = -(id + 1);
  DCHECK(id >= 0 && id < kMaxAccessibilityViewEvents);
  return accessibility_view_events_[id];
}

int WidgetWin::AddAccessibilityViewEvent(View* view) {
  accessibility_view_events_index_ =
      (accessibility_view_events_index_ + 1) % kMaxAccessibilityViewEvents;
  accessibility_view_events_[accessibility_view_events_index_] = view;

  // Convert to MSAA child id.
  return -(accessibility_view_events_index_ + 1);
}

void WidgetWin::ClearAccessibilityViewEvent(View* view) {
  for (std::vector<View*>::iterator it = accessibility_view_events_.begin();
      it != accessibility_view_events_.end();
      ++it) {
    if (*it == view)
      *it = NULL;
  }
}

////////////////////////////////////////////////////////////////////////////////
// WidgetWin, Widget implementation:

void WidgetWin::Init(gfx::NativeView parent, const gfx::Rect& bounds) {
  Widget::Init(parent, bounds);

  // Create the window.
  WindowImpl::Init(parent, bounds);
}

void WidgetWin::InitWithWidget(Widget* parent, const gfx::Rect& bounds) {
  Init(parent->GetNativeView(), bounds);
}

gfx::NativeView WidgetWin::GetNativeView() const {
  return WindowImpl::hwnd();
}

bool WidgetWin::GetAccelerator(int cmd_id, ui::Accelerator* accelerator) {
  return false;
}

Window* WidgetWin::GetWindow() {
  return GetWindowImpl(hwnd());
}

const Window* WidgetWin::GetWindow() const {
  return GetWindowImpl(hwnd());
}

void WidgetWin::ViewHierarchyChanged(bool is_add, View* parent,
                                     View* child) {
  Widget::ViewHierarchyChanged(is_add, parent, child);
  if (drop_target_.get())
    drop_target_->ResetTargetViewIfEquals(child);

  if (!is_add)
    ClearAccessibilityViewEvent(child);
}

////////////////////////////////////////////////////////////////////////////////
// WidgetWin, NativeWidget implementation:

void WidgetWin::SetCreateParams(const CreateParams& params) {
  DCHECK(!GetNativeView());

  // Set non-style attributes.
  set_delete_on_destroy(params.delete_on_destroy);

  DWORD style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
  DWORD ex_style = 0;
  DWORD class_style = CS_DBLCLKS;

  // Set type-independent style attributes.
  if (params.child)
    style |= WS_CHILD | WS_VISIBLE;
  if (!params.accept_events)
    ex_style |= WS_EX_TRANSPARENT;
  if (!params.can_activate)
    ex_style |= WS_EX_NOACTIVATE;
  if (params.keep_on_top)
    ex_style |= WS_EX_TOPMOST;
  if (params.mirror_origin_in_rtl)
    ex_style |= l10n_util::GetExtendedTooltipStyles();
  if (params.transparent)
    ex_style |= WS_EX_LAYERED;
  if (params.has_dropshadow) {
    class_style |= (base::win::GetVersion() < base::win::VERSION_XP) ?
        0 : CS_DROPSHADOW;
  }

  // Set type-dependent style attributes.
  switch (params.type) {
    case CreateParams::TYPE_WINDOW:
    case CreateParams::TYPE_CONTROL:
      break;
    case CreateParams::TYPE_POPUP:
      style |= WS_POPUP;
      ex_style |= WS_EX_TOOLWINDOW;
      break;
    case CreateParams::TYPE_MENU:
      style |= WS_POPUP;
      is_mouse_button_pressed_ =
          ((GetKeyState(VK_LBUTTON) & 0x80) ||
          (GetKeyState(VK_RBUTTON) & 0x80) ||
          (GetKeyState(VK_MBUTTON) & 0x80) ||
          (GetKeyState(VK_XBUTTON1) & 0x80) ||
          (GetKeyState(VK_XBUTTON2) & 0x80));
      break;
    default:
      NOTREACHED();
  }

  set_initial_class_style(class_style);
  set_window_style(style);
  set_window_ex_style(ex_style);
}

Widget* WidgetWin::GetWidget() {
  return this;
}

void WidgetWin::SetNativeWindowProperty(const char* name, void* value) {
  // Remove the existing property (if any).
  for (ViewProps::iterator i = props_.begin(); i != props_.end(); ++i) {
    if ((*i)->Key() == name) {
      props_.erase(i);
      break;
    }
  }

  if (value)
    props_.push_back(new ViewProp(hwnd(), name, value));
}

void* WidgetWin::GetNativeWindowProperty(const char* name) {
  return ViewProp::GetValue(hwnd(), name);
}

TooltipManager* WidgetWin::GetTooltipManager() const {
  return tooltip_manager_.get();
}

bool WidgetWin::IsScreenReaderActive() const {
  return screen_reader_active_;
}

void WidgetWin::SetMouseCapture() {
  DCHECK(!HasMouseCapture());
  SetCapture(hwnd());
}

void WidgetWin::ReleaseMouseCapture() {
  ReleaseCapture();
}

bool WidgetWin::HasMouseCapture() const {
  return GetCapture() == hwnd();
}

InputMethod* WidgetWin::GetInputMethodNative() {
  return input_method_.get();
}

void WidgetWin::ReplaceInputMethod(InputMethod* input_method) {
  input_method_.reset(input_method);
  if (input_method) {
    input_method->set_delegate(this);
    input_method->Init(GetWidget());
  }
  is_input_method_win_ = false;
}

gfx::Rect WidgetWin::GetWindowScreenBounds() const {
  RECT r;
  GetWindowRect(&r);
  return gfx::Rect(r);
}

gfx::Rect WidgetWin::GetClientAreaScreenBounds() const {
  RECT r;
  GetClientRect(&r);
  POINT point = { r.left, r.top };
  ClientToScreen(hwnd(), &point);
  return gfx::Rect(point.x, point.y, r.right - r.left, r.bottom - r.top);
}

void WidgetWin::SetBounds(const gfx::Rect& bounds) {
  LONG style = GetWindowLong(GWL_STYLE);
  if (style & WS_MAXIMIZE)
    SetWindowLong(GWL_STYLE, style & ~WS_MAXIMIZE);
  SetWindowPos(NULL, bounds.x(), bounds.y(), bounds.width(), bounds.height(),
               SWP_NOACTIVATE | SWP_NOZORDER);
}

void WidgetWin::SetSize(const gfx::Size& size) {
  SetWindowPos(NULL, 0, 0, size.width(), size.height(),
               SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE);
}

void WidgetWin::MoveAbove(gfx::NativeView native_view) {
  SetWindowPos(native_view, 0, 0, 0, 0,
               SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
}

void WidgetWin::SetShape(gfx::NativeRegion region) {
  SetWindowRgn(region, TRUE);
}

void WidgetWin::Close() {
  if (!IsWindow())
    return;  // No need to do anything.

  // Let's hide ourselves right away.
  Hide();

  if (close_widget_factory_.empty()) {
    // And we delay the close so that if we are called from an ATL callback,
    // we don't destroy the window before the callback returned (as the caller
    // may delete ourselves on destroy and the ATL callback would still
    // dereference us when the callback returns).
    MessageLoop::current()->PostTask(FROM_HERE,
        close_widget_factory_.NewRunnableMethod(
            &WidgetWin::CloseNow));
  }
}

void WidgetWin::CloseNow() {
  // Destroys the input method before closing the window so that it can be
  // detached from the widget correctly.
  input_method_.reset();
  is_input_method_win_ = false;

  // We may already have been destroyed if the selection resulted in a tab
  // switch which will have reactivated the browser window and closed us, so
  // we need to check to see if we're still a window before trying to destroy
  // ourself.
  if (IsWindow())
    DestroyWindow(hwnd());
}

void WidgetWin::Show() {
  if (IsWindow())
    ShowWindow(SW_SHOWNOACTIVATE);
}

void WidgetWin::Hide() {
  if (IsWindow()) {
    // NOTE: Be careful not to activate any windows here (for example, calling
    // ShowWindow(SW_HIDE) will automatically activate another window).  This
    // code can be called while a window is being deactivated, and activating
    // another window will screw up the activation that is already in progress.
    SetWindowPos(NULL, 0, 0, 0, 0,
                 SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOMOVE |
                 SWP_NOREPOSITION | SWP_NOSIZE | SWP_NOZORDER);
  }
}

void WidgetWin::SetOpacity(unsigned char opacity) {
  layered_alpha_ = static_cast<BYTE>(opacity);
}

void WidgetWin::SetAlwaysOnTop(bool on_top) {
  if (on_top)
    set_window_ex_style(window_ex_style() | WS_EX_TOPMOST);
  else
    set_window_ex_style(window_ex_style() & ~WS_EX_TOPMOST);
}

bool WidgetWin::IsVisible() const {
  return !!::IsWindowVisible(hwnd());
}

bool WidgetWin::IsActive() const {
  return IsWindowActive(hwnd());
}

bool WidgetWin::IsAccessibleWidget() const {
  return screen_reader_active_;
}

bool WidgetWin::ContainsNativeView(gfx::NativeView native_view) const {
  return hwnd() == native_view || ::IsChild(hwnd(), native_view);
}

void WidgetWin::RunShellDrag(View* view,
                             const ui::OSExchangeData& data,
                             int operation) {
  scoped_refptr<ui::DragSource> drag_source(new ui::DragSource);
  DWORD effects;
  DoDragDrop(ui::OSExchangeDataProviderWin::GetIDataObject(data), drag_source,
             ui::DragDropTypes::DragOperationToDropEffect(operation), &effects);
}

void WidgetWin::SchedulePaintInRect(const gfx::Rect& rect) {
  if (use_layered_buffer_) {
    // We must update the back-buffer immediately, since Windows' handling of
    // invalid rects is somewhat mysterious.
    layered_window_invalid_rect_ = layered_window_invalid_rect_.Union(rect);

    // In some situations, such as drag and drop, when Windows itself runs a
    // nested message loop our message loop appears to be starved and we don't
    // receive calls to DidProcessMessage(). This only seems to affect layered
    // windows, so we schedule a redraw manually using a task, since those never
    // seem to be starved. Also, wtf.
    if (paint_layered_window_factory_.empty()) {
      MessageLoop::current()->PostTask(FROM_HERE,
          paint_layered_window_factory_.NewRunnableMethod(
          &WidgetWin::RedrawLayeredWindowContents));
    }
  } else {
    // InvalidateRect() expects client coordinates.
    RECT r = rect.ToRECT();
    InvalidateRect(hwnd(), &r, FALSE);
  }
}

void WidgetWin::SetCursor(gfx::NativeCursor cursor) {
  if (cursor) {
    previous_cursor_ = ::SetCursor(cursor);
  } else if (previous_cursor_) {
    ::SetCursor(previous_cursor_);
    previous_cursor_ = NULL;
  }
}

void WidgetWin::NotifyAccessibilityEvent(
    View* view,
    ui::AccessibilityTypes::Event event_type,
    bool send_native_event) {
  // Send the notification to the delegate.
  if (ViewsDelegate::views_delegate)
    ViewsDelegate::views_delegate->NotifyAccessibilityEvent(view, event_type);

  // Now call the Windows-specific method to notify MSAA clients of this
  // event.  The widget gives us a temporary unique child ID to associate
  // with this view so that clients can call get_accChild in
  // NativeViewAccessibilityWin to retrieve the IAccessible associated
  // with this view.
  if (send_native_event) {
    int child_id = AddAccessibilityViewEvent(view);
    ::NotifyWinEvent(NativeViewAccessibilityWin::MSAAEvent(event_type),
                     GetNativeView(), OBJID_CLIENT, child_id);
  }
}

////////////////////////////////////////////////////////////////////////////////
// WidgetWin, MessageLoop::Observer implementation:

void WidgetWin::WillProcessMessage(const MSG& msg) {
}

void WidgetWin::DidProcessMessage(const MSG& msg) {
  RedrawInvalidRect();
}

////////////////////////////////////////////////////////////////////////////////
// WidgetWin, WindowImpl overrides:

HICON WidgetWin::GetDefaultWindowIcon() const {
  if (ViewsDelegate::views_delegate)
    return ViewsDelegate::views_delegate->GetDefaultWindowIcon();
  return NULL;
}

LRESULT WidgetWin::OnWndProc(UINT message, WPARAM w_param, LPARAM l_param) {
  HWND window = hwnd();
  LRESULT result = 0;

  // First allow messages sent by child controls to be processed directly by
  // their associated views. If such a view is present, it will handle the
  // message *instead of* this WidgetWin.
  if (ProcessChildWindowMessage(message, w_param, l_param, &result))
    return result;

  // Otherwise we handle everything else.
  if (!ProcessWindowMessage(window, message, w_param, l_param, result))
    result = DefWindowProc(window, message, w_param, l_param);
  if (message == WM_NCDESTROY) {
    MessageLoopForUI::current()->RemoveObserver(this);
    OnFinalMessage(window);
  }
  if (message == WM_ACTIVATE)
    PostProcessActivateMessage(this, LOWORD(w_param));
  if (message == WM_ENABLE && restore_focus_when_enabled_) {
    restore_focus_when_enabled_ = false;
    GetFocusManager()->RestoreFocusedView();
  }
  return result;
}

////////////////////////////////////////////////////////////////////////////////
// WidgetWin, protected:

// Message handlers ------------------------------------------------------------

void WidgetWin::OnActivate(UINT action, BOOL minimized, HWND window) {
  SetMsgHandled(FALSE);
}

void WidgetWin::OnActivateApp(BOOL active, DWORD thread_id) {
  SetMsgHandled(FALSE);
}

LRESULT WidgetWin::OnAppCommand(HWND window, short app_command, WORD device,
                                int keystate) {
  SetMsgHandled(FALSE);
  return 0;
}

void WidgetWin::OnCancelMode() {
}

void WidgetWin::OnCaptureChanged(HWND hwnd) {
  delegate_->OnMouseCaptureLost();
}

void WidgetWin::OnClose() {
  Close();
}

void WidgetWin::OnCommand(UINT notification_code, int command_id, HWND window) {
  SetMsgHandled(FALSE);
}

LRESULT WidgetWin::OnCreate(CREATESTRUCT* create_struct) {
  SetNativeWindowProperty(kNativeWidgetKey, this);
  CHECK_EQ(this, GetNativeWidgetForNativeView(hwnd()));

  use_layered_buffer_ = !!(window_ex_style() & WS_EX_LAYERED);

  // Attempt to detect screen readers by sending an event with our custom id.
  if (!IsAccessibleWidget())
    NotifyWinEvent(EVENT_SYSTEM_ALERT, hwnd(), kCustomObjectID, CHILDID_SELF);

  props_.push_back(SetWindowSupportsRerouteMouseWheel(hwnd()));

  drop_target_ = new DropTargetWin(GetRootView());

  // We need to add ourselves as a message loop observer so that we can repaint
  // aggressively if the contents of our window become invalid. Unfortunately
  // WM_PAINT messages are starved and we get flickery redrawing when resizing
  // if we do not do this.
  MessageLoopForUI::current()->AddObserver(this);

  // Windows special DWM window frame requires a special tooltip manager so
  // that window controls in Chrome windows don't flicker when you move your
  // mouse over them. See comment in aero_tooltip_manager.h.
  if (GetThemeProvider()->ShouldUseNativeFrame()) {
    tooltip_manager_.reset(new AeroTooltipManager(this));
  } else {
    tooltip_manager_.reset(new TooltipManagerWin(this));
  }

  // This message initializes the window so that focus border are shown for
  // windows.
  SendMessage(
      hwnd(), WM_CHANGEUISTATE, MAKELPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);

  // Bug 964884: detach the IME attached to this window.
  // We should attach IMEs only when we need to input CJK strings.
  ImmAssociateContextEx(hwnd(), NULL, 0);

  // We need to allow the delegate to size its contents since the window may not
  // receive a size notification when its initial bounds are specified at window
  // creation time.
  ClientAreaSizeChanged();

  delegate_->OnNativeWidgetCreated();

  // delegate_->OnNativeWidgetCreated() creates the focus manager for top-level
  // widget. Only top-level widget should have an input method.
  if (delegate_->HasFocusManager() &&
      NativeTextfieldViews::IsTextfieldViewsEnabled()) {
    input_method_.reset(new InputMethodWin(this));
    input_method_->Init(GetWidget());
    is_input_method_win_ = true;
  }
  return 0;
}

void WidgetWin::OnDestroy() {
  if (drop_target_.get()) {
    RevokeDragDrop(hwnd());
    drop_target_ = NULL;
  }

  props_.reset();
}

void WidgetWin::OnDisplayChange(UINT bits_per_pixel, CSize screen_size) {
  if (widget_delegate())
    widget_delegate()->OnDisplayChanged();
}

LRESULT WidgetWin::OnDwmCompositionChanged(UINT msg,
                                           WPARAM w_param,
                                           LPARAM l_param) {
  SetMsgHandled(FALSE);
  return 0;
}

void WidgetWin::OnEndSession(BOOL ending, UINT logoff) {
  SetMsgHandled(FALSE);
}

void WidgetWin::OnEnterSizeMove() {
  SetMsgHandled(FALSE);
}

LRESULT WidgetWin::OnEraseBkgnd(HDC dc) {
  // This is needed for magical win32 flicker ju-ju.
  return 1;
}

void WidgetWin::OnExitMenuLoop(BOOL is_track_popup_menu) {
  SetMsgHandled(FALSE);
}

void WidgetWin::OnExitSizeMove() {
  SetMsgHandled(FALSE);
}

LRESULT WidgetWin::OnGetObject(UINT uMsg, WPARAM w_param, LPARAM l_param) {
  LRESULT reference_result = static_cast<LRESULT>(0L);

  // Accessibility readers will send an OBJID_CLIENT message
  if (OBJID_CLIENT == l_param) {
    // Retrieve MSAA dispatch object for the root view.
    base::win::ScopedComPtr<IAccessible> root(
        NativeViewAccessibilityWin::GetAccessibleForView(GetRootView()));

    // Create a reference that MSAA will marshall to the client.
    reference_result = LresultFromObject(IID_IAccessible, w_param,
        static_cast<IAccessible*>(root.Detach()));
  }

  if (kCustomObjectID == l_param) {
    // An MSAA client requestes our custom id. Assume that we have detected an
    // active windows screen reader.
    OnScreenReaderDetected();

    // Return with failure.
    return static_cast<LRESULT>(0L);
  }

  return reference_result;
}

void WidgetWin::OnGetMinMaxInfo(MINMAXINFO* minmax_info) {
  SetMsgHandled(FALSE);
}

void WidgetWin::OnHScroll(int scroll_type, short position, HWND scrollbar) {
  SetMsgHandled(FALSE);
}

LRESULT WidgetWin::OnImeMessages(UINT message, WPARAM w_param, LPARAM l_param) {
  if (!is_input_method_win_) {
    SetMsgHandled(FALSE);
    return 0;
  }

  InputMethodWin* ime = static_cast<InputMethodWin*>(input_method_.get());
  BOOL handled = FALSE;
  LRESULT result = 0;
  switch (message) {
    case WM_IME_SETCONTEXT:
      result = ime->OnImeSetContext(message, w_param, l_param, &handled);
      break;
    case WM_IME_STARTCOMPOSITION:
      result = ime->OnImeStartComposition(message, w_param, l_param, &handled);
      break;
    case WM_IME_COMPOSITION:
      result = ime->OnImeComposition(message, w_param, l_param, &handled);
      break;
    case WM_IME_ENDCOMPOSITION:
      result = ime->OnImeEndComposition(message, w_param, l_param, &handled);
      break;
    case WM_CHAR:
    case WM_SYSCHAR:
      result = ime->OnChar(message, w_param, l_param, &handled);
      break;
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
      result = ime->OnDeadChar(message, w_param, l_param, &handled);
      break;
    default:
      NOTREACHED() << "Unknown IME message:" << message;
      break;
  }

  SetMsgHandled(handled);
  return result;
}

void WidgetWin::OnInitMenu(HMENU menu) {
  SetMsgHandled(FALSE);
}

void WidgetWin::OnInitMenuPopup(HMENU menu,
                                UINT position,
                                BOOL is_system_menu) {
  SetMsgHandled(FALSE);
}

void WidgetWin::OnInputLangChange(DWORD character_set, HKL input_language_id) {
  if (is_input_method_win_) {
    static_cast<InputMethodWin*>(input_method_.get())->OnInputLangChange(
        character_set, input_language_id);
  }
}

LRESULT WidgetWin::OnKeyEvent(UINT message, WPARAM w_param, LPARAM l_param) {
  MSG msg = { hwnd(), message, w_param, l_param };
  KeyEvent key(msg);
  if (input_method_.get())
    input_method_->DispatchKeyEvent(key);
  else
    DispatchKeyEventPostIME(key);
  return 0;
}

void WidgetWin::OnKillFocus(HWND focused_window) {
  delegate_->OnNativeBlur(focused_window);
  if (input_method_.get())
    input_method_->OnBlur();
  SetMsgHandled(FALSE);
}

LRESULT WidgetWin::OnMouseActivate(UINT message,
                                   WPARAM w_param,
                                   LPARAM l_param) {
  if (GetWindowLong(GWL_EXSTYLE) & WS_EX_NOACTIVATE)
    return MA_NOACTIVATE;
  SetMsgHandled(FALSE);
  return MA_ACTIVATE;
}

LRESULT WidgetWin::OnMouseRange(UINT message, WPARAM w_param, LPARAM l_param) {
  MSG msg = { hwnd(), message, w_param, l_param, 0,
              { GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) } };
  MouseEvent event(msg);

  if (!(event.flags() & ui::EF_IS_NON_CLIENT))
    tooltip_manager_->OnMouse(message, w_param, l_param);

  if (event.type() == ui::ET_MOUSE_MOVED && !HasMouseCapture()) {
    // Windows only fires WM_MOUSELEAVE events if the application begins
    // "tracking" mouse events for a given HWND during WM_MOUSEMOVE events.
    // We need to call |TrackMouseEvents| to listen for WM_MOUSELEAVE.
    TrackMouseEvents((message == WM_NCMOUSEMOVE) ?
        TME_NONCLIENT | TME_LEAVE : TME_LEAVE);
  } else if (event.type() == ui::ET_MOUSE_EXITED) {
    // Reset our tracking flags so future mouse movement over this WidgetWin
    // results in a new tracking session. Fall through for OnMouseEvent.
    active_mouse_tracking_flags_ = 0;
  } else if (event.type() == ui::ET_MOUSEWHEEL) {
    // Reroute the mouse wheel to the window under the pointer if applicable.
    return (views::RerouteMouseWheel(hwnd(), w_param, l_param) ||
            delegate_->OnMouseEvent(MouseWheelEvent(msg))) ? 0 : 1;
  }

  SetMsgHandled(delegate_->OnMouseEvent(event));
  return 0;
}

void WidgetWin::OnMove(const CPoint& point) {
  if (widget_delegate())
    widget_delegate()->OnWidgetMove();
  SetMsgHandled(FALSE);
}

void WidgetWin::OnMoving(UINT param, const LPRECT new_bounds) {
  if (widget_delegate())
    widget_delegate()->OnWidgetMove();
}

LRESULT WidgetWin::OnNCActivate(BOOL active) {
  SetMsgHandled(FALSE);
  return 0;
}

LRESULT WidgetWin::OnNCCalcSize(BOOL w_param, LPARAM l_param) {
  SetMsgHandled(FALSE);
  return 0;
}

LRESULT WidgetWin::OnNCHitTest(const CPoint& pt) {
  SetMsgHandled(FALSE);
  return 0;
}

void WidgetWin::OnNCPaint(HRGN rgn) {
  SetMsgHandled(FALSE);
}

LRESULT WidgetWin::OnNCUAHDrawCaption(UINT msg,
                                      WPARAM w_param,
                                      LPARAM l_param) {
  SetMsgHandled(FALSE);
  return 0;
}

LRESULT WidgetWin::OnNCUAHDrawFrame(UINT msg, WPARAM w_param, LPARAM l_param) {
  SetMsgHandled(FALSE);
  return 0;
}

LRESULT WidgetWin::OnNotify(int w_param, NMHDR* l_param) {
  // We can be sent this message before the tooltip manager is created, if a
  // subclass overrides OnCreate and creates some kind of Windows control there
  // that sends WM_NOTIFY messages.
  if (tooltip_manager_.get()) {
    bool handled;
    LRESULT result = tooltip_manager_->OnNotify(w_param, l_param, &handled);
    SetMsgHandled(handled);
    return result;
  }
  SetMsgHandled(FALSE);
  return 0;
}

void WidgetWin::OnPaint(HDC dc) {
  scoped_ptr<gfx::CanvasPaint> canvas(
      gfx::CanvasPaint::CreateCanvasPaint(hwnd()));
  delegate_->OnNativeWidgetPaint(canvas->AsCanvas());
}

LRESULT WidgetWin::OnPowerBroadcast(DWORD power_event, DWORD data) {
  ui::SystemMonitor* monitor = ui::SystemMonitor::Get();
  if (monitor)
    monitor->ProcessWmPowerBroadcastMessage(power_event);
  SetMsgHandled(FALSE);
  return 0;
}

LRESULT WidgetWin::OnReflectedMessage(UINT msg,
                                      WPARAM w_param,
                                      LPARAM l_param) {
  SetMsgHandled(FALSE);
  return 0;
}

void WidgetWin::OnSetFocus(HWND focused_window) {
  delegate_->OnNativeFocus(focused_window);
  if (input_method_.get())
    input_method_->OnFocus();
  SetMsgHandled(FALSE);
}

LRESULT WidgetWin::OnSetIcon(UINT size_type, HICON new_icon) {
  SetMsgHandled(FALSE);
  return 0;
}

LRESULT WidgetWin::OnSetText(const wchar_t* text) {
  SetMsgHandled(FALSE);
  return 0;
}

void WidgetWin::OnSettingChange(UINT flags, const wchar_t* section) {
  if (flags == SPI_SETWORKAREA && widget_delegate())
    widget_delegate()->OnWorkAreaChanged();
  SetMsgHandled(FALSE);
}

void WidgetWin::OnSize(UINT param, const CSize& size) {
  SetMsgHandled(FALSE);
}

void WidgetWin::OnSysCommand(UINT notification_code, CPoint click) {
}

void WidgetWin::OnThemeChanged() {
  // Notify NativeThemeWin.
  gfx::NativeThemeWin::instance()->CloseHandles();
}

void WidgetWin::OnVScroll(int scroll_type, short position, HWND scrollbar) {
  SetMsgHandled(FALSE);
}

void WidgetWin::OnWindowPosChanging(WINDOWPOS* window_pos) {
  SetMsgHandled(FALSE);
}

void WidgetWin::OnWindowPosChanged(WINDOWPOS* window_pos) {
  if (DidClientAreaSizeChange(window_pos))
    ClientAreaSizeChanged();
  SetMsgHandled(FALSE);
}

void WidgetWin::OnFinalMessage(HWND window) {
  if (delete_on_destroy_)
    delete this;
}

////////////////////////////////////////////////////////////////////////////////
// WidgetWin, protected:

void WidgetWin::TrackMouseEvents(DWORD mouse_tracking_flags) {
  // Begin tracking mouse events for this HWND so that we get WM_MOUSELEAVE
  // when the user moves the mouse outside this HWND's bounds.
  if (active_mouse_tracking_flags_ == 0 || mouse_tracking_flags & TME_CANCEL) {
    if (mouse_tracking_flags & TME_CANCEL) {
      // We're about to cancel active mouse tracking, so empty out the stored
      // state.
      active_mouse_tracking_flags_ = 0;
    } else {
      active_mouse_tracking_flags_ = mouse_tracking_flags;
    }

    TRACKMOUSEEVENT tme;
    tme.cbSize = sizeof(tme);
    tme.dwFlags = mouse_tracking_flags;
    tme.hwndTrack = hwnd();
    tme.dwHoverTime = 0;
    TrackMouseEvent(&tme);
  } else if (mouse_tracking_flags != active_mouse_tracking_flags_) {
    TrackMouseEvents(active_mouse_tracking_flags_ | TME_CANCEL);
    TrackMouseEvents(mouse_tracking_flags);
  }
}

void WidgetWin::OnScreenReaderDetected() {
  screen_reader_active_ = true;
}

////////////////////////////////////////////////////////////////////////////////
// WidgetWin, private:

// static
Window* WidgetWin::GetWindowImpl(HWND hwnd) {
  // NOTE: we can't use GetAncestor here as constrained windows are a Window,
  // but not a top level window.
  HWND parent = hwnd;
  while (parent) {
    WidgetWin* widget =
        reinterpret_cast<WidgetWin*>(ui::GetWindowUserData(parent));
    if (widget && widget->is_window_)
      return static_cast<WindowWin*>(widget);
    parent = ::GetParent(parent);
  }
  return NULL;
}

RootView* WidgetWin::GetFocusedViewRootView() {
  // TODO(beng): get rid of this
  FocusManager* focus_manager = GetFocusManager();
  if (!focus_manager) {
    NOTREACHED();
    return NULL;
  }
  View* focused_view = focus_manager->GetFocusedView();
  if (!focused_view)
    return NULL;
  return focused_view->GetRootView();
}

// static
void WidgetWin::PostProcessActivateMessage(WidgetWin* widget,
                                           int activation_state) {
  if (!widget->delegate_->HasFocusManager()) {
    NOTREACHED();
    return;
  }
  FocusManager* focus_manager = widget->GetWidget()->GetFocusManager();
  if (WA_INACTIVE == activation_state) {
    // We might get activated/inactivated without being enabled, so we need to
    // clear restore_focus_when_enabled_.
    widget->restore_focus_when_enabled_ = false;
    focus_manager->StoreFocusedView();
  } else {
    // We must restore the focus after the message has been DefProc'ed as it
    // does set the focus to the last focused HWND.
    // Note that if the window is not enabled, we cannot restore the focus as
    // calling ::SetFocus on a child of the non-enabled top-window would fail.
    // This is the case when showing a modal dialog (such as 'open file',
    // 'print'...) from a different thread.
    // In that case we delay the focus restoration to when the window is enabled
    // again.
    if (!IsWindowEnabled(widget->GetNativeView())) {
      DCHECK(!widget->restore_focus_when_enabled_);
      widget->restore_focus_when_enabled_ = true;
      return;
    }
    focus_manager->RestoreFocusedView();
  }
}

void WidgetWin::RedrawInvalidRect() {
  if (!use_layered_buffer_) {
    RECT r = { 0, 0, 0, 0 };
    if (GetUpdateRect(hwnd(), &r, FALSE) && !IsRectEmpty(&r)) {
      RedrawWindow(hwnd(), &r, NULL,
                   RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
    }
  }
}

void WidgetWin::RedrawLayeredWindowContents() {
  if (layered_window_invalid_rect_.IsEmpty())
    return;

  // We need to clip to the dirty rect ourselves.
  layered_window_contents_->save(SkCanvas::kClip_SaveFlag);
  layered_window_contents_->ClipRectInt(layered_window_invalid_rect_.x(),
                                        layered_window_invalid_rect_.y(),
                                        layered_window_invalid_rect_.width(),
                                        layered_window_invalid_rect_.height());
  GetRootView()->Paint(layered_window_contents_.get());
  layered_window_contents_->restore();

  RECT wr;
  GetWindowRect(&wr);
  SIZE size = {wr.right - wr.left, wr.bottom - wr.top};
  POINT position = {wr.left, wr.top};
  HDC dib_dc = layered_window_contents_->beginPlatformPaint();
  POINT zero = {0, 0};
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, layered_alpha_, AC_SRC_ALPHA};
  UpdateLayeredWindow(hwnd(), NULL, &position, &size, dib_dc, &zero,
                      RGB(0xFF, 0xFF, 0xFF), &blend, ULW_ALPHA);
  layered_window_invalid_rect_.SetRect(0, 0, 0, 0);
  layered_window_contents_->endPlatformPaint();
}

void WidgetWin::ClientAreaSizeChanged() {
  RECT r;
  Window* window = GetWindow();
  if (IsZoomed() || (window && window->ShouldUseNativeFrame()))
    GetClientRect(&r);
  else
    GetWindowRect(&r);
  gfx::Size s(std::max(0, static_cast<int>(r.right - r.left)),
              std::max(0, static_cast<int>(r.bottom - r.top)));
  delegate_->OnSizeChanged(s);
  if (use_layered_buffer_) {
    layered_window_contents_.reset(
        new gfx::CanvasSkia(s.width(), s.height(), false));
  }
}

gfx::AcceleratedWidget WidgetWin::GetAcceleratedWidget() {
  // TODO(beng):
  return gfx::kNullAcceleratedWidget;
}

void WidgetWin::DispatchKeyEventPostIME(const KeyEvent& key) {
  RootView* root_view = GetFocusedViewRootView();
  if (!root_view)
    root_view = GetRootView();

  SetMsgHandled(root_view->ProcessKeyEvent(key));
}

////////////////////////////////////////////////////////////////////////////////
// Widget, public:

// static
Widget* Widget::CreateWidget(const CreateParams& params) {
  WidgetWin* widget = new WidgetWin;
  widget->SetCreateParams(params);
  return widget;
}

// static
void Widget::NotifyLocaleChanged() {
  NOTIMPLEMENTED();
}

bool Widget::ConvertRect(const Widget* source,
                         const Widget* target,
                         gfx::Rect* rect) {
  DCHECK(source);
  DCHECK(target);
  DCHECK(rect);

  HWND source_hwnd = source->GetNativeView();
  HWND target_hwnd = target->GetNativeView();
  if (source_hwnd == target_hwnd)
    return true;

  RECT win_rect = rect->ToRECT();
  if (::MapWindowPoints(source_hwnd, target_hwnd,
                        reinterpret_cast<LPPOINT>(&win_rect),
                        sizeof(RECT)/sizeof(POINT))) {
    *rect = win_rect;
    return true;
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidget, public:

NativeWidget* NativeWidget::GetNativeWidgetForNativeView(
    gfx::NativeView native_view) {
  return reinterpret_cast<WidgetWin*>(
      ViewProp::GetValue(native_view, kNativeWidgetKey));
}

NativeWidget* NativeWidget::GetNativeWidgetForNativeWindow(
    gfx::NativeWindow native_window) {
  return GetNativeWidgetForNativeView(native_window);
}

NativeWidget* NativeWidget::GetTopLevelNativeWidget(
    gfx::NativeView native_view) {
  if (!native_view)
    return NULL;

  // First, check if the top-level window is a Widget.
  HWND root = ::GetAncestor(native_view, GA_ROOT);
  if (!root)
    return NULL;

  NativeWidget* widget = GetNativeWidgetForNativeView(root);
  if (widget)
    return widget;

  // Second, try to locate the last Widget window in the parent hierarchy.
  HWND parent_hwnd = native_view;
  NativeWidget* parent_widget;
  do {
    parent_widget = GetNativeWidgetForNativeView(parent_hwnd);
    if (parent_widget) {
      widget = parent_widget;
      parent_hwnd = ::GetAncestor(parent_hwnd, GA_PARENT);
    }
  } while (parent_hwnd != NULL && parent_widget != NULL);

  return widget;
}

void NativeWidget::GetAllNativeWidgets(gfx::NativeView native_view,
                                       NativeWidgets* children) {
  if (!native_view)
    return;

  NativeWidget* native_widget = GetNativeWidgetForNativeView(native_view);
  if (native_widget)
    children->insert(native_widget);
  EnumChildWindows(native_view, EnumerateChildWindowsForNativeWidgets,
      reinterpret_cast<LPARAM>(children));
}

void NativeWidget::ReparentNativeView(gfx::NativeView native_view,
                                      gfx::NativeView new_parent) {
  if (!native_view)
    return;

  HWND previous_parent = ::GetParent(native_view);
  if (previous_parent == new_parent)
    return;

  NativeWidgets widgets;
  GetAllNativeWidgets(native_view, &widgets);

  // First notify all the widgets that they are being disassociated
  // from their previous parent.
  for (NativeWidgets::iterator it = widgets.begin();
       it != widgets.end(); ++it) {
    // TODO(beng): Rename this notification to NotifyNativeViewChanging()
    // and eliminate the bool parameter.
    (*it)->GetWidget()->NotifyNativeViewHierarchyChanged(false,
                                                         previous_parent);
  }

  ::SetParent(native_view, new_parent);

  // And now, notify them that they have a brand new parent.
  for (NativeWidgets::iterator it = widgets.begin();
       it != widgets.end(); ++it) {
    (*it)->GetWidget()->NotifyNativeViewHierarchyChanged(true,
                                                         new_parent);
  }
}

}  // namespace views
