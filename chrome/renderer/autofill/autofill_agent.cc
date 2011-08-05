// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/autofill/autofill_agent.h"

#include "base/message_loop.h"
#include "base/utf_string_conversions.h"
#include "chrome/common/autofill_messages.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/renderer/autofill/password_autofill_manager.h"
#include "content/renderer/render_view.h"
#include "grit/generated_resources.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFormControlElement.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebInputElement.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebInputEvent.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/base/l10n/l10n_util.h"
#include "webkit/glue/form_data.h"
#include "webkit/glue/form_field.h"
#include "webkit/glue/password_form.h"

using WebKit::WebFormControlElement;
using WebKit::WebFormElement;
using WebKit::WebFrame;
using WebKit::WebInputElement;
using WebKit::WebKeyboardEvent;
using WebKit::WebNode;
using WebKit::WebString;
using webkit_glue::FormData;

namespace {

// The size above which we stop triggering autofill for an input text field
// (so to avoid sending long strings through IPC).
const size_t kMaximumTextSizeForAutofill = 1000;

}  // namespace

namespace autofill {

AutofillAgent::AutofillAgent(
    RenderView* render_view,
    PasswordAutofillManager* password_autofill_manager)
    : RenderViewObserver(render_view),
      password_autofill_manager_(password_autofill_manager),
      autofill_query_id_(0),
      autofill_action_(AUTOFILL_NONE),
      display_warning_if_disabled_(false),
      was_query_node_autofilled_(false),
      suggestions_clear_index_(-1),
      suggestions_options_index_(-1),
      ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)) {
  render_view->webview()->setAutoFillClient(this);
}

AutofillAgent::~AutofillAgent() {}

bool AutofillAgent::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(AutofillAgent, message)
    IPC_MESSAGE_HANDLER(AutofillMsg_SuggestionsReturned, OnSuggestionsReturned)
    IPC_MESSAGE_HANDLER(AutofillMsg_FormDataFilled, OnFormDataFilled)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void AutofillAgent::DidFinishDocumentLoad(WebKit::WebFrame* frame) {
  // The document has now been fully loaded.  Scan for forms to be sent up to
  // the browser.
  form_manager_.ExtractForms(frame);
  SendForms(frame);
}

void AutofillAgent::FrameDetached(WebKit::WebFrame* frame) {
  form_manager_.ResetFrame(frame);
}

void AutofillAgent::FrameWillClose(WebKit::WebFrame* frame) {
  form_manager_.ResetFrame(frame);
}

void AutofillAgent::WillSubmitForm(WebFrame* frame,
                                   const WebFormElement& form) {
  FormData form_data;
  if (FormManager::WebFormElementToFormData(
          form,
          FormManager::REQUIRE_AUTOCOMPLETE,
          static_cast<FormManager::ExtractMask>(
              FormManager::EXTRACT_VALUE | FormManager::EXTRACT_OPTION_TEXT),
          &form_data)) {
    Send(new AutofillHostMsg_FormSubmitted(routing_id(), form_data));
  }
}

void AutofillAgent::FrameTranslated(WebKit::WebFrame* frame) {
  // The page is translated, so try to extract the form data again.
  DidFinishDocumentLoad(frame);
}

bool AutofillAgent::InputElementClicked(const WebInputElement& element,
                                        bool was_focused,
                                        bool is_focused) {
  if (was_focused)
    ShowSuggestions(element, true, false, true);
  return false;
}

void AutofillAgent::didAcceptAutoFillSuggestion(const WebKit::WebNode& node,
                                                const WebKit::WebString& value,
                                                const WebKit::WebString& label,
                                                int unique_id,
                                                unsigned index) {
  if (password_autofill_manager_->DidAcceptAutofillSuggestion(node, value))
    return;

  if (suggestions_options_index_ != -1 &&
      index == static_cast<unsigned>(suggestions_options_index_)) {
    // User selected 'Autofill Options'.
    Send(new AutofillHostMsg_ShowAutofillDialog(routing_id()));
  } else if (suggestions_clear_index_ != -1 &&
             index == static_cast<unsigned>(suggestions_clear_index_)) {
    // User selected 'Clear form'.
    form_manager_.ClearFormWithNode(node);
  } else if (!unique_id) {
    // User selected an Autocomplete entry, so we fill directly.
    WebInputElement element = node.toConst<WebInputElement>();

    string16 substring = value;
    substring = substring.substr(0, element.maxLength());
    element.setValue(substring, true);

    WebFrame* webframe = node.document().frame();
    if (webframe)
      webframe->notifiyPasswordListenerOfAutocomplete(element);
  } else {
    // Fill the values for the whole form.
    FillAutofillFormData(node, unique_id, AUTOFILL_FILL);
  }

  suggestions_clear_index_ = -1;
  suggestions_options_index_ = -1;
}

