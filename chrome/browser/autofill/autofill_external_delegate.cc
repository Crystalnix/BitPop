// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/utf_string_conversions.h"
#include "chrome/browser/autofill/autocomplete_history_manager.h"
#include "chrome/browser/autofill/autofill_external_delegate.h"
#include "chrome/browser/autofill/autofill_manager.h"
#include "chrome/browser/ui/autofill/autofill_popup_controller_impl.h"
#include "chrome/common/autofill_messages.h"
#include "chrome/common/chrome_constants.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebAutofillClient.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(OS_ANDROID)
#include "content/public/browser/android/content_view_core.h"
#endif

using content::RenderViewHost;
using WebKit::WebAutofillClient;

DEFINE_WEB_CONTENTS_USER_DATA_KEY(AutofillExternalDelegate)

void AutofillExternalDelegate::CreateForWebContentsAndManager(
    content::WebContents* web_contents,
    AutofillManager* autofill_manager) {
  if (FromWebContents(web_contents))
    return;

  web_contents->SetUserData(
      UserDataKey(),
      new AutofillExternalDelegate(web_contents, autofill_manager));
}

AutofillExternalDelegate::AutofillExternalDelegate(
    content::WebContents* web_contents,
    AutofillManager* autofill_manager)
    : web_contents_(web_contents),
      autofill_manager_(autofill_manager),
      controller_(NULL),
      password_autofill_manager_(web_contents),
      autofill_query_id_(0),
      display_warning_if_disabled_(false),
      has_shown_autofill_popup_for_current_edit_(false) {
  registrar_.Add(this,
                 content::NOTIFICATION_WEB_CONTENTS_VISIBILITY_CHANGED,
                 content::Source<content::WebContents>(web_contents));
  if (web_contents) {
    registrar_.Add(
        this,
        content::NOTIFICATION_NAV_ENTRY_COMMITTED,
        content::Source<content::NavigationController>(
            &(web_contents->GetController())));
  }
}

AutofillExternalDelegate::~AutofillExternalDelegate() {
  if (controller_)
    controller_->DelegateDestroyed();
}

void AutofillExternalDelegate::SelectAutofillSuggestionAtIndex(int unique_id) {
  ClearPreviewedForm();

  // Only preview the data if it is a profile.
  if (unique_id > 0)
    FillAutofillFormData(unique_id, true);
}

void AutofillExternalDelegate::OnQuery(int query_id,
                                       const FormData& form,
                                       const FormFieldData& field,
                                       const gfx::Rect& element_bounds,
                                       bool display_warning_if_disabled) {
  autofill_query_form_ = form;
  autofill_query_field_ = field;
  display_warning_if_disabled_ = display_warning_if_disabled;
  autofill_query_id_ = query_id;

  EnsurePopupForElement(element_bounds);
}

void AutofillExternalDelegate::OnSuggestionsReturned(
    int query_id,
    const std::vector<string16>& autofill_values,
    const std::vector<string16>& autofill_labels,
    const std::vector<string16>& autofill_icons,
    const std::vector<int>& autofill_unique_ids) {
  if (query_id != autofill_query_id_ || !controller_)
    return;

  std::vector<string16> values(autofill_values);
  std::vector<string16> labels(autofill_labels);
  std::vector<string16> icons(autofill_icons);
  std::vector<int> ids(autofill_unique_ids);

  // Add a separator to go between the values and menu items.
  values.push_back(string16());
  labels.push_back(string16());
  icons.push_back(string16());
  ids.push_back(WebAutofillClient::MenuItemIDSeparator);

  ApplyAutofillWarnings(&values, &labels, &icons, &ids);

  // Only include "Autofill Options" special menu item if we have Autofill
  // items, identified by |unique_ids| having at least one valid value.
  bool has_autofill_item = false;
  for (size_t i = 0; i < ids.size(); ++i) {
    if (ids[i] > 0) {
      has_autofill_item = true;
      break;
    }
  }

  if (has_autofill_item)
    ApplyAutofillOptions(&values, &labels, &icons, &ids);

  // Remove the separator if it is the last element.
  if (ids.back() == WebAutofillClient::MenuItemIDSeparator) {
    values.pop_back();
    labels.pop_back();
    icons.pop_back();
    ids.pop_back();
  }

  InsertDataListValues(&values, &labels, &icons, &ids);

  if (values.empty()) {
    // No suggestions, any popup currently showing is obsolete.
    HideAutofillPopup();
    return;
  }

  // Send to display.
  if (autofill_query_field_.is_focusable) {
    ApplyAutofillSuggestions(values, labels, icons, ids);

    if (autofill_manager_) {
      autofill_manager_->OnDidShowAutofillSuggestions(
          has_autofill_item && !has_shown_autofill_popup_for_current_edit_);
    }
    has_shown_autofill_popup_for_current_edit_ |= has_autofill_item;
  }
}

