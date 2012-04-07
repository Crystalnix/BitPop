// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/simple_message_box_views.h"

#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/simple_message_box.h"
#include "chrome/browser/ui/dialog_style.h"
#include "chrome/browser/ui/views/window.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/message_box_view.h"
#include "ui/views/widget/widget.h"

#if defined(USE_AURA)
#include "ui/views/focus/accelerator_handler.h"
#endif

namespace browser {

void ShowErrorBox(gfx::NativeWindow parent,
                  const string16& title,
                  const string16& message) {
  SimpleMessageBoxViews::ShowErrorBox(parent, title, message);
}

bool ShowYesNoBox(gfx::NativeWindow parent,
                  const string16& title,
                  const string16& message) {
  return SimpleMessageBoxViews::ShowYesNoBox(parent, title, message);
}

}  // namespace browser

////////////////////////////////////////////////////////////////////////////////
// SimpleMessageBoxViews, public:

// static
void SimpleMessageBoxViews::ShowErrorBox(gfx::NativeWindow parent_window,
                                         const string16& title,
                                         const string16& message) {
  // This is a reference counted object so it is given an initial increment
  // in the constructor with a corresponding decrement in DeleteDelegate().
  new SimpleMessageBoxViews(parent_window, DIALOG_ERROR, title, message);
}

bool SimpleMessageBoxViews::ShowYesNoBox(gfx::NativeWindow parent_window,
                                         const string16& title,
                                         const string16& message) {
  // This is a reference counted object so it is given an initial increment
  // in the constructor plus an extra one below to ensure the dialog persists
  // until we retrieve the user response..
  scoped_refptr<SimpleMessageBoxViews> dialog =
      new SimpleMessageBoxViews(parent_window, DIALOG_YES_NO, title, message);

  // Make sure Chrome doesn't attempt to shut down with the dialog up.
  g_browser_process->AddRefModule();

  bool old_state = MessageLoopForUI::current()->NestableTasksAllowed();
  MessageLoopForUI::current()->SetNestableTasksAllowed(true);
  MessageLoopForUI::current()->RunWithDispatcher(dialog);
  MessageLoopForUI::current()->SetNestableTasksAllowed(old_state);

  g_browser_process->ReleaseModule();

  return dialog->Accepted();
}

bool SimpleMessageBoxViews::Cancel() {
  disposition_ = DISPOSITION_CANCEL;
  return true;
}

bool SimpleMessageBoxViews::Accept() {
  disposition_ = DISPOSITION_OK;
  return true;
}

int SimpleMessageBoxViews::GetDialogButtons() const {
  if (type_ == DIALOG_ERROR)
    return ui::DIALOG_BUTTON_OK;
  return ui::DIALOG_BUTTON_OK | ui::DIALOG_BUTTON_CANCEL;
}

string16 SimpleMessageBoxViews::GetDialogButtonLabel(
    ui::DialogButton button) const {
  return button == ui::DIALOG_BUTTON_OK ? l10n_util::GetStringUTF16(IDS_OK)
                                        : l10n_util::GetStringUTF16(IDS_CLOSE);
}

bool SimpleMessageBoxViews::ShouldShowWindowTitle() const {
  return true;
}

string16 SimpleMessageBoxViews::GetWindowTitle() const {
  return message_box_title_;
}

void SimpleMessageBoxViews::DeleteDelegate() {
  Release();
}

ui::ModalType SimpleMessageBoxViews::GetModalType() const {
  return ui::MODAL_TYPE_WINDOW;
}

views::View* SimpleMessageBoxViews::GetContentsView() {
  return message_box_view_;
}

views::Widget* SimpleMessageBoxViews::GetWidget() {
  return message_box_view_->GetWidget();
}

const views::Widget* SimpleMessageBoxViews::GetWidget() const {
  return message_box_view_->GetWidget();
}

////////////////////////////////////////////////////////////////////////////////
// SimpleMessageBoxViews, private:

SimpleMessageBoxViews::SimpleMessageBoxViews(gfx::NativeWindow parent_window,
                                             DialogType type,
                                             const string16& title,
                                             const string16& message)
    : type_(type),
      disposition_(DISPOSITION_UNKNOWN) {
  message_box_title_ = title;
  message_box_view_ = new views::MessageBoxView(
      views::MessageBoxView::NO_OPTIONS,
      message,
      string16());
  browser::CreateViewsWindow(parent_window, this, STYLE_GENERIC)->Show();

  // Add reference to be released in DeleteDelegate().
  AddRef();
}

SimpleMessageBoxViews::~SimpleMessageBoxViews() {
}

#if defined(OS_WIN)
bool SimpleMessageBoxViews::Dispatch(const MSG& msg) {
  TranslateMessage(&msg);
  DispatchMessage(&msg);
  return disposition_ == DISPOSITION_UNKNOWN;
}
#elif defined(USE_AURA)
base::MessagePumpDispatcher::DispatchStatus
    SimpleMessageBoxViews::Dispatch(XEvent* xev) {
  if (!views::DispatchXEvent(xev))
    return EVENT_IGNORED;

  if (disposition_ == DISPOSITION_UNKNOWN)
    return base::MessagePumpDispatcher::EVENT_PROCESSED;
  return base::MessagePumpDispatcher::EVENT_QUIT;
}
#else
bool SimpleMessageBoxViews::Dispatch(GdkEvent* event) {
  gtk_main_do_event(event);
  return disposition_ == DISPOSITION_UNKNOWN;
}
#endif
