// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_HTML_DIALOG_UI_H_
#define CHROME_BROWSER_UI_WEBUI_HTML_DIALOG_UI_H_
#pragma once

#include <string>
#include <vector>

#include "content/browser/webui/web_ui.h"
#include "content/common/property_bag.h"
#include "googleurl/src/gurl.h"

namespace gfx {
class Size;
}

struct ContextMenuParams;

// Implement this class to receive notifications.
class HtmlDialogUIDelegate {
 public:
  // Returns true if the contents needs to be run in a modal dialog.
  virtual bool IsDialogModal() const = 0;

  // Returns the title of the dialog.
  virtual std::wstring GetDialogTitle() const = 0;

  // Get the HTML file path for the content to load in the dialog.
  virtual GURL GetDialogContentURL() const = 0;

  // Get WebUIMessageHandler objects to handle messages from the HTML/JS page.
  // The handlers are used to send and receive messages from the page while it
  // is still open.  Ownership of each handler is taken over by the WebUI
  // hosting the page.
  virtual void GetWebUIMessageHandlers(
      std::vector<WebUIMessageHandler*>* handlers) const = 0;

  // Get the size of the dialog.
  virtual void GetDialogSize(gfx::Size* size) const = 0;

  // Gets the JSON string input to use when showing the dialog.
  virtual std::string GetDialogArgs() const = 0;

  // A callback to notify the delegate that the dialog closed.
  virtual void OnDialogClosed(const std::string& json_retval) = 0;

  // Notifies the delegate that the dialog's containing window has been
  // closed, and that OnDialogClosed() will be called shortly.
  // TODO(jamescook): Make this pure virtual.
  virtual void OnWindowClosed();

  // A callback to notify the delegate that the contents have gone
  // away. Only relevant if your dialog hosts code that calls
  // windows.close() and you've allowed that.  If the output parameter
  // is set to true, then the dialog is closed.  The default is false.
  virtual void OnCloseContents(TabContents* source, bool* out_close_dialog) = 0;

  // A callback to allow the delegate to dictate that the window should not
  // have a title bar.  This is useful when presenting branded interfaces.
  virtual bool ShouldShowDialogTitle() const = 0;

  // A callback to allow the delegate to inhibit context menu or show
  // customized menu.
  virtual bool HandleContextMenu(const ContextMenuParams& params);

 protected:
  virtual ~HtmlDialogUIDelegate() {}
};

// Displays file URL contents inside a modal HTML dialog.
//
// This application really should not use TabContents + WebUI. It should instead
// just embed a RenderView in a dialog and be done with it.
//
// Before loading a URL corresponding to this WebUI, the caller should set its
// delegate as a property on the TabContents. This WebUI will pick it up from
// there and call it back. This is a bit of a hack to allow the dialog to pass
// its delegate to the Web UI without having nasty accessors on the TabContents.
// The correct design using RVH directly would avoid all of this.
class HtmlDialogUI : public WebUI {
 public:
  struct HtmlDialogParams {
    // The URL for the content that will be loaded in the dialog.
    GURL url;
    // Width of the dialog.
    int width;
    // Height of the dialog.
    int height;
    // The JSON input to pass to the dialog when showing it.
    std::string json_input;
  };

  // When created, the property should already be set on the TabContents.
  explicit HtmlDialogUI(TabContents* tab_contents);
  virtual ~HtmlDialogUI();

  // Returns the PropertyBag accessor object used to write the delegate pointer
  // into the TabContents (see class-level comment above).
  static PropertyAccessor<HtmlDialogUIDelegate*>& GetPropertyAccessor();

 private:
  // WebUI
  virtual void RenderViewCreated(RenderViewHost* render_view_host);

  // JS message handler.
  void OnDialogClosed(const ListValue* args);

  DISALLOW_COPY_AND_ASSIGN(HtmlDialogUI);
};

// Displays external URL contents inside a modal HTML dialog.
//
// Intended to be the place to collect the settings and lockdowns
// necessary for running external UI conponents securely (e.g., the
// cloud print dialog).
class ExternalHtmlDialogUI : public HtmlDialogUI {
 public:
  explicit ExternalHtmlDialogUI(TabContents* tab_contents);
  virtual ~ExternalHtmlDialogUI();
};

#endif  // CHROME_BROWSER_UI_WEBUI_HTML_DIALOG_UI_H_
