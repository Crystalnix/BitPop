// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/focus_store_gtk.h"

#include <gtk/gtk.h>

#include "chrome/browser/platform_util.h"

FocusStoreGtk::FocusStoreGtk()
    : widget_(NULL),
      destroy_handler_id_(0) {
}

FocusStoreGtk::~FocusStoreGtk() {
  DisconnectDestroyHandler();
}

void FocusStoreGtk::Store(GtkWidget* widget) {
  GtkWidget* focus_widget = NULL;
  if (widget) {
    GtkWindow* window = platform_util::GetTopLevel(widget);
    if (window)
      focus_widget = window->focus_widget;
  }

  SetWidget(focus_widget);
}

void FocusStoreGtk::SetWidget(GtkWidget* widget) {
  DisconnectDestroyHandler();

  // We don't add a ref. The signal handler below effectively gives us a weak
  // reference.
  widget_ = widget;
  if (widget_) {
    // When invoked, |gtk_widget_destroyed| will set |widget_| to NULL.
    destroy_handler_id_ = g_signal_connect(widget_, "destroy",
                                           G_CALLBACK(gtk_widget_destroyed),
                                           &widget_);
  }
}

void FocusStoreGtk::DisconnectDestroyHandler() {
  if (widget_) {
    g_signal_handler_disconnect(widget_, destroy_handler_id_);
    widget_ = NULL;
  }
}
