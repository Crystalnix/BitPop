// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_HTML_DIALOG_UI_H_
#define CHROME_BROWSER_UI_WEBUI_HTML_DIALOG_UI_H_
#pragma once

#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/string16.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_ui_controller.h"
#include "googleurl/src/gurl.h"
#include "ui/base/ui_base_types.h"

struct ContextMenuParams;

namespace base {
class ListValue;
template<class T> class PropertyAccessor;
}

namespace content {
class WebContents;
class WebUIMessageHandler;
}

namespace gfx {
class Size;
}

// Implement this class to receive notifications.
class HtmlDialogUIDelegate {
 public:
  // Returns true if the contents needs to be run in a modal dialog.
  virtual ui::ModalType GetDialogModalType() const = 0;

  // Returns the title of the dialog.
  virtual string16 GetDialogTitle() const = 0;

  // Get the HTML file path for the content to load in the dialog.
  virtual GURL GetDialogContentURL() const = 0;

  // Get WebUIMessageHandler objects to handle messages from the HTML/JS page.
  // The handlers are used to send and receive messages from the page while it
  // is still open.  Ownership of each handler is taken over by the WebUI
  // hosting the page.
  virtual void GetWebUIMessageHandlers(
      std::vector<content::WebUIMessageHandler*>* handlers) const = 0;

  // Get the size of the dialog.
  virtual void GetDialogSize(gfx::Size* size) const = 0;

  // Gets the JSON string input to use when showing the dialog.
  virtual std::string GetDialogArgs() const = 0;

  // A callback to notify the delegate that |source|'s loading state has
  // changed.
  virtual void OnLoadingStateChanged(content::WebContents* source) {}

  // A callback to notify the delegate that the dialog closed.
  // IMPORTANT: Implementations should delete |this| here (unless they've
  // arranged for the delegate to be deleted in some other way, e.g. by
  // registering it as a message handler in the WebUI object).
  virtual void OnDialogClosed(const std::string& json_retval) = 0;

  // A callback to notify the delegate that the contents have gone
  // away. Only relevant if your dialog hosts code that calls
  // windows.close() and you've allowed that.  If the output parameter
  // is set to true, then the dialog is closed.  The default is false.
  virtual void OnCloseContents(content::WebContents* source,
                               bool* out_close_dialog) = 0;

  // A callback to allow the delegate to dictate that the window should not
  // have a title bar.  This is useful when presenting branded interfaces.
  virtual bool ShouldShowDialogTitle() const = 0;

  // A callback to allow the delegate to inhibit context menu or show
  // customized menu.
  // Returns true iff you do NOT want the standard context menu to be
  // shown (because you want to handle it yourself).
  virtual bool HandleContextMenu(const ContextMenuParams& params);

  // A callback to allow the delegate to open a new URL inside |source|.
  // On return |out_new_contents| should contain the WebContents the URL
  // is opened in. Return false to use the default handler.
  virtual bool HandleOpenURLFromTab(content::WebContents* source,
                                    const content::OpenURLParams& params,
                                    content::WebContents** out_new_contents);

  // A callback to create a new tab with |new_contents|. |source| is the
  // WebContent where the operation originated. |disposition| controls how the
  // new tab should be opened. |initial_pos| is the position of the window if a
  // new window is created. |user_gesture| is true if the operation was started
  // by a user gesture. Return false to use the default handler.
  virtual bool HandleAddNewContents(content::WebContents* source,
                                    content::WebContents* new_contents,
                                    WindowOpenDisposition disposition,
                                    const gfx::Rect& initial_pos,
                                    bool user_gesture);

  // Stores the dialog bounds.
  virtual void StoreDialogSize(const gfx::Size& dialog_size) {}

 protected:
  virtual ~HtmlDialogUIDelegate() {}
};

// Displays file URL contents inside a modal HTML dialog.
//
// This application really should not use WebContents + WebUI. It should instead
// just embed a RenderView in a dialog and be done with it.
//
// Before loading a URL corresponding to this WebUI, the caller should set its
// delegate as a property on the WebContents. This WebUI will pick it up from
// there and call it back. This is a bit of a hack to allow the dialog to pass
// its delegate to the Web UI without having nasty accessors on the WebContents.
// The correct design using RVH directly would avoid all of this.
class HtmlDialogUI : public content::WebUIController {
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

  // When created, the property should already be set on the WebContents.
  explicit HtmlDialogUI(content::WebUI* web_ui);
  virtual ~HtmlDialogUI();

  // Close the dialog, passing the specified arguments to the close handler.
  void CloseDialog(const base::ListValue* args);

  // Returns the PropertyBag accessor object used to write the delegate pointer
  // into the WebContents (see class-level comment above).
  static base::PropertyAccessor<HtmlDialogUIDelegate*>& GetPropertyAccessor();

 private:
  // WebUIController
  virtual void RenderViewCreated(RenderViewHost* render_view_host) OVERRIDE;

  // JS message handler.
  void OnDialogClosed(const base::ListValue* args);

  DISALLOW_COPY_AND_ASSIGN(HtmlDialogUI);
};

// Displays external URL contents inside a modal HTML dialog.
//
// Intended to be the place to collect the settings and lockdowns
// necessary for running external UI components securely (e.g., the
// cloud print dialog).
class ExternalHtmlDialogUI : public HtmlDialogUI {
 public:
  explicit ExternalHtmlDialogUI(content::WebUI* web_ui);
  virtual ~ExternalHtmlDialogUI();
};

#endif  // CHROME_BROWSER_UI_WEBUI_HTML_DIALOG_UI_H_
