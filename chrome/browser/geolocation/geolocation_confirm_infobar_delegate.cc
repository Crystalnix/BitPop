// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/geolocation/geolocation_confirm_infobar_delegate.h"

#include "chrome/browser/geolocation/geolocation_infobar_queue_controller.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "net/base/net_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"


GeolocationConfirmInfoBarDelegate::GeolocationConfirmInfoBarDelegate(
    InfoBarTabHelper* infobar_helper,
    GeolocationInfoBarQueueController* controller,
    const GeolocationPermissionRequestID& id,
    const GURL& requesting_frame,
    const std::string& display_languages)
    : ConfirmInfoBarDelegate(infobar_helper),
      controller_(controller),
      id_(id),
      requesting_frame_(requesting_frame),
      display_languages_(display_languages) {
  const content::NavigationEntry* committed_entry =
      infobar_helper->GetWebContents()->GetController().GetLastCommittedEntry();
  set_contents_unique_id(committed_entry ? committed_entry->GetUniqueID() : 0);
}

gfx::Image* GeolocationConfirmInfoBarDelegate::GetIcon() const {
  return &ResourceBundle::GetSharedInstance().GetNativeImageNamed(
      IDR_GEOLOCATION_INFOBAR_ICON);
}

InfoBarDelegate::Type
    GeolocationConfirmInfoBarDelegate::GetInfoBarType() const {
  return PAGE_ACTION_TYPE;
}

string16 GeolocationConfirmInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringFUTF16(IDS_GEOLOCATION_INFOBAR_QUESTION,
      net::FormatUrl(requesting_frame_.GetOrigin(), display_languages_));
}

string16 GeolocationConfirmInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  return l10n_util::GetStringUTF16((button == BUTTON_OK) ?
      IDS_GEOLOCATION_ALLOW_BUTTON : IDS_GEOLOCATION_DENY_BUTTON);
}

void GeolocationConfirmInfoBarDelegate::SetPermission(
    bool update_content_setting, bool allowed) {
  controller_->OnPermissionSet(id_, requesting_frame_,
                               owner()->GetWebContents()->GetURL(),
                               update_content_setting, allowed);
}

bool GeolocationConfirmInfoBarDelegate::Accept() {
  SetPermission(true, true);
  return true;
}

bool GeolocationConfirmInfoBarDelegate::Cancel() {
  SetPermission(true, false);
  return true;
}

string16 GeolocationConfirmInfoBarDelegate::GetLinkText() const {
  return l10n_util::GetStringUTF16(IDS_LEARN_MORE);
}

bool GeolocationConfirmInfoBarDelegate::LinkClicked(
    WindowOpenDisposition disposition) {
  const char kGeolocationLearnMoreUrl[] =
#if defined(OS_CHROMEOS)
      "https://www.google.com/support/chromeos/bin/answer.py?answer=142065";
#else
      "https://www.google.com/support/chrome/bin/answer.py?answer=142065";
#endif

  content::OpenURLParams params(
      google_util::AppendGoogleLocaleParam(GURL(kGeolocationLearnMoreUrl)),
      content::Referrer(),
      (disposition == CURRENT_TAB) ? NEW_FOREGROUND_TAB : disposition,
      content::PAGE_TRANSITION_LINK, false);
  owner()->GetWebContents()->OpenURL(params);
  return false;  // Do not dismiss the info bar.
}
