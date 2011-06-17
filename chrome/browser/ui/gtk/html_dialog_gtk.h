// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_HTML_DIALOG_GTK_H_
#define CHROME_BROWSER_UI_GTK_HTML_DIALOG_GTK_H_
#pragma once

#include <string>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/webui/html_dialog_tab_contents_delegate.h"
#include "chrome/browser/ui/webui/html_dialog_ui.h"
#include "ui/base/gtk/gtk_signal.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/size.h"

typedef struct _GtkWidget GtkWidget;

class Browser;
class Profile;
class TabContents;
class TabContentsContainerGtk;
class TabContentsWrapper;

class HtmlDialogGtk : public HtmlDialogTabContentsDelegate,
                      public HtmlDialogUIDelegate {
 public:
  HtmlDialogGtk(Profile* profile, HtmlDialogUIDelegate* delegate,
                gfx::NativeWindow parent_window);
  virtual ~HtmlDialogGtk();

  // Initializes the contents of the dialog (the DOMView and the callbacks).
  gfx::NativeWindow InitDialog();

  // Overridden from HtmlDialogUI::Delegate:
  virtual bool IsDialogModal() const;
  virtual std::wstring GetDialogTitle() const;
  virtual GURL GetDialogContentURL() const;
  virtual void GetWebUIMessageHandlers(
      std::vector<WebUIMessageHandler*>* handlers) const;
  virtual void GetDialogSize(gfx::Size* size) const;
  virtual std::string GetDialogArgs() const;
  virtual void OnDialogClosed(const std::string& json_retval);
  virtual void OnCloseContents(TabContents* source, bool* out_close_dialog) { }
  virtual bool ShouldShowDialogTitle() const;

  // Overridden from TabContentsDelegate:
  virtual void MoveContents(TabContents* source, const gfx::Rect& pos);
  virtual void HandleKeyboardEvent(const NativeWebKeyboardEvent& event);

 private:
  CHROMEGTK_CALLBACK_1(HtmlDialogGtk, void, OnResponse, int);

  // This view is a delegate to the HTML content since it needs to get notified
  // about when the dialog is closing. For all other actions (besides dialog
  // closing) we delegate to the creator of this view, which we keep track of
  // using this variable.
  HtmlDialogUIDelegate* delegate_;

  gfx::NativeWindow parent_window_;

  GtkWidget* dialog_;

  scoped_ptr<TabContentsWrapper> tab_;
  scoped_ptr<TabContentsContainerGtk> tab_contents_container_;

  DISALLOW_COPY_AND_ASSIGN(HtmlDialogGtk);
};

#endif  // CHROME_BROWSER_UI_GTK_HTML_DIALOG_GTK_H_
