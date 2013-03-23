// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/intents/web_intent_inline_disposition_delegate.h"

#include "base/logging.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/intents/web_intent_picker.h"
#include "chrome/common/extensions/extension_messages.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/renderer_preferences.h"
#include "ipc/ipc_message_macros.h"

WebIntentInlineDispositionDelegate::WebIntentInlineDispositionDelegate(
    WebIntentPicker* picker,
    content::WebContents* contents,
    Browser* browser)
    : picker_(picker),
      web_contents_(contents),
      browser_(browser),
      ALLOW_THIS_IN_INITIALIZER_LIST(
        extension_function_dispatcher_(browser->profile(), this)) {
  content::WebContentsObserver::Observe(web_contents_);
  web_contents_->SetDelegate(this);
  // TODO(groby): Technically, allowing the browser to hande all requests
  // should work just fine. Practically, we're getting a cross-origin warning
  // for a googleapis request.
  // Investigate why OpenURLFromTab delegate misfires on this.
  // web_contents_->GetMutableRendererPrefs()->browser_handles_all_requests =
  //    true;
}

WebIntentInlineDispositionDelegate::~WebIntentInlineDispositionDelegate() {
}

bool WebIntentInlineDispositionDelegate::IsPopupOrPanel(
    const content::WebContents* source) const {
  return true;
}

content::WebContents* WebIntentInlineDispositionDelegate::OpenURLFromTab(
    content::WebContents* source, const content::OpenURLParams& params) {
  DCHECK(source);  // Can only be invoked from inline disposition.

  // Load in place.
  source->GetController().LoadURL(params.url, content::Referrer(),
      content::PAGE_TRANSITION_AUTO_TOPLEVEL, std::string());

  // Remove previous history entries - users should not navigate in intents.
  source->GetController().PruneAllButActive();

  return source;
}

void WebIntentInlineDispositionDelegate::AddNewContents(
    content::WebContents* source,
    content::WebContents* new_contents,
    WindowOpenDisposition disposition,
    const gfx::Rect& initial_pos,
    bool user_gesture,
    bool* was_blocked) {
  DCHECK_EQ(source, web_contents_);
  DCHECK_EQ(Profile::FromBrowserContext(new_contents->GetBrowserContext()),
      browser_->profile());
  // Force all links to open in a new tab, even when different disposition is
  // requested.
  disposition =
      disposition == NEW_BACKGROUND_TAB ? disposition : NEW_FOREGROUND_TAB;
  chrome::AddWebContents(browser_, NULL, new_contents, disposition, initial_pos,
                         user_gesture, was_blocked);
}

void WebIntentInlineDispositionDelegate::LoadingStateChanged(
    content::WebContents* source) {
  if (!source->IsLoading())
    picker_->OnInlineDispositionWebContentsLoaded(source);
}

bool WebIntentInlineDispositionDelegate::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(WebIntentInlineDispositionDelegate, message)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_Request, OnRequest)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void WebIntentInlineDispositionDelegate::RenderViewCreated(
    content::RenderViewHost* render_view_host) {
  render_view_host_ = render_view_host;
  SetRenderViewSizeLimits();
}

void WebIntentInlineDispositionDelegate::DocumentAvailableInMainFrame() {
  std::string css = "body { min-width:400px; }";
  render_view_host_->InsertCSS(string16(), css);
}

content::WebContents* WebIntentInlineDispositionDelegate::
    GetAssociatedWebContents() const {
  return NULL;
}

extensions::WindowController*
WebIntentInlineDispositionDelegate::GetExtensionWindowController() const {
  return NULL;
}

void WebIntentInlineDispositionDelegate::OnRequest(
    const ExtensionHostMsg_Request_Params& params) {
  extension_function_dispatcher_.Dispatch(params,
                                          web_contents_->GetRenderViewHost());
}

void WebIntentInlineDispositionDelegate::ResizeDueToAutoResize(
    content::WebContents* source, const gfx::Size& pref_size) {
  DCHECK(picker_);
  picker_->OnInlineDispositionAutoResize(pref_size);
}

void WebIntentInlineDispositionDelegate::HandleKeyboardEvent(
      content::WebContents* source,
      const content::NativeWebKeyboardEvent& event) {
  picker_->OnInlineDispositionHandleKeyboardEvent(event);
}

void WebIntentInlineDispositionDelegate::SetRenderViewSizeLimits() {
  render_view_host_->EnableAutoResize(
      picker_->GetMinInlineDispositionSize(),
      picker_->GetMaxInlineDispositionSize());
}
