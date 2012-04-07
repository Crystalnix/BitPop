// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/constrained_html_ui.h"

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/lazy_instance.h"
#include "base/property_bag.h"
#include "base/values.h"
#include "chrome/browser/ui/webui/html_dialog_ui.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"

using content::WebContents;
using content::WebUIMessageHandler;

static base::LazyInstance<base::PropertyAccessor<ConstrainedHtmlUIDelegate*> >
    g_constrained_html_ui_property_accessor = LAZY_INSTANCE_INITIALIZER;

ConstrainedHtmlUI::ConstrainedHtmlUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
}

ConstrainedHtmlUI::~ConstrainedHtmlUI() {
}

void ConstrainedHtmlUI::RenderViewCreated(RenderViewHost* render_view_host) {
  ConstrainedHtmlUIDelegate* delegate = GetConstrainedDelegate();
  if (!delegate)
    return;

  HtmlDialogUIDelegate* dialog_delegate = delegate->GetHtmlDialogUIDelegate();
  std::vector<WebUIMessageHandler*> handlers;
  dialog_delegate->GetWebUIMessageHandlers(&handlers);
  render_view_host->SetWebUIProperty("dialogArguments",
                                     dialog_delegate->GetDialogArgs());
  for (std::vector<WebUIMessageHandler*>::iterator it = handlers.begin();
       it != handlers.end(); ++it) {
    web_ui()->AddMessageHandler(*it);
  }

  // Add a "DialogClose" callback which matches HTMLDialogUI behavior.
  web_ui()->RegisterMessageCallback("DialogClose",
      base::Bind(&ConstrainedHtmlUI::OnDialogCloseMessage,
                 base::Unretained(this)));

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_HTML_DIALOG_SHOWN,
      content::Source<content::WebUI>(web_ui()),
      content::Details<RenderViewHost>(render_view_host));
}

void ConstrainedHtmlUI::OnDialogCloseMessage(const ListValue* args) {
  ConstrainedHtmlUIDelegate* delegate = GetConstrainedDelegate();
  if (!delegate)
    return;

  std::string json_retval;
  if (!args->empty() && !args->GetString(0, &json_retval))
    NOTREACHED() << "Could not read JSON argument";
  delegate->GetHtmlDialogUIDelegate()->OnDialogClosed(json_retval);
  delegate->OnDialogCloseFromWebUI();
}

ConstrainedHtmlUIDelegate* ConstrainedHtmlUI::GetConstrainedDelegate() {
  ConstrainedHtmlUIDelegate** property = GetPropertyAccessor().GetProperty(
      web_ui()->GetWebContents()->GetPropertyBag());
  return property ? *property : NULL;
}

// static
base::PropertyAccessor<ConstrainedHtmlUIDelegate*>&
    ConstrainedHtmlUI::GetPropertyAccessor() {
  return g_constrained_html_ui_property_accessor.Get();
}