void AutofillExternalDelegate::OnShowPasswordSuggestions(
    const std::vector<string16>& suggestions,
    const FormFieldData& field,
    const gfx::Rect& element_bounds) {
  autofill_query_field_ = field;
  EnsurePopupForElement(element_bounds);

  if (suggestions.empty()) {
    HideAutofillPopup();
    return;
  }

  std::vector<string16> empty(suggestions.size());
  std::vector<int> password_ids(suggestions.size(),
                                WebAutofillClient::MenuItemIDPasswordEntry);
  ApplyAutofillSuggestions(suggestions, empty, empty, password_ids);
}

void AutofillExternalDelegate::EnsurePopupForElement(
    const gfx::Rect& element_bounds) {
  if (controller_)
    return;

  // |controller_| owns itself.
  controller_ = new AutofillPopupControllerImpl(
      this,
      // web_contents() may be NULL during testing.
      web_contents() ? web_contents()->GetView()->GetContentNativeView() : NULL,
      element_bounds);
}

void AutofillExternalDelegate::ApplyAutofillSuggestions(
    const std::vector<string16>& autofill_values,
    const std::vector<string16>& autofill_labels,
    const std::vector<string16>& autofill_icons,
    const std::vector<int>& autofill_unique_ids) {
  controller_->Show(autofill_values,
                    autofill_labels,
                    autofill_icons,
                    autofill_unique_ids);

  web_contents()->GetRenderViewHost()->AddKeyboardListener(controller_);
}

void AutofillExternalDelegate::SetCurrentDataListValues(
    const std::vector<string16>& data_list_values,
    const std::vector<string16>& data_list_labels,
    const std::vector<string16>& data_list_icons,
    const std::vector<int>& data_list_unique_ids) {
  data_list_values_ = data_list_values;
  data_list_labels_ = data_list_labels;
  data_list_icons_ = data_list_icons;
  data_list_unique_ids_ = data_list_unique_ids;
}

void AutofillExternalDelegate::RemoveAutocompleteEntry(const string16& value) {
  if (web_contents_) {
    autofill_manager_->RemoveAutocompleteEntry(
        autofill_query_field_.name, value);
  }
}

void AutofillExternalDelegate::RemoveAutofillProfileOrCreditCard(
    int unique_id) {
  autofill_manager_->RemoveAutofillProfileOrCreditCard(unique_id);
}

void AutofillExternalDelegate::DidEndTextFieldEditing() {
  HideAutofillPopup();

  has_shown_autofill_popup_for_current_edit_ = false;
}

bool AutofillExternalDelegate::DidAcceptAutofillSuggestion(
    const string16& value,
    int unique_id,
    unsigned index) {
  // If the selected element is a warning we don't want to do anything.
  if (unique_id == WebAutofillClient::MenuItemIDWarningMessage)
    return false;

  RenderViewHost* host = web_contents_->GetRenderViewHost();

  if (unique_id == WebAutofillClient::MenuItemIDAutofillOptions) {
    // User selected 'Autofill Options'.
    autofill_manager_->OnShowAutofillDialog();
  } else if (unique_id == WebAutofillClient::MenuItemIDClearForm) {
    // User selected 'Clear form'.
    host->Send(new AutofillMsg_ClearForm(host->GetRoutingID()));
  } else if (unique_id == WebAutofillClient::MenuItemIDPasswordEntry &&
             password_autofill_manager_.DidAcceptAutofillSuggestion(
                 autofill_query_field_, value)) {
    // DidAcceptAutofillSuggestion has already handled the work to fill in
    // the page as required.
  } else if (unique_id == WebAutofillClient::MenuItemIDDataListEntry) {
    host->Send(new AutofillMsg_AcceptDataListSuggestion(host->GetRoutingID(),
                                                        value));
  } else if (unique_id == WebAutofillClient::MenuItemIDAutocompleteEntry) {
    // User selected an Autocomplete, so we fill directly.
    host->Send(new AutofillMsg_SetNodeText(
        host->GetRoutingID(),
        value));
  } else {
    FillAutofillFormData(unique_id, false);
  }

  HideAutofillPopup();

  return true;
}

void AutofillExternalDelegate::ClearPreviewedForm() {
  if (web_contents_) {
    RenderViewHost* host = web_contents_->GetRenderViewHost();

    if (host)
      host->Send(new AutofillMsg_ClearPreviewedForm(host->GetRoutingID()));
  }
}

void AutofillExternalDelegate::ControllerDestroyed() {
  web_contents()->GetRenderViewHost()->RemoveKeyboardListener(controller_);
  controller_ = NULL;
}

void AutofillExternalDelegate::HideAutofillPopup() {
  if (controller_) {
    ClearPreviewedForm();
    controller_->Hide();
  }
}

void AutofillExternalDelegate::Reset() {
  HideAutofillPopup();

  password_autofill_manager_.Reset();
}

void AutofillExternalDelegate::AddPasswordFormMapping(
      const FormFieldData& form,
      const PasswordFormFillData& fill_data) {
  password_autofill_manager_.AddPasswordFormMapping(form, fill_data);
}