void AutofillAgent::didSelectAutoFillSuggestion(const WebKit::WebNode& node,
                                                const WebKit::WebString& value,
                                                const WebKit::WebString& label,
                                                int unique_id) {
  DCHECK_GE(unique_id, 0);
  if (password_autofill_manager_->DidSelectAutofillSuggestion(node))
    return;

  didClearAutoFillSelection(node);
  FillAutofillFormData(node, unique_id, AUTOFILL_PREVIEW);
}

void AutofillAgent::didClearAutoFillSelection(const WebKit::WebNode& node) {
  form_manager_.ClearPreviewedFormWithNode(node, was_query_node_autofilled_);
}

void AutofillAgent::removeAutocompleteSuggestion(
    const WebKit::WebString& name,
    const WebKit::WebString& value) {
  // The index of clear & options will have shifted down.
  if (suggestions_clear_index_ != -1)
    suggestions_clear_index_--;
  if (suggestions_options_index_ != -1)
    suggestions_options_index_--;

  Send(new AutofillHostMsg_RemoveAutocompleteEntry(routing_id(), name, value));
}

void AutofillAgent::textFieldDidEndEditing(
    const WebKit::WebInputElement& element) {
  password_autofill_manager_->TextFieldDidEndEditing(element);
}

void AutofillAgent::textFieldDidChange(const WebKit::WebInputElement& element) {
  // We post a task for doing the Autofill as the caret position is not set
  // properly at this point (http://bugs.webkit.org/show_bug.cgi?id=16976) and
  // it is needed to trigger autofill.
  method_factory_.RevokeAll();
  MessageLoop::current()->PostTask(
        FROM_HERE,
        method_factory_.NewRunnableMethod(
            &AutofillAgent::TextFieldDidChangeImpl, element));
}

void AutofillAgent::TextFieldDidChangeImpl(
    const WebKit::WebInputElement& element) {
  if (password_autofill_manager_->TextDidChangeInTextField(element))
    return;

  ShowSuggestions(element, false, true, false);
}

void AutofillAgent::textFieldDidReceiveKeyDown(
    const WebKit::WebInputElement& element,
    const WebKit::WebKeyboardEvent& event) {
  if (password_autofill_manager_->TextFieldHandlingKeyDown(element, event))
    return;

  if (event.windowsKeyCode == ui::VKEY_DOWN ||
      event.windowsKeyCode == ui::VKEY_UP)
    ShowSuggestions(element, true, true, true);
}

void AutofillAgent::OnSuggestionsReturned(int query_id,
                                          const std::vector<string16>& values,
                                          const std::vector<string16>& labels,
                                          const std::vector<string16>& icons,
                                          const std::vector<int>& unique_ids) {
  WebKit::WebView* web_view = render_view()->webview();
  if (!web_view || query_id != autofill_query_id_)
    return;

  if (values.empty()) {
    // No suggestions, any popup currently showing is obsolete.
    web_view->hidePopups();
    return;
  }

  std::vector<string16> v(values);
  std::vector<string16> l(labels);
  std::vector<string16> i(icons);
  std::vector<int> ids(unique_ids);
  int separator_index = -1;

  if (ids[0] < 0 && ids.size() > 1) {
    // If we received a warning instead of suggestions from autofill but regular
    // suggestions from autocomplete, don't show the autofill warning.
    v.erase(v.begin());
    l.erase(l.begin());
    i.erase(i.begin());
    ids.erase(ids.begin());
  }

  // If we were about to show a warning and we shouldn't, don't.
  if (ids[0] < 0 && !display_warning_if_disabled_)
    return;

  // Only include "Autofill Options" special menu item if we have Autofill
  // items, identified by |unique_ids| having at least one valid value.
  bool has_autofill_item = false;
  for (size_t i = 0; i < ids.size(); ++i) {
    if (ids[i] > 0) {
      has_autofill_item = true;
      break;
    }
  }

  // The form has been auto-filled, so give the user the chance to clear the
  // form.  Append the 'Clear form' menu item.
  if (has_autofill_item &&
      form_manager_.FormWithNodeIsAutofilled(autofill_query_node_)) {
    v.push_back(l10n_util::GetStringUTF16(IDS_AUTOFILL_CLEAR_FORM_MENU_ITEM));
    l.push_back(string16());
    i.push_back(string16());
    ids.push_back(0);
    suggestions_clear_index_ = v.size() - 1;
    separator_index = v.size() - 1;
  }

  if (has_autofill_item) {
    // Append the 'Chrome Autofill options' menu item;
    v.push_back(l10n_util::GetStringFUTF16(IDS_AUTOFILL_OPTIONS_POPUP,
        WideToUTF16(chrome::kBrowserAppName)));
    l.push_back(string16());
    i.push_back(string16());
    ids.push_back(0);
    suggestions_options_index_ = v.size() - 1;
    separator_index = values.size();
  }

  // Send to WebKit for display.
  if (!v.empty() && !autofill_query_node_.isNull() &&
      autofill_query_node_.isFocusable()) {
    web_view->applyAutoFillSuggestions(
        autofill_query_node_, v, l, i, ids, separator_index);
  }

  Send(new AutofillHostMsg_DidShowAutofillSuggestions(routing_id()));
}

