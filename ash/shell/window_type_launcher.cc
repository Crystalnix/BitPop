// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shell/window_type_launcher.h"

#include "ash/shell_window_ids.h"
#include "ash/shell/example_factory.h"
#include "ash/shell/toplevel_window.h"
#include "ash/wm/shadow_types.h"
#include "ash/wm/toplevel_frame_view.h"
#include "base/utf_string_conversions.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/compositor/layer.h"
#include "ui/views/controls/button/text_button.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/examples/examples_window.h"
#include "ui/views/widget/widget.h"

using views::MenuItemView;
using views::MenuRunner;

namespace ash {
namespace shell {

namespace {

SkColor g_colors[] = { SK_ColorRED,
                       SK_ColorYELLOW,
                       SK_ColorBLUE,
                       SK_ColorGREEN };
int g_color_index = 0;

class ModalWindow : public views::WidgetDelegateView,
                    public views::ButtonListener {
 public:
  explicit ModalWindow(ui::ModalType modal_type)
      : modal_type_(modal_type),
        color_(g_colors[g_color_index]),
        ALLOW_THIS_IN_INITIALIZER_LIST(open_button_(
            new views::NativeTextButton(this, ASCIIToUTF16("Moar!")))) {
    ++g_color_index %= arraysize(g_colors);
    AddChildView(open_button_);
  }
  virtual ~ModalWindow() {
  }

  static void OpenModalWindow(aura::Window* parent, ui::ModalType modal_type) {
    views::Widget* widget =
        views::Widget::CreateWindowWithParent(new ModalWindow(modal_type),
                                              parent);
    widget->GetNativeView()->SetName("ModalWindow");
    widget->Show();
  }

  // Overridden from views::View:
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE {
    canvas->FillRect(color_, GetLocalBounds());
  }
  virtual gfx::Size GetPreferredSize() OVERRIDE {
    return gfx::Size(200, 200);
  }
  virtual void Layout() OVERRIDE {
    gfx::Size open_ps = open_button_->GetPreferredSize();
    gfx::Rect local_bounds = GetLocalBounds();
    open_button_->SetBounds(
        5, local_bounds.bottom() - open_ps.height() - 5,
        open_ps.width(), open_ps.height());
  }

  // Overridden from views::WidgetDelegate:
  virtual views::View* GetContentsView() OVERRIDE {
    return this;
  }
  virtual bool CanResize() const OVERRIDE {
    return true;
  }
  virtual string16 GetWindowTitle() const OVERRIDE {
    return ASCIIToUTF16("Modal Window");
  }
  virtual ui::ModalType GetModalType() const OVERRIDE {
    return modal_type_;
  }

  // Overridden from views::ButtonListener:
  virtual void ButtonPressed(views::Button* sender,
                             const views::Event& event) OVERRIDE {
    DCHECK(sender == open_button_);
    OpenModalWindow(GetWidget()->GetNativeView(), modal_type_);
  }

 private:
  ui::ModalType modal_type_;
  SkColor color_;
  views::NativeTextButton* open_button_;

  DISALLOW_COPY_AND_ASSIGN(ModalWindow);
};

class NonModalTransient : public views::WidgetDelegateView {
 public:
  NonModalTransient()
      : color_(g_colors[g_color_index]) {
    ++g_color_index %= arraysize(g_colors);
  }
  virtual ~NonModalTransient() {
  }

  static void OpenNonModalTransient(aura::Window* parent) {
    views::Widget* widget =
        views::Widget::CreateWindowWithParent(new NonModalTransient, parent);
    widget->GetNativeView()->SetName("NonModalTransient");
    widget->Show();
  }

  static void ToggleNonModalTransient(aura::Window* parent) {
    if (!non_modal_transient_) {
      non_modal_transient_ =
          views::Widget::CreateWindowWithParent(new NonModalTransient, parent);
      non_modal_transient_->GetNativeView()->SetName("NonModalTransient");
    }
    if (non_modal_transient_->IsVisible())
      non_modal_transient_->Hide();
    else
      non_modal_transient_->Show();
  }

  // Overridden from views::View:
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE {
    canvas->FillRect(color_, GetLocalBounds());
  }
  virtual gfx::Size GetPreferredSize() OVERRIDE {
    return gfx::Size(250, 250);
  }

  // Overridden from views::WidgetDelegate:
  virtual views::View* GetContentsView() OVERRIDE {
    return this;
  }
  virtual bool CanResize() const OVERRIDE {
    return true;
  }
  virtual string16 GetWindowTitle() const OVERRIDE {
    return ASCIIToUTF16("Non-Modal Transient");
  }
  virtual void DeleteDelegate() OVERRIDE {
    if (GetWidget() == non_modal_transient_)
      non_modal_transient_ = NULL;
  }

