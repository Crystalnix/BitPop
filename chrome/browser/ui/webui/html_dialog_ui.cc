// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/html_dialog_ui.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/lazy_instance.h"
#include "base/property_bag.h"
#include "base/values.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/common/bindings_policy.h"

using content::WebUIMessageHandler;

static base::LazyInstance<base::PropertyAccessor<HtmlDialogUIDelegate*> >
    g_html_dialog_ui_property_accessor = LAZY_INSTANCE_INITIALIZER;

HtmlDialogUI::HtmlDialogUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
}

HtmlDialogUI::~HtmlDialogUI() {
  // Don't unregister our property. During the teardown of the WebContents,
  // this will be deleted, but the WebContents will already be destroyed.
  //
  // This object is owned indirectly by the WebContents. WebUIs can change, so
  // it's scary if this WebUI is changed out and replaced with something else,
  // since the property will still point to the old delegate. But the delegate
  // is itself the owner of the WebContents for a dialog so will be in scope,
  // and the HTML dialogs won't swap WebUIs anyway since they don't navigate.
}

void HtmlDialogUI::CloseDialog(const base::ListValue* args) {
  OnDialogClosed(args);
}

// static
base::PropertyAccessor<HtmlDialogUIDelegate*>&
    HtmlDialogUI::GetPropertyAccessor() {
  return g_html_dialog_ui_property_accessor.Get();
}

////////////////////////////////////////////////////////////////////////////////
// Private:

void HtmlDialogUI::RenderViewCreated(RenderViewHost* render_view_host) {
  // Hook up the javascript function calls, also known as chrome.send("foo")
  // calls in the HTML, to the actual C++ functions.
  web_ui()->RegisterMessageCallback("DialogClose",
      base::Bind(&HtmlDialogUI::OnDialogClosed, base::Unretained(this)));

  // Pass the arguments to the renderer supplied by the delegate.
  std::string dialog_args;
  std::vector<WebUIMessageHandler*> handlers;
  HtmlDialogUIDelegate** delegate = GetPropertyAccessor().GetProperty(
      web_ui()->GetWebContents()->GetPropertyBag());
  if (delegate) {
    dialog_args = (*delegate)->GetDialogArgs();
    (*delegate)->GetWebUIMessageHandlers(&handlers);
  }

  if (0 != (web_ui()->GetBindings() & content::BINDINGS_POLICY_WEB_UI))
    render_view_host->SetWebUIProperty("dialogArguments", dialog_args);
  for (std::vector<WebUIMessageHandler*>::iterator it = handlers.begin();
       it != handlers.end(); ++it) {
    web_ui()->AddMessageHandler(*it);
  }

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_HTML_DIALOG_SHOWN,
      content::Source<content::WebUI>(web_ui()),
      content::Details<RenderViewHost>(render_view_host));
}

void HtmlDialogUI::OnDialogClosed(const ListValue* args) {
  HtmlDialogUIDelegate** delegate = GetPropertyAccessor().GetProperty(
      web_ui()->GetWebContents()->GetPropertyBag());
  if (delegate) {
    std::string json_retval;
    if (args && !args->empty() && !args->GetString(0, &json_retval))
      NOTREACHED() << "Could not read JSON argument";

    (*delegate)->OnDialogClosed(json_retval);
  }
}

ExternalHtmlDialogUI::ExternalHtmlDialogUI(content::WebUI* web_ui)
    : HtmlDialogUI(web_ui) {
  // Non-file based UI needs to not have access to the Web UI bindings
  // for security reasons. The code hosting the dialog should provide
  // dialog specific functionality through other bindings and methods
  // that are scoped in duration to the dialogs existence.
  web_ui->SetBindings(web_ui->GetBindings() & ~content::BINDINGS_POLICY_WEB_UI);
}

ExternalHtmlDialogUI::~ExternalHtmlDialogUI() {
}

bool HtmlDialogUIDelegate::HandleContextMenu(const ContextMenuParams& params) {
  return false;
}

bool HtmlDialogUIDelegate::HandleOpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    content::WebContents** out_new_contents) {
  return false;
}

bool HtmlDialogUIDelegate::HandleAddNewContents(
    content::WebContents* source,
    content::WebContents* new_contents,
    WindowOpenDisposition disposition,
    const gfx::Rect& initial_pos,
    bool user_gesture) {
  return false;
}
