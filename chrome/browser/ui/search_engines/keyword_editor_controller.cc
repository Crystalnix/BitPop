// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search_engines/keyword_editor_controller.h"

#include "base/utf_string_conversions.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_model.h"
#include "chrome/browser/ui/search_engines/template_url_table_model.h"
#include "chrome/common/pref_names.h"
#include "content/browser/user_metrics.h"

KeywordEditorController::KeywordEditorController(Profile* profile)
    : profile_(profile) {
  table_model_.reset(new TemplateURLTableModel(profile->GetTemplateURLModel()));
}

KeywordEditorController::~KeywordEditorController() {
}

// static
// TODO(rsesek): Other platforms besides Mac should remember window
// placement. http://crbug.com/22269
void KeywordEditorController::RegisterPrefs(PrefService* prefs) {
  prefs->RegisterDictionaryPref(prefs::kKeywordEditorWindowPlacement);
}

int KeywordEditorController::AddTemplateURL(const string16& title,
                                            const string16& keyword,
                                            const std::string& url) {
  DCHECK(!url.empty());

  UserMetrics::RecordAction(UserMetricsAction("KeywordEditor_AddKeyword"));

  TemplateURL* template_url = new TemplateURL();
  template_url->set_short_name(title);
  template_url->set_keyword(keyword);
  template_url->SetURL(url, 0, 0);

  // There's a bug (1090726) in TableView with groups enabled such that newly
  // added items in groups ALWAYS appear at the end, regardless of the index
  // passed in. Worse yet, the selected rows get messed up when this happens
  // causing other problems. As a work around we always add the item to the end
  // of the list.
  const int new_index = table_model_->RowCount();
  table_model_->Add(new_index, template_url);

  return new_index;
}

void KeywordEditorController::ModifyTemplateURL(const TemplateURL* template_url,
                                                const string16& title,
                                                const string16& keyword,
                                                const std::string& url) {
  const int index = table_model_->IndexOfTemplateURL(template_url);
  if (index == -1) {
    // Will happen if url was deleted out from under us while the user was
    // editing it.
    return;
  }

  // Don't do anything if the entry didn't change.
  if (template_url->short_name() == title &&
      template_url->keyword() == keyword &&
      ((url.empty() && !template_url->url()) ||
       (!url.empty() && template_url->url() &&
        template_url->url()->url() == url))) {
    return;
  }

  table_model_->ModifyTemplateURL(index, title, keyword, url);

  UserMetrics::RecordAction(UserMetricsAction("KeywordEditor_ModifiedKeyword"));
}

bool KeywordEditorController::CanEdit(const TemplateURL* url) const {
  return !url_model()->is_default_search_managed() ||
      url != url_model()->GetDefaultSearchProvider();
}

bool KeywordEditorController::CanMakeDefault(const TemplateURL* url) const {
  return url_model()->CanMakeDefault(url);
}

bool KeywordEditorController::CanRemove(const TemplateURL* url) const {
  return url != url_model()->GetDefaultSearchProvider();
}

void KeywordEditorController::RemoveTemplateURL(int index) {
  table_model_->Remove(index);
  UserMetrics::RecordAction(UserMetricsAction("KeywordEditor_RemoveKeyword"));
}

int KeywordEditorController::MakeDefaultTemplateURL(int index) {
  return table_model_->MakeDefaultTemplateURL(index);
}

bool KeywordEditorController::loaded() const {
  return url_model()->loaded();
}

const TemplateURL* KeywordEditorController::GetTemplateURL(int index) const {
  return &table_model_->GetTemplateURL(index);
}

TemplateURLModel* KeywordEditorController::url_model() const {
  return table_model_->template_url_model();
}
