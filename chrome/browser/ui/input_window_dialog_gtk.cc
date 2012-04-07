// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/input_window_dialog_gtk.h"

#include <gtk/gtk.h>
#include "base/message_loop.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "ui/base/gtk/gtk_signal.h"

InputWindowDialogGtk::InputWindowDialogGtk(GtkWindow* parent,
                                           const std::string& window_title,
                                           const std::string& label,
                                           const std::string& contents,
                                           Delegate* delegate,
                                           ButtonType type)
    : dialog_(gtk_dialog_new_with_buttons(
                  window_title.c_str(),
                  parent,
                  GTK_DIALOG_MODAL,
                  GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                  type == BUTTON_TYPE_ADD ? GTK_STOCK_ADD : GTK_STOCK_SAVE,
                  GTK_RESPONSE_ACCEPT,
                  NULL)),
      delegate_(delegate) {
  gtk_dialog_set_default_response(GTK_DIALOG(dialog_), GTK_RESPONSE_ACCEPT);
#if !GTK_CHECK_VERSION(2, 22, 0)
  gtk_dialog_set_has_separator(GTK_DIALOG(dialog_), FALSE);
#endif
  gtk_window_set_resizable(GTK_WINDOW(dialog_), FALSE);

  GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog_));
  gtk_box_set_spacing(GTK_BOX(content_area), 18);

  GtkWidget* hbox = gtk_hbox_new(FALSE, 6);
  GtkWidget* label_widget = gtk_label_new(label.c_str());
  gtk_box_pack_start(GTK_BOX(hbox), label_widget, FALSE, FALSE, 0);

  input_ = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(input_), contents.c_str());
  g_signal_connect(input_, "changed",
                   G_CALLBACK(OnEntryChangedThunk), this);
  g_object_set(G_OBJECT(input_), "activates-default", TRUE, NULL);
  gtk_box_pack_start(GTK_BOX(hbox), input_, TRUE, TRUE, 0);

  gtk_widget_show_all(hbox);

  gtk_box_pack_start(GTK_BOX(content_area), hbox, FALSE, FALSE, 0);

  g_signal_connect(dialog_, "response",
                   G_CALLBACK(OnResponseThunk), this);
  g_signal_connect(dialog_, "delete-event",
                   G_CALLBACK(OnWindowDeleteEventThunk), this);
  g_signal_connect(dialog_, "destroy",
                   G_CALLBACK(OnWindowDestroyThunk), this);
}

InputWindowDialogGtk::~InputWindowDialogGtk() {
}

void InputWindowDialogGtk::Show() {
  gtk_util::ShowDialog(dialog_);
}

void InputWindowDialogGtk::Close() {
  // Under the model that we've inherited from Windows, dialogs can receive
  // more than one Close() call inside the current message loop event.
  if (dialog_) {
    gtk_widget_destroy(dialog_);
    dialog_ = NULL;
  }
}

void InputWindowDialogGtk::OnEntryChanged(GtkEditable* entry) {
  string16 value(UTF8ToUTF16(gtk_entry_get_text(GTK_ENTRY(entry))));
  InputWindowDialog::InputTexts texts;
  texts.push_back(value);
  gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog_),
                                    GTK_RESPONSE_ACCEPT,
                                    delegate_->IsValid(texts));
}

void InputWindowDialogGtk::OnResponse(GtkWidget* dialog, int response_id) {
  if (response_id == GTK_RESPONSE_ACCEPT) {
    string16 value(UTF8ToUTF16(gtk_entry_get_text(GTK_ENTRY(input_))));
    InputTexts texts;
    texts.push_back(value);
    delegate_->InputAccepted(texts);
  } else {
    delegate_->InputCanceled();
  }
  Close();
}

gboolean InputWindowDialogGtk::OnWindowDeleteEvent(GtkWidget* widget,
                                                   GdkEvent* event) {
  Close();

  // Return true to prevent the gtk dialog from being destroyed. Close will
  // destroy it for us and the default gtk_dialog_delete_event_handler() will
  // force the destruction without us being able to stop it.
  return TRUE;
}

void InputWindowDialogGtk::OnWindowDestroy(GtkWidget* widget) {
  MessageLoop::current()->DeleteSoon(FROM_HERE, this);
}
