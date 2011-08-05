// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search_engines/edit_search_engine_controller.h"

#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/net/url_fixer_upper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_model.h"
#include "content/browser/user_metrics.h"
#include "googleurl/src/gurl.h"

EditSearchEngineController::EditSearchEngineController(
    const TemplateURL* template_url,
    EditSearchEngineControllerDelegate* edit_keyword_delegate,
    Profile* profile)
    : template_url_(template_url),
      edit_keyword_delegate_(edit_keyword_delegate),
      profile_(profile) {
  DCHECK(profile_);
}

bool EditSearchEngineController::IsTitleValid(
    const string16& title_input) const {
  return !CollapseWhitespace(title_input, true).empty();
}

bool EditSearchEngineController::IsURLValid(
    const std::string& url_input) const {
  std::string url = GetFixedUpURL(url_input);
  if (url.empty())
    return false;

  // Use TemplateURLRef to extract the search placeholder.
  TemplateURLRef template_ref(url, 0, 0);
  if (!template_ref.IsValid())
    return false;

  if (!template_ref.SupportsReplacement()) {
    // If this is the default search engine, there must be a search term
    // placeholder.
    if (template_url_ ==
        profile_->GetTemplateURLModel()->GetDefaultSearchProvider())
      return false;
    return GURL(url).is_valid();
  }

  // If the url has a search term, replace it with a random string and make
  // sure the resulting URL is valid. We don't check the validity of the url
  // with the search term as that is not necessarily valid.
  return GURL(template_ref.ReplaceSearchTerms(TemplateURL(), ASCIIToUTF16("a"),
      TemplateURLRef::NO_SUGGESTIONS_AVAILABLE, string16())).is_valid();
}

bool EditSearchEngineController::IsKeywordValid(
    const string16& keyword_input) const {
  string16 keyword_input_trimmed(CollapseWhitespace(keyword_input, true));
  if (keyword_input_trimmed.empty())
    return false;  // Do not allow empty keyword.
  const TemplateURL* turl_with_keyword =
      profile_->GetTemplateURLModel()->GetTemplateURLForKeyword(
          keyword_input_trimmed);
  return (turl_with_keyword == NULL || turl_with_keyword == template_url_);
}

void EditSearchEngineController::AcceptAddOrEdit(
    const string16& title_input,
    const string16& keyword_input,
    const std::string& url_input) {
  std::string url_string = GetFixedUpURL(url_input);
  DCHECK(!url_string.empty());

  const TemplateURL* existing =
      profile_->GetTemplateURLModel()->GetTemplateURLForKeyword(
          keyword_input);
  if (existing &&
      (!edit_keyword_delegate_ || existing != template_url_)) {
    // An entry may have been added with the same keyword string while the
    // user edited the dialog, either automatically or by the user (if we're
    // confirming a JS addition, they could have the Options dialog open at the
    // same time). If so, just ignore this add.
    // TODO(pamg): Really, we should modify the entry so this later one
    // overwrites it. But we don't expect this case to be common.
    CleanUpCancelledAdd();
    return;
  }

  if (!edit_keyword_delegate_) {
    // Confiming an entry we got from JS. We have a template_url_, but it
    // hasn't yet been added to the model.
    DCHECK(template_url_);
    // const_cast is ugly, but this is the same thing the TemplateURLModel
    // does in a similar situation (updating an existing TemplateURL with
    // data from a new one).
    TemplateURL* modifiable_url = const_cast<TemplateURL*>(template_url_);
    modifiable_url->set_short_name(title_input);
    modifiable_url->set_keyword(keyword_input);
    modifiable_url->SetURL(url_string, 0, 0);
    // TemplateURLModel takes ownership of template_url_.
    profile_->GetTemplateURLModel()->Add(modifiable_url);
    UserMetrics::RecordAction(UserMetricsAction("KeywordEditor_AddKeywordJS"));
  } else {
    // Adding or modifying an entry via the Delegate.
    edit_keyword_delegate_->OnEditedKeyword(template_url_,
                                            title_input,
                                            keyword_input,
                                            url_string);
  }
}

void EditSearchEngineController::CleanUpCancelledAdd() {
  if (!edit_keyword_delegate_ && template_url_) {
    // When we have no Delegate, we know that the template_url_ hasn't yet been
    // added to the model, so we need to clean it up.
    delete template_url_;
    template_url_ = NULL;
  }
}

std::string EditSearchEngineController::GetFixedUpURL(
    const std::string& url_input) const {
  std::string url;
  TrimWhitespace(TemplateURLRef::DisplayURLToURLRef(UTF8ToUTF16(url_input)),
                 TRIM_ALL, &url);
  if (url.empty())
    return url;

  // Parse the string as a URL to determine the scheme. If we need to, add the
  // scheme. As the scheme may be expanded (as happens with {google:baseURL})
  // we need to replace the search terms before testing for the scheme.
  TemplateURL t_url;
  t_url.SetURL(url, 0, 0);
  std::string expanded_url =
      t_url.url()->ReplaceSearchTerms(t_url, ASCIIToUTF16("x"), 0, string16());
  url_parse::Parsed parts;
  std::string scheme(
      URLFixerUpper::SegmentURL(expanded_url, &parts));
  if (!parts.scheme.is_valid()) {
    scheme.append("://");
    url.insert(0, scheme);
  }

  return url;
}

