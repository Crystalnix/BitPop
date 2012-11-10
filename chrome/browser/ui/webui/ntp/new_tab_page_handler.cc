// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/ntp/new_tab_page_handler.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/memory/scoped_ptr.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/default_apps_trial.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/ui/webui/ntp/new_tab_ui.h"
#include "chrome/browser/web_resource/notification_promo.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_ui.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

static const char kDefaultPageTypeHistogram[] =
    "NewTabPage.DefaultPageType";

NewTabPageHandler::NewTabPageHandler() : page_switch_count_(0) {
}

NewTabPageHandler::~NewTabPageHandler() {
  HISTOGRAM_COUNTS_100("NewTabPage.SingleSessionPageSwitches",
                       page_switch_count_);
}

void NewTabPageHandler::RegisterMessages() {
  // Record an open of the NTP with its default page type.
  PrefService* prefs = Profile::FromWebUI(web_ui())->GetPrefs();
  int shown_page_type = prefs->GetInteger(prefs::kNtpShownPage) >>
      kPageIdOffset;
  UMA_HISTOGRAM_ENUMERATION(kDefaultPageTypeHistogram,
                            shown_page_type, kHistogramEnumerationMax);

  static bool default_apps_trial_exists =
      base::FieldTrialList::TrialExists(kDefaultAppsTrialName);
  if (default_apps_trial_exists) {
    UMA_HISTOGRAM_ENUMERATION(
        base::FieldTrial::MakeName(kDefaultPageTypeHistogram,
                                   kDefaultAppsTrialName),
        shown_page_type, kHistogramEnumerationMax);
  }

  web_ui()->RegisterMessageCallback("closeNotificationPromo",
      base::Bind(&NewTabPageHandler::HandleCloseNotificationPromo,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("notificationPromoViewed",
      base::Bind(&NewTabPageHandler::HandleNotificationPromoViewed,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("pageSelected",
      base::Bind(&NewTabPageHandler::HandlePageSelected,
                 base::Unretained(this)));
}

void NewTabPageHandler::HandleCloseNotificationPromo(const ListValue* args) {
  NotificationPromo::HandleClosed(Profile::FromWebUI(web_ui()),
                                  NotificationPromo::NTP_NOTIFICATION_PROMO);
  Notify(chrome::NOTIFICATION_PROMO_RESOURCE_STATE_CHANGED);
}

void NewTabPageHandler::HandleNotificationPromoViewed(const ListValue* args) {
  if (NotificationPromo::HandleViewed(Profile::FromWebUI(web_ui()),
        NotificationPromo::NTP_NOTIFICATION_PROMO)) {
    Notify(chrome::NOTIFICATION_PROMO_RESOURCE_STATE_CHANGED);
  }
}

void NewTabPageHandler::HandlePageSelected(const ListValue* args) {
  page_switch_count_++;

  double page_id_double;
  CHECK(args->GetDouble(0, &page_id_double));
  int page_id = static_cast<int>(page_id_double);

  double index_double;
  CHECK(args->GetDouble(1, &index_double));
  int index = static_cast<int>(index_double);

  PrefService* prefs = Profile::FromWebUI(web_ui())->GetPrefs();
  int previous_shown_page =
      prefs->GetInteger(prefs::kNtpShownPage) >> kPageIdOffset;
  UMA_HISTOGRAM_ENUMERATION("NewTabPage.PreviousSelectedPageType",
                            previous_shown_page, kHistogramEnumerationMax);

  prefs->SetInteger(prefs::kNtpShownPage, page_id | index);

  int shown_page_type = page_id >> kPageIdOffset;
  UMA_HISTOGRAM_ENUMERATION("NewTabPage.SelectedPageType",
                            shown_page_type, kHistogramEnumerationMax);

  static bool default_apps_trial_exists =
      base::FieldTrialList::TrialExists(kDefaultAppsTrialName);
  if (default_apps_trial_exists) {
    UMA_HISTOGRAM_ENUMERATION(
        base::FieldTrial::MakeName("NewTabPage.SelectedPageType",
                                   kDefaultAppsTrialName),
        shown_page_type, kHistogramEnumerationMax);
  }
}

// static
void NewTabPageHandler::RegisterUserPrefs(PrefService* prefs) {
  // TODO(estade): should be syncable.
  prefs->RegisterIntegerPref(prefs::kNtpShownPage, APPS_PAGE_ID,
                             PrefService::UNSYNCABLE_PREF);
}

// static
void NewTabPageHandler::GetLocalizedValues(Profile* profile,
                                           DictionaryValue* values) {
  values->SetInteger("most_visited_page_id", MOST_VISITED_PAGE_ID);
  values->SetInteger("apps_page_id", APPS_PAGE_ID);
  values->SetInteger("suggestions_page_id", SUGGESTIONS_PAGE_ID);

  PrefService* prefs = profile->GetPrefs();
  int shown_page = prefs->GetInteger(prefs::kNtpShownPage);
  values->SetInteger("shown_page_type", shown_page & ~INDEX_MASK);
  values->SetInteger("shown_page_index", shown_page & INDEX_MASK);
}

void NewTabPageHandler::Notify(chrome::NotificationType notification_type) {
  content::NotificationService* service =
      content::NotificationService::current();
  service->Notify(notification_type,
                  content::Source<NewTabPageHandler>(this),
                  content::NotificationService::NoDetails());
}