 private:
  SkColor color_;

  static views::Widget* non_modal_transient_;

  DISALLOW_COPY_AND_ASSIGN(NonModalTransient);
};

// static
views::Widget* NonModalTransient::non_modal_transient_ = NULL;

}  // namespace

void InitWindowTypeLauncher() {
  views::Widget* widget =
      views::Widget::CreateWindowWithBounds(new WindowTypeLauncher,
                                            gfx::Rect(120, 150, 400, 400));
  widget->GetNativeView()->SetName("WindowTypeLauncher");
  ash::internal::SetShadowType(widget->GetNativeView(),
                               ash::internal::SHADOW_TYPE_NONE);
  widget->Show();
}

WindowTypeLauncher::WindowTypeLauncher()
    : ALLOW_THIS_IN_INITIALIZER_LIST(create_button_(
          new views::NativeTextButton(this, ASCIIToUTF16("Create Window")))),
      ALLOW_THIS_IN_INITIALIZER_LIST(create_nonresizable_button_(
          new views::NativeTextButton(
              this, ASCIIToUTF16("Create Non-Resizable Window")))),
      ALLOW_THIS_IN_INITIALIZER_LIST(bubble_button_(
          new views::NativeTextButton(
              this, ASCIIToUTF16("Create Pointy Bubble")))),
      ALLOW_THIS_IN_INITIALIZER_LIST(lock_button_(
          new views::NativeTextButton(this, ASCIIToUTF16("Lock Screen")))),
      ALLOW_THIS_IN_INITIALIZER_LIST(widgets_button_(
          new views::NativeTextButton(
              this, ASCIIToUTF16("Show Example Widgets")))),
      ALLOW_THIS_IN_INITIALIZER_LIST(system_modal_button_(
          new views::NativeTextButton(
              this, ASCIIToUTF16("Open System Modal Window")))),
      ALLOW_THIS_IN_INITIALIZER_LIST(window_modal_button_(
          new views::NativeTextButton(
              this, ASCIIToUTF16("Open Window Modal Window")))),
      ALLOW_THIS_IN_INITIALIZER_LIST(transient_button_(
          new views::NativeTextButton(
              this, ASCIIToUTF16("Open Non-Modal Transient Window")))),
      ALLOW_THIS_IN_INITIALIZER_LIST(examples_button_(
          new views::NativeTextButton(
              this, ASCIIToUTF16("Open Views Examples Window")))),
      ALLOW_THIS_IN_INITIALIZER_LIST(show_hide_window_button_(
          new views::NativeTextButton(
              this, ASCIIToUTF16("Show/Hide a Window")))) {
  AddChildView(create_button_);
  AddChildView(create_nonresizable_button_);
  AddChildView(bubble_button_);
  AddChildView(lock_button_);
  AddChildView(widgets_button_);
  AddChildView(system_modal_button_);
  AddChildView(window_modal_button_);
  AddChildView(transient_button_);
  AddChildView(examples_button_);
  AddChildView(show_hide_window_button_);
#if !defined(OS_MACOSX)
  set_context_menu_controller(this);
#endif
}

WindowTypeLauncher::~WindowTypeLauncher() {
}

void WindowTypeLauncher::OnPaint(gfx::Canvas* canvas) {
  canvas->FillRect(SK_ColorWHITE, GetLocalBounds());
}

void WindowTypeLauncher::Layout() {
  gfx::Size create_button_ps = create_button_->GetPreferredSize();
  gfx::Rect local_bounds = GetLocalBounds();
  create_button_->SetBounds(
      5, local_bounds.bottom() - create_button_ps.height() - 5,
      create_button_ps.width(), create_button_ps.height());

  gfx::Size bubble_button_ps = bubble_button_->GetPreferredSize();
  bubble_button_->SetBounds(
      5, create_button_->y() - bubble_button_ps.height() - 5,
      bubble_button_ps.width(), bubble_button_ps.height());

  gfx::Size create_nr_button_ps =
      create_nonresizable_button_->GetPreferredSize();
  create_nonresizable_button_->SetBounds(
      5, bubble_button_->y() - create_nr_button_ps.height() - 5,
      create_nr_button_ps.width(), create_nr_button_ps.height());

  gfx::Size lock_ps = lock_button_->GetPreferredSize();
  lock_button_->SetBounds(
      5, create_nonresizable_button_->y() - lock_ps.height() - 5,
      lock_ps.width(), lock_ps.height());

  gfx::Size widgets_ps = widgets_button_->GetPreferredSize();
  widgets_button_->SetBounds(
      5, lock_button_->y() - widgets_ps.height() - 5,
      widgets_ps.width(), widgets_ps.height());

  gfx::Size system_modal_ps = system_modal_button_->GetPreferredSize();
  system_modal_button_->SetBounds(
      5, widgets_button_->y() - system_modal_ps.height() - 5,
      system_modal_ps.width(), system_modal_ps.height());

  gfx::Size window_modal_ps = window_modal_button_->GetPreferredSize();
  window_modal_button_->SetBounds(
      5, system_modal_button_->y() - window_modal_ps.height() - 5,
      window_modal_ps.width(), window_modal_ps.height());

  gfx::Size transient_ps = transient_button_->GetPreferredSize();
  transient_button_->SetBounds(
      5, window_modal_button_->y() - transient_ps.height() - 5,
      transient_ps.width(), transient_ps.height());

  gfx::Size examples_ps = examples_button_->GetPreferredSize();
  examples_button_->SetBounds(
      5, transient_button_->y() - examples_ps.height() - 5,
      examples_ps.width(), examples_ps.height());

  gfx::Size show_hide_window_ps =
      show_hide_window_button_->GetPreferredSize();
  show_hide_window_button_->SetBounds(
      5, examples_button_->y() - show_hide_window_ps.height() - 5,
      show_hide_window_ps.width(), show_hide_window_ps.height());
}

bool WindowTypeLauncher::OnMousePressed(const views::MouseEvent& event) {
  // Overridden so we get OnMouseReleased and can show the context menu.
  return true;
}

views::View* WindowTypeLauncher::GetContentsView() {
  return this;
}

bool WindowTypeLauncher::CanResize() const {
  return true;
}

string16 WindowTypeLauncher::GetWindowTitle() const {
  return ASCIIToUTF16("Examples: Window Builder");
}

views::NonClientFrameView* WindowTypeLauncher::CreateNonClientFrameView() {
  return new ash::internal::ToplevelFrameView;
}

void WindowTypeLauncher::ButtonPressed(views::Button* sender,
                                       const views::Event& event) {
  if (sender == create_button_) {
    ToplevelWindow::CreateParams params;
    params.can_resize = true;
    ToplevelWindow::CreateToplevelWindow(params);
  } else if (sender == create_nonresizable_button_) {
    ToplevelWindow::CreateToplevelWindow(ToplevelWindow::CreateParams());
  } else if (sender == bubble_button_) {
    CreatePointyBubble(sender);
  } else if (sender == lock_button_) {
    CreateLockScreen();
  } else if (sender == widgets_button_) {
    CreateWidgetsWindow();
  } else if (sender == system_modal_button_) {
    ModalWindow::OpenModalWindow(GetWidget()->GetNativeView(),
                                 ui::MODAL_TYPE_SYSTEM);
  } else if (sender == window_modal_button_) {
    ModalWindow::OpenModalWindow(GetWidget()->GetNativeView(),
                                 ui::MODAL_TYPE_WINDOW);
  } else if (sender == transient_button_) {
    NonModalTransient::OpenNonModalTransient(GetWidget()->GetNativeView());
  } else if (sender == show_hide_window_button_) {
    NonModalTransient::ToggleNonModalTransient(GetWidget()->GetNativeView());
  }
#if !defined(OS_MACOSX)
  else if (sender == examples_button_) {
    views::examples::ShowExamplesWindow(false);
  }
#endif  // !defined(OS_MACOSX)
}

#if !defined(OS_MACOSX)
void WindowTypeLauncher::ExecuteCommand(int id) {
  switch (id) {
    case COMMAND_NEW_WINDOW:
      InitWindowTypeLauncher();
      break;
    case COMMAND_TOGGLE_FULLSCREEN:
      GetWidget()->SetFullscreen(!GetWidget()->IsFullscreen());
      break;
    default:
      break;
  }
}
#endif  // !defined(OS_MACOSX)

#if !defined(OS_MACOSX)
void WindowTypeLauncher::ShowContextMenuForView(views::View* source,
                                                const gfx::Point& p,
                                                bool is_mouse_gesture) {
  MenuItemView* root = new MenuItemView(this);
  root->AppendMenuItem(COMMAND_NEW_WINDOW,
                       ASCIIToUTF16("New Window"),
                       MenuItemView::NORMAL);
  root->AppendMenuItem(COMMAND_TOGGLE_FULLSCREEN,
                       ASCIIToUTF16("Toggle FullScreen"),
                       MenuItemView::NORMAL);
  // MenuRunner takes ownership of root.
  menu_runner_.reset(new MenuRunner(root));
  if (menu_runner_->RunMenuAt(GetWidget(), NULL, gfx::Rect(p, gfx::Size(0, 0)),
        MenuItemView::TOPLEFT,
        MenuRunner::HAS_MNEMONICS) == MenuRunner::MENU_DELETED)
    return;
}
#endif  // !defined(OS_MACOSX)

}  // namespace shell
}  // namespace ash
