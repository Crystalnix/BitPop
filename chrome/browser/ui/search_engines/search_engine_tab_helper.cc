// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search_engines/search_engine_tab_helper.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_fetcher.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/search_engines/template_url_fetcher_ui_callbacks.h"
#include "chrome/common/render_messages.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/frame_navigate_params.h"

using content::NavigationController;
using content::NavigationEntry;
using content::WebContents;

namespace {

// Returns true if the entry's transition type is FORM_SUBMIT.
bool IsFormSubmit(const NavigationEntry* entry) {
  return (content::PageTransitionStripQualifier(entry->GetTransitionType()) ==
          content::PAGE_TRANSITION_FORM_SUBMIT);
}

}  // namespace

SearchEngineTabHelper::SearchEngineTabHelper(WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {
  DCHECK(web_contents);
}

SearchEngineTabHelper::~SearchEngineTabHelper() {
}

void SearchEngineTabHelper::DidNavigateMainFrame(
    const content::LoadCommittedDetails& /*details*/,
    const content::FrameNavigateParams& params) {
  GenerateKeywordIfNecessary(params);
}

bool SearchEngineTabHelper::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(SearchEngineTabHelper, message)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_PageHasOSDD, OnPageHasOSDD)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  return handled;
}

void SearchEngineTabHelper::OnPageHasOSDD(
    int32 page_id,
    const GURL& doc_url,
    const search_provider::OSDDType& msg_provider_type) {
  // Checks to see if we should generate a keyword based on the OSDD, and if
  // necessary uses TemplateURLFetcher to download the OSDD and create a
  // keyword.

  // Make sure page_id is the current page and other basic checks.
  DCHECK(doc_url.is_valid());
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (!web_contents()->IsActiveEntry(page_id))
    return;
  if (!profile->GetTemplateURLFetcher())
    return;
  if (profile->IsOffTheRecord())
    return;

  TemplateURLFetcher::ProviderType provider_type;
  switch (msg_provider_type) {
    case search_provider::AUTODETECTED_PROVIDER:
      provider_type = TemplateURLFetcher::AUTODETECTED_PROVIDER;
      break;

    case search_provider::EXPLICIT_DEFAULT_PROVIDER:
      provider_type = TemplateURLFetcher::EXPLICIT_DEFAULT_PROVIDER;
      break;

    case search_provider::EXPLICIT_PROVIDER:
      provider_type = TemplateURLFetcher::EXPLICIT_PROVIDER;
      break;

    default:
      NOTREACHED();
      return;
  }

  const NavigationController& controller = web_contents()->GetController();
  const NavigationEntry* entry = controller.GetLastCommittedEntry();
  DCHECK(entry);

  const NavigationEntry* base_entry = entry;
  if (IsFormSubmit(base_entry)) {
    // If the current page is a form submit, find the last page that was not
    // a form submit and use its url to generate the keyword from.
    int index = controller.GetLastCommittedEntryIndex() - 1;
    while (index >= 0 && IsFormSubmit(controller.GetEntryAtIndex(index)))
      index--;
    if (index >= 0)
      base_entry = controller.GetEntryAtIndex(index);
    else
      base_entry = NULL;
  }

  // We want to use the user typed URL if available since that represents what
  // the user typed to get here, and fall back on the regular URL if not.
  if (!base_entry)
    return;
  GURL keyword_url = base_entry->GetUserTypedURL().is_valid() ?
          base_entry->GetUserTypedURL() : base_entry->GetURL();
  if (!keyword_url.is_valid())
    return;

  string16 keyword = TemplateURLService::GenerateKeyword(
      keyword_url,
      provider_type == TemplateURLFetcher::AUTODETECTED_PROVIDER);

  // Download the OpenSearch description document. If this is successful, a
  // new keyword will be created when done.
  profile->GetTemplateURLFetcher()->ScheduleDownload(
      keyword,
      doc_url,
      base_entry->GetFavicon().url,
      new TemplateURLFetcherUICallbacks(this, web_contents()),
      provider_type);
}

void SearchEngineTabHelper::GenerateKeywordIfNecessary(
    const content::FrameNavigateParams& params) {
  if (!params.searchable_form_url.is_valid())
    return;

  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (profile->IsOffTheRecord())
    return;

  const NavigationController& controller = web_contents()->GetController();
  int last_index = controller.GetLastCommittedEntryIndex();
  // When there was no previous page, the last index will be 0. This is
  // normally due to a form submit that opened in a new tab.
  // TODO(brettw) bug 916126: we should support keywords when form submits
  //              happen in new tabs.
  if (last_index <= 0)
    return;
  const NavigationEntry* previous_entry =
      controller.GetEntryAtIndex(last_index - 1);
  if (IsFormSubmit(previous_entry)) {
    // Only generate a keyword if the previous page wasn't itself a form
    // submit.
    return;
  }

  GURL keyword_url = previous_entry->GetUserTypedURL().is_valid() ?
          previous_entry->GetUserTypedURL() : previous_entry->GetURL();
  string16 keyword =
      TemplateURLService::GenerateKeyword(keyword_url, true);  // autodetected
  if (keyword.empty())
    return;

  TemplateURLService* url_service =
      TemplateURLServiceFactory::GetForProfile(profile);
  if (!url_service)
    return;

  if (!url_service->loaded()) {
    url_service->Load();
    return;
  }

  const TemplateURL* current_url;
  GURL url = params.searchable_form_url;
  if (!url_service->CanReplaceKeyword(keyword, url, &current_url))
    return;

  if (current_url) {
    if (current_url->originating_url().is_valid()) {
      // The existing keyword was generated from an OpenSearch description
      // document, don't regenerate.
      return;
    }
    url_service->Remove(current_url);
  }
  TemplateURL* new_url = new TemplateURL();
  new_url->set_keyword(keyword);
  new_url->set_short_name(keyword);
  new_url->SetURL(url.spec(), 0, 0);
  new_url->add_input_encoding(params.searchable_form_encoding);
  DCHECK(controller.GetLastCommittedEntry());
  const GURL& favicon_url =
      controller.GetLastCommittedEntry()->GetFavicon().url;
  if (favicon_url.is_valid()) {
    new_url->SetFaviconURL(favicon_url);
  } else {
    // The favicon url isn't valid. This means there really isn't a favicon,
    // or the favicon url wasn't obtained before the load started. This assumes
    // the later.
    // TODO(sky): Need a way to set the favicon that doesn't involve generating
    // its url.
    new_url->SetFaviconURL(
        TemplateURL::GenerateFaviconURL(params.referrer.url));
  }
  new_url->set_safe_for_autoreplace(true);
  url_service->Add(new_url);
}
