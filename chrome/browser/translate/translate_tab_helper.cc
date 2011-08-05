// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/translate/translate_tab_helper.h"

#include "chrome/browser/translate/page_translated_details.h"
#include "chrome/common/render_messages.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/common/notification_service.h"

TranslateTabHelper::TranslateTabHelper(TabContents* tab_contents)
    : TabContentsObserver(tab_contents),
      language_state_(&tab_contents->controller()) {
}

TranslateTabHelper::~TranslateTabHelper() {
}

bool TranslateTabHelper::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(TranslateTabHelper, message)
    IPC_MESSAGE_HANDLER(ViewHostMsg_TranslateLanguageDetermined,
                        OnLanguageDetermined)
    IPC_MESSAGE_HANDLER(ViewHostMsg_PageTranslated, OnPageTranslated)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  return handled;
}

void TranslateTabHelper::DidNavigateAnyFramePostCommit(
    const content::LoadCommittedDetails& details,
    const ViewHostMsg_FrameNavigate_Params& params) {
  // Let the LanguageState clear its state.
  language_state_.DidNavigate(details);
}

void TranslateTabHelper::OnLanguageDetermined(const std::string& language,
                                              bool page_translatable) {
  language_state_.LanguageDetermined(language, page_translatable);

  std::string lang = language;
  NotificationService::current()->Notify(
      NotificationType::TAB_LANGUAGE_DETERMINED,
      Source<TabContents>(tab_contents()),
      Details<std::string>(&lang));
}

void TranslateTabHelper::OnPageTranslated(int32 page_id,
                                          const std::string& original_lang,
                                          const std::string& translated_lang,
                                          TranslateErrors::Type error_type) {
  language_state_.set_current_language(translated_lang);
  language_state_.set_translation_pending(false);
  PageTranslatedDetails details(original_lang, translated_lang, error_type);
  NotificationService::current()->Notify(
      NotificationType::PAGE_TRANSLATED,
      Source<TabContents>(tab_contents()),
      Details<PageTranslatedDetails>(&details));
}
