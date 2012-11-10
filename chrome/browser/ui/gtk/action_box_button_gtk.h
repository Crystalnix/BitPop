// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_ACTION_BOX_BUTTON_GTK_H_
#define CHROME_BROWSER_UI_GTK_ACTION_BOX_BUTTON_GTK_H_

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "ui/base/gtk/gtk_signal.h"

class Browser;
class CustomDrawButton;

typedef struct _GtkWidget GtkWidget;

// This class displays the action box button with an associated menu. This is
// where extension actions and the bookmark star live.
class ActionBoxButtonGtk {
 public:
  explicit ActionBoxButtonGtk(Browser* browser);
  virtual ~ActionBoxButtonGtk();

  GtkWidget* widget();

 private:
  // Executes the browser command.
  CHROMEGTK_CALLBACK_0(ActionBoxButtonGtk, void, OnClick);

  scoped_ptr<CustomDrawButton> button_;

  // The browser to which we will send commands.
  Browser* browser_;

  DISALLOW_COPY_AND_ASSIGN(ActionBoxButtonGtk);
};

#endif  // CHROME_BROWSER_UI_GTK_ACTION_BOX_BUTTON_GTK_H_