void AutofillExternalDelegate::FillAutofillFormData(int unique_id,
                                                    bool is_preview) {
  RenderViewHost* host = web_contents_->GetRenderViewHost();

  if (is_preview) {
    host->Send(new AutofillMsg_SetAutofillActionPreview(
        host->GetRoutingID()));
  } else {
    host->Send(new AutofillMsg_SetAutofillActionFill(
        host->GetRoutingID()));
  }

  // Fill the values for the whole form.
  autofill_manager_->OnFillAutofillFormData(autofill_query_id_,
                                            autofill_query_form_,
                                            autofill_query_field_,
                                            unique_id);
}

void AutofillExternalDelegate::ApplyAutofillWarnings(
    std::vector<string16>* autofill_values,
    std::vector<string16>* autofill_labels,
    std::vector<string16>* autofill_icons,
    std::vector<int>* autofill_unique_ids) {
  if (!autofill_query_field_.should_autocomplete) {
    // If autofill is disabled and we had suggestions, show a warning instead.
    autofill_values->assign(
        1, l10n_util::GetStringUTF16(IDS_AUTOFILL_WARNING_FORM_DISABLED));
    autofill_labels->assign(1, string16());
    autofill_icons->assign(1, string16());
    autofill_unique_ids->assign(1, WebAutofillClient::MenuItemIDWarningMessage);
  } else if (autofill_unique_ids->size() > 1 &&
             (*autofill_unique_ids)[0] ==
                 WebAutofillClient::MenuItemIDWarningMessage) {
    // If we received a warning instead of suggestions from autofill but regular
    // suggestions from autocomplete, don't show the autofill warning.
    autofill_values->erase(autofill_values->begin());
    autofill_labels->erase(autofill_labels->begin());
    autofill_icons->erase(autofill_icons->begin());
    autofill_unique_ids->erase(autofill_unique_ids->begin());
  }

  // If we were about to show a warning and we shouldn't, don't.
  if (!autofill_unique_ids->empty() &&
      (*autofill_unique_ids)[0] ==
          WebAutofillClient::MenuItemIDWarningMessage &&
      !display_warning_if_disabled_) {
    autofill_values->clear();
    autofill_labels->clear();
    autofill_icons->clear();
    autofill_unique_ids->clear();
  }
}

void AutofillExternalDelegate::ApplyAutofillOptions(
    std::vector<string16>* autofill_values,
    std::vector<string16>* autofill_labels,
    std::vector<string16>* autofill_icons,
    std::vector<int>* autofill_unique_ids) {
  // The form has been auto-filled, so give the user the chance to clear the
  // form.  Append the 'Clear form' menu item.
  if (autofill_query_field_.is_autofilled) {
    autofill_values->push_back(
        l10n_util::GetStringUTF16(IDS_AUTOFILL_CLEAR_FORM_MENU_ITEM));
    autofill_labels->push_back(string16());
    autofill_icons->push_back(string16());
    autofill_unique_ids->push_back(WebAutofillClient::MenuItemIDClearForm);
  }

  // Append the 'Chrome Autofill options' menu item;
  autofill_values->push_back(
      l10n_util::GetStringUTF16(IDS_AUTOFILL_OPTIONS_POPUP));
  autofill_labels->push_back(string16());
  autofill_icons->push_back(string16());
  autofill_unique_ids->push_back(WebAutofillClient::MenuItemIDAutofillOptions);
}

void AutofillExternalDelegate::InsertDataListValues(
    std::vector<string16>* autofill_values,
    std::vector<string16>* autofill_labels,
    std::vector<string16>* autofill_icons,
    std::vector<int>* autofill_unique_ids) {
  if (data_list_values_.empty())
    return;

  // Insert the separator between the datalist and Autofill values (if there
  // are any).
  if (!autofill_values->empty()) {
    autofill_values->insert(autofill_values->begin(), string16());
    autofill_labels->insert(autofill_labels->begin(), string16());
    autofill_icons->insert(autofill_icons->begin(), string16());
    autofill_unique_ids->insert(autofill_unique_ids->begin(),
                                WebAutofillClient::MenuItemIDSeparator);
  }

  // Insert the datalist elements.
  autofill_values->insert(autofill_values->begin(),
                          data_list_values_.begin(),
                          data_list_values_.end());
  autofill_labels->insert(autofill_labels->begin(),
                          data_list_labels_.begin(),
                          data_list_labels_.end());
  autofill_icons->insert(autofill_icons->begin(),
                         data_list_icons_.begin(),
                         data_list_icons_.end());
  autofill_unique_ids->insert(autofill_unique_ids->begin(),
                              data_list_unique_ids_.begin(),
                              data_list_unique_ids_.end());
}

void AutofillExternalDelegate::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == content::NOTIFICATION_WEB_CONTENTS_VISIBILITY_CHANGED) {
    if (!*content::Details<bool>(details).ptr())
      HideAutofillPopup();
  } else if (type == content::NOTIFICATION_NAV_ENTRY_COMMITTED) {
    HideAutofillPopup();
  } else {
    NOTREACHED();
  }
}


