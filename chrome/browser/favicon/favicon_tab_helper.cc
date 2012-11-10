// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/favicon/favicon_tab_helper.h"

#include "chrome/browser/favicon/favicon_handler.h"
#include "chrome/browser/favicon/favicon_util.h"
#include "chrome/browser/favicon/select_favicon_frames.h"
#include "chrome/browser/history/history.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/icon_messages.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_ui.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"

using content::FaviconStatus;
using content::NavigationController;
using content::NavigationEntry;
using content::WebContents;

FaviconTabHelper::FaviconTabHelper(WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      profile_(Profile::FromBrowserContext(web_contents->GetBrowserContext())) {
  favicon_handler_.reset(new FaviconHandler(profile_, this,
                                            FaviconHandler::FAVICON));
  if (chrome::kEnableTouchIcon)
    touch_icon_handler_.reset(new FaviconHandler(profile_, this,
                                                 FaviconHandler::TOUCH));
}

FaviconTabHelper::~FaviconTabHelper() {
}

void FaviconTabHelper::FetchFavicon(const GURL& url) {
  favicon_handler_->FetchFavicon(url);
  if (touch_icon_handler_.get())
    touch_icon_handler_->FetchFavicon(url);
}

gfx::Image FaviconTabHelper::GetFavicon() const {
  // Like GetTitle(), we also want to use the favicon for the last committed
  // entry rather than a pending navigation entry.
  const NavigationController& controller = web_contents()->GetController();
  NavigationEntry* entry = controller.GetTransientEntry();
  if (entry)
    return entry->GetFavicon().image;

  entry = controller.GetLastCommittedEntry();
  if (entry)
    return entry->GetFavicon().image;
  return gfx::Image();
}

bool FaviconTabHelper::FaviconIsValid() const {
  const NavigationController& controller = web_contents()->GetController();
  NavigationEntry* entry = controller.GetTransientEntry();
  if (entry)
    return entry->GetFavicon().valid;

  entry = controller.GetLastCommittedEntry();
  if (entry)
    return entry->GetFavicon().valid;

  return false;
}

bool FaviconTabHelper::ShouldDisplayFavicon() {
  // Always display a throbber during pending loads.
  const NavigationController& controller = web_contents()->GetController();
  if (controller.GetLastCommittedEntry() && controller.GetPendingEntry())
    return true;

  content::WebUI* web_ui = web_contents()->GetWebUIForCurrentState();
  if (web_ui)
    return !web_ui->ShouldHideFavicon();
  return true;
}

void FaviconTabHelper::SaveFavicon() {
  NavigationEntry* entry = web_contents()->GetController().GetActiveEntry();
  if (!entry || entry->GetURL().is_empty())
    return;

  // Make sure the page is in history, otherwise adding the favicon does
  // nothing.
  HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile_->GetOriginalProfile(), Profile::IMPLICIT_ACCESS);
  if (!history)
    return;
  history->AddPageNoVisitForBookmark(entry->GetURL(), entry->GetTitle());

  FaviconService* service = profile_->
      GetOriginalProfile()->GetFaviconService(Profile::IMPLICIT_ACCESS);
  if (!service)
    return;
  const FaviconStatus& favicon(entry->GetFavicon());
  if (!favicon.valid || favicon.url.is_empty() ||
      favicon.image.IsEmpty()) {
    return;
  }
  std::vector<unsigned char> image_data;
  // TODO: Save all representations.
  gfx::PNGCodec::EncodeBGRASkBitmap(
      favicon.image.AsBitmap(), false, &image_data);
  service->SetFavicon(
      entry->GetURL(), favicon.url, image_data, history::FAVICON);
}

int FaviconTabHelper::DownloadImage(const GURL& image_url,
                                 int image_size,
                                 history::IconType icon_type,
                                 const ImageDownloadCallback& callback) {
  if (icon_type == history::FAVICON)
    return favicon_handler_->DownloadImage(image_url, image_size, icon_type,
                                           callback);
  else if (touch_icon_handler_.get())
    return touch_icon_handler_->DownloadImage(image_url, image_size, icon_type,
                                              callback);
  return 0;
}

void FaviconTabHelper::OnUpdateFaviconURL(
    int32 page_id,
    const std::vector<FaviconURL>& candidates) {
  favicon_handler_->OnUpdateFaviconURL(page_id, candidates);
  if (touch_icon_handler_.get())
    touch_icon_handler_->OnUpdateFaviconURL(page_id, candidates);
}

NavigationEntry* FaviconTabHelper::GetActiveEntry() {
  return web_contents()->GetController().GetActiveEntry();
}

int FaviconTabHelper::StartDownload(const GURL& url, int image_size) {
  content::RenderViewHost* host = web_contents()->GetRenderViewHost();
  int id = FaviconUtil::DownloadFavicon(host, url, image_size);
  return id;
}

void FaviconTabHelper::NotifyFaviconUpdated() {
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_FAVICON_UPDATED,
      content::Source<WebContents>(web_contents()),
      content::NotificationService::NoDetails());
  web_contents()->NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TAB);
}

void FaviconTabHelper::NavigateToPendingEntry(
    const GURL& url,
    NavigationController::ReloadType reload_type) {
  if (reload_type != NavigationController::NO_RELOAD &&
      !profile_->IsOffTheRecord()) {
    FaviconService* favicon_service =
        profile_->GetFaviconService(Profile::IMPLICIT_ACCESS);
    if (favicon_service)
      favicon_service->SetFaviconOutOfDateForPage(url);
  }
}

void FaviconTabHelper::DidNavigateMainFrame(
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& params) {
  // Get the favicon, either from history or request it from the net.
  FetchFavicon(details.entry->GetURL());
}

bool FaviconTabHelper::OnMessageReceived(const IPC::Message& message) {
  bool message_handled = false;   // Allow other handlers to receive these.
  IPC_BEGIN_MESSAGE_MAP(FaviconTabHelper, message)
    IPC_MESSAGE_HANDLER(IconHostMsg_DidDownloadFavicon, OnDidDownloadFavicon)
    IPC_MESSAGE_HANDLER(IconHostMsg_UpdateFaviconURL, OnUpdateFaviconURL)
    IPC_MESSAGE_UNHANDLED(message_handled = false)
  IPC_END_MESSAGE_MAP()
  return message_handled;
}

void FaviconTabHelper::OnDidDownloadFavicon(
    int id,
    const GURL& image_url,
    bool errored,
    int requested_size,
    const std::vector<SkBitmap>& bitmaps) {
  float score = 0;
  // TODO: Possibly do bitmap selection in FaviconHandler, so that it can score
  // favicons better.
  std::vector<ui::ScaleFactor> scale_factors;
#if defined(OS_MACOSX)
  scale_factors = ui::GetSupportedScaleFactors();
#else
  scale_factors.push_back(ui::SCALE_FACTOR_100P);  // TODO: Aura?
#endif
  gfx::Image favicon(SelectFaviconFrames(
      bitmaps, scale_factors, requested_size, &score));
  favicon_handler_->OnDidDownloadFavicon(
      id, image_url, errored, favicon, score);
  if (touch_icon_handler_.get()) {
    touch_icon_handler_->OnDidDownloadFavicon(
        id, image_url, errored, favicon, score);
  }
}
