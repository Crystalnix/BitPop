// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/importer/import_lock_dialog_gtk.h"

#include <gtk/gtk.h>

#include "base/message_loop.h"
#include "chrome/browser/importer/importer_host.h"
#include "chrome/browser/importer/importer_lock_dialog.h"
#include "chrome/browser/metrics/user_metrics.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace importer {

void ShowImportLockDialog(gfx::NativeWindow parent,
                          ImporterHost* importer_host) {
  ImportLockDialogGtk::Show(parent, importer_host);
  UserMetrics::RecordAction(UserMetricsAction("ImportLockDialogGtk_Shown"));
}

}  // namespace importer

// static
void ImportLockDialogGtk::Show(GtkWindow* parent, ImporterHost* importer_host) {
  new ImportLockDialogGtk(parent, importer_host);
}

ImportLockDialogGtk::ImportLockDialogGtk(GtkWindow* parent,
                                         ImporterHost* importer_host)
    : importer_host_(importer_host) {
  // Build the dialog.
  dialog_ = gtk_dialog_new_with_buttons(
      l10n_util::GetStringUTF8(IDS_IMPORTER_LOCK_TITLE).c_str(),
      parent,
      (GtkDialogFlags) (GTK_DIALOG_MODAL | GTK_DIALOG_NO_SEPARATOR),
      NULL);

  gtk_util::AddButtonToDialog(dialog_,
      l10n_util::GetStringUTF8(IDS_IMPORTER_LOCK_CANCEL).c_str(),
      GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT);
  gtk_util::AddButtonToDialog(dialog_,
      l10n_util::GetStringUTF8(IDS_IMPORTER_LOCK_OK).c_str(),
      GTK_STOCK_OK, GTK_RESPONSE_ACCEPT);

  GtkWidget* content_area = GTK_DIALOG(dialog_)->vbox;
  gtk_box_set_spacing(GTK_BOX(content_area), gtk_util::kContentAreaSpacing);
  GtkWidget* label = gtk_label_new(
      l10n_util::GetStringUTF8(IDS_IMPORTER_LOCK_TEXT).c_str());
  gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
  gtk_box_pack_start(GTK_BOX(content_area), label, FALSE, FALSE, 0);

  g_signal_connect(dialog_, "response", G_CALLBACK(OnResponseThunk), this);
  gtk_window_set_resizable(GTK_WINDOW(dialog_), FALSE);
  gtk_widget_show_all(dialog_);
}

ImportLockDialogGtk::~ImportLockDialogGtk() {}

void ImportLockDialogGtk::OnResponse(GtkWidget* dialog, int response_id) {
  if (response_id == GTK_RESPONSE_ACCEPT) {
    MessageLoop::current()->PostTask(FROM_HERE, NewRunnableMethod(
        importer_host_.get(), &ImporterHost::OnImportLockDialogEnd, true));
  } else {
    MessageLoop::current()->PostTask(FROM_HERE, NewRunnableMethod(
        importer_host_.get(), &ImporterHost::OnImportLockDialogEnd, false));
  }
  gtk_widget_destroy(dialog_);
  delete this;
}