void AutofillAgent::OnFormDataFilled(int query_id,
                                     const webkit_glue::FormData& form) {
  if (!render_view()->webview() || query_id != autofill_query_id_)
    return;

  switch (autofill_action_) {
    case AUTOFILL_FILL:
      form_manager_.FillForm(form, autofill_query_node_);
      break;
    case AUTOFILL_PREVIEW:
      form_manager_.PreviewForm(form, autofill_query_node_);
      break;
    default:
      NOTREACHED();
  }
  autofill_action_ = AUTOFILL_NONE;
  Send(new AutofillHostMsg_DidFillAutofillFormData(routing_id()));
}

void AutofillAgent::ShowSuggestions(const WebInputElement& element,
                                    bool autofill_on_empty_values,
                                    bool requires_caret_at_end,
                                    bool display_warning_if_disabled) {
  if (!element.isEnabled() || element.isReadOnly() || !element.autoComplete() ||
      !element.isTextField() || element.isPasswordField() ||
      !element.suggestedValue().isEmpty())
    return;

  // If the field has no name, then we won't have values.
  if (element.nameForAutofill().isEmpty())
    return;

  // Don't attempt to autofill with values that are too large.
  WebString value = element.value();
  if (value.length() > kMaximumTextSizeForAutofill)
    return;

  if (!autofill_on_empty_values && value.isEmpty())
    return;

  if (requires_caret_at_end &&
      (element.selectionStart() != element.selectionEnd() ||
       element.selectionEnd() != static_cast<int>(value.length())))
    return;

  QueryAutofillSuggestions(element, display_warning_if_disabled);
}

void AutofillAgent::QueryAutofillSuggestions(const WebNode& node,
                                             bool display_warning_if_disabled) {
  static int query_counter = 0;
  autofill_query_id_ = query_counter++;
  autofill_query_node_ = node;
  display_warning_if_disabled_ = display_warning_if_disabled;

  webkit_glue::FormData form;
  webkit_glue::FormField field;
  if (!FindFormAndFieldForNode(node, &form, &field)) {
    // If we didn't find the cached form, at least let autocomplete have a shot
    // at providing suggestions.
    FormManager::WebFormControlElementToFormField(
        node.toConst<WebFormControlElement>(), FormManager::EXTRACT_VALUE,
        &field);
  }

  Send(new AutofillHostMsg_QueryFormFieldAutofill(
      routing_id(), autofill_query_id_, form, field));
}

void AutofillAgent::FillAutofillFormData(const WebNode& node,
                                         int unique_id,
                                         AutofillAction action) {
  static int query_counter = 0;
  autofill_query_id_ = query_counter++;

  webkit_glue::FormData form;
  webkit_glue::FormField field;
  if (!FindFormAndFieldForNode(node, &form, &field))
    return;

  autofill_action_ = action;
  was_query_node_autofilled_ = field.is_autofilled;
  Send(new AutofillHostMsg_FillAutofillFormData(
      routing_id(), autofill_query_id_, form, field, unique_id));
}

void AutofillAgent::SendForms(WebFrame* frame) {
  std::vector<webkit_glue::FormData> forms;
  form_manager_.GetFormsInFrame(frame, FormManager::REQUIRE_NONE, &forms);

  if (!forms.empty())
    Send(new AutofillHostMsg_FormsSeen(routing_id(), forms));
}

bool AutofillAgent::FindFormAndFieldForNode(const WebNode& node,
                                            webkit_glue::FormData* form,
                                            webkit_glue::FormField* field) {
  const WebInputElement& element = node.toConst<WebInputElement>();
  if (!form_manager_.FindFormWithFormControlElement(element,
                                                    FormManager::REQUIRE_NONE,
                                                    form))
    return false;

  FormManager::WebFormControlElementToFormField(element,
                                                FormManager::EXTRACT_VALUE,
                                                field);

  // WebFormControlElementToFormField does not scrape the DOM for the field
  // label, so find the label here.
  // TODO(jhawkins): Add form and field identities so we can use the cached form
  // data in FormManager.
  field->label = FormManager::LabelForElement(element);

  return true;
}

}  // namespace autofill
