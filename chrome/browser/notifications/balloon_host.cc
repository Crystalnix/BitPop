// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/balloon_host.h"
#include "chrome/browser/notifications/balloon.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_preferences_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/webui/chrome_web_ui_controller_factory.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "chrome/common/chrome_view_type.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/common/renderer_preferences.h"
#include "ipc/ipc_message.h"
#include "webkit/glue/webpreferences.h"

using content::SiteInstance;
using content::WebContents;

BalloonHost::BalloonHost(Balloon* balloon)
    : balloon_(balloon),
      initialized_(false),
      should_notify_on_disconnect_(false),
      enable_web_ui_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          extension_function_dispatcher_(balloon_->profile(), this)) {
  site_instance_ = SiteInstance::CreateForURL(
      balloon_->profile(), balloon_->notification().content_url());
}

void BalloonHost::Shutdown() {
  NotifyDisconnect();
  web_contents_.reset();
}

Browser* BalloonHost::GetBrowser() {
  // Notifications aren't associated with a particular browser.
  return NULL;
}

content::WebContents* BalloonHost::GetAssociatedWebContents() const {
  return NULL;
}

const string16& BalloonHost::GetSource() const {
  return balloon_->notification().display_source();
}

void BalloonHost::CloseContents(WebContents* source) {
  balloon_->CloseByScript();
  NotifyDisconnect();
}

void BalloonHost::HandleMouseDown() {
  balloon_->OnClick();
}

void BalloonHost::UpdatePreferredSize(WebContents* source,
                                      const gfx::Size& pref_size) {
  balloon_->SetContentPreferredSize(pref_size);
}

void BalloonHost::AddNewContents(WebContents* source,
                                 WebContents* new_contents,
                                 WindowOpenDisposition disposition,
                                 const gfx::Rect& initial_pos,
                                 bool user_gesture) {
  Browser* browser = BrowserList::GetLastActiveWithProfile(
      Profile::FromBrowserContext(new_contents->GetBrowserContext()));
  if (!browser)
    return;
  browser->AddWebContents(new_contents, disposition, initial_pos, user_gesture);
}

void BalloonHost::RenderViewCreated(RenderViewHost* render_view_host) {
  render_view_host->DisableScrollbarsForThreshold(
      balloon_->min_scrollbar_size());
  render_view_host->WasResized();
  render_view_host->EnablePreferredSizeMode();

  if (enable_web_ui_)
    render_view_host->AllowBindings(content::BINDINGS_POLICY_WEB_UI);
}

void BalloonHost::RenderViewReady() {
  should_notify_on_disconnect_ = true;
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_NOTIFY_BALLOON_CONNECTED,
      content::Source<BalloonHost>(this),
      content::NotificationService::NoDetails());
}

void BalloonHost::RenderViewGone(base::TerminationStatus status) {
  CloseContents(web_contents_.get());
}

bool BalloonHost::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(BalloonHost, message)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_Request, OnRequest)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void BalloonHost::OnRequest(const ExtensionHostMsg_Request_Params& params) {
  extension_function_dispatcher_.Dispatch(params,
                                          web_contents_->GetRenderViewHost());
}

void BalloonHost::Init() {
  DCHECK(!web_contents_.get()) << "BalloonViewHost already initialized.";
  web_contents_.reset(WebContents::Create(
      balloon_->profile(),
      site_instance_.get(),
      MSG_ROUTING_NONE,
      NULL,
      NULL));
  web_contents_->SetViewType(chrome::VIEW_TYPE_NOTIFICATION);
  web_contents_->SetDelegate(this);
  Observe(web_contents_.get());

  web_contents_->GetController().LoadURL(
      balloon_->notification().content_url(), content::Referrer(),
      content::PAGE_TRANSITION_LINK, std::string());

  initialized_ = true;
}

void BalloonHost::EnableWebUI() {
  DCHECK(!web_contents_.get()) <<
      "EnableWebUI has to be called before a renderer is created.";
  enable_web_ui_ = true;
}

BalloonHost::~BalloonHost() {
}

void BalloonHost::NotifyDisconnect() {
  if (!should_notify_on_disconnect_)
    return;

  should_notify_on_disconnect_ = false;
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_NOTIFY_BALLOON_DISCONNECTED,
      content::Source<BalloonHost>(this),
      content::NotificationService::NoDetails());
}

bool BalloonHost::IsRenderViewReady() const {
  return should_notify_on_disconnect_;
}
