// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/autofill/form_manager.h"

#include "base/logging.h"
#include "base/memory/scoped_vector.h"
#include "base/stl_util-inl.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebElement.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFormControlElement.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebInputElement.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebLabelElement.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebNode.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebNodeList.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebOptionElement.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebSelectElement.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebString.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebVector.h"
#include "webkit/glue/form_data.h"
#include "webkit/glue/form_field.h"
#include "webkit/glue/web_io_operators.h"

using webkit_glue::FormData;
using webkit_glue::FormField;
using WebKit::WebDocument;
using WebKit::WebElement;
using WebKit::WebFormControlElement;
using WebKit::WebFormElement;
using WebKit::WebFrame;
using WebKit::WebInputElement;
using WebKit::WebLabelElement;
using WebKit::WebNode;
using WebKit::WebNodeList;
using WebKit::WebOptionElement;
using WebKit::WebSelectElement;
using WebKit::WebString;
using WebKit::WebVector;

namespace {

// The number of fields required by Autofill.  Ideally we could send the forms
// to Autofill no matter how many fields are in the forms; however, finding the
// label for each field is a costly operation and we can't spare the cycles if
// it's not necessary.
const size_t kRequiredAutofillFields = 3;

// The maximum number of form fields we are willing to parse, due to
// computational costs.  This is a very conservative upper bound.
const size_t kMaxParseableFields = 1000;

// The maximum length allowed for form data.
const size_t kMaxDataLength = 1024;

// In HTML5, all text fields except password are text input fields to
// autocomplete.
bool IsTextInput(const WebInputElement* element) {
  if (!element)
    return false;

  return element->isTextField() && !element->isPasswordField();
}

bool IsSelectElement(const WebFormControlElement& element) {
  return element.formControlType() == ASCIIToUTF16("select-one");
}

bool IsOptionElement(const WebElement& element) {
  return element.hasTagName("option");
}

bool IsAutofillableElement(const WebFormControlElement& element) {
  const WebInputElement* input_element = toWebInputElement(&element);
  return IsTextInput(input_element) || IsSelectElement(element);
}

// This is a helper function for the FindChildText() function (see below).
// Search depth is limited with the |depth| parameter.
string16 FindChildTextInner(const WebNode& node, int depth) {
  string16 element_text;
  if (depth <= 0 || node.isNull())
    return element_text;

  string16 node_text = node.nodeValue();
  TrimWhitespace(node_text, TRIM_ALL, &node_text);
  if (!node_text.empty())
    element_text = node_text;

  string16 child_text = FindChildTextInner(node.firstChild(), depth-1);
  if (!child_text.empty())
    element_text = element_text + child_text;

  string16 sibling_text = FindChildTextInner(node.nextSibling(), depth-1);
  if (!sibling_text.empty())
    element_text = element_text + sibling_text;

  return element_text;
}

// Returns the aggregated values of the descendants or siblings of |node| that
// are non-empty text nodes.  This is a faster alternative to |innerText()| for
// performance critical operations.  It does a full depth-first search so can be
// used when the structure is not directly known.  Whitespace is trimmed from
// text accumulated at descendant and sibling.  Search is limited to within 10
// siblings and/or descendants.
string16 FindChildText(const WebElement& element) {
  WebNode child = element.firstChild();

  const int kChildSearchDepth = 10;
  return FindChildTextInner(child, kChildSearchDepth);
}

// Helper for |InferLabelForElement()| that infers a label, if possible, from
// a previous node of |element|.
string16 InferLabelFromPrevious(const WebFormControlElement& element) {
  string16 inferred_label;
  WebNode previous = element.previousSibling();
  if (previous.isNull())
    return string16();

  if (previous.isTextNode()) {
    inferred_label = previous.nodeValue();
    TrimWhitespace(inferred_label, TRIM_ALL, &inferred_label);
  }

  // If we didn't find text, check for previous paragraph.
  // Eg. <p>Some Text</p><input ...>
  // Note the lack of whitespace between <p> and <input> elements.
  if (inferred_label.empty() && previous.isElementNode()) {
    WebElement element = previous.to<WebElement>();
    if (element.hasTagName("p")) {
      inferred_label = FindChildText(element);
    }
  }

  // If we didn't find paragraph, check for previous paragraph to this.
  // Eg. <p>Some Text</p>   <input ...>
  // Note the whitespace between <p> and <input> elements.
  if (inferred_label.empty()) {
    WebNode sibling = previous.previousSibling();
    if (!sibling.isNull() && sibling.isElementNode()) {
      WebElement element = sibling.to<WebElement>();
      if (element.hasTagName("p")) {
        inferred_label = FindChildText(element);
      }
    }
  }

  // Look for text node prior to <img> tag.
  // Eg. Some Text<img/><input ...>
  if (inferred_label.empty()) {
    while (inferred_label.empty() && !previous.isNull()) {
      if (previous.isTextNode()) {
        inferred_label = previous.nodeValue();
        TrimWhitespace(inferred_label, TRIM_ALL, &inferred_label);
      } else if (previous.isElementNode()) {
        WebElement element = previous.to<WebElement>();
        if (!element.hasTagName("img"))
          break;
      } else {
        break;
      }
      previous = previous.previousSibling();
    }
  }

  // Look for label node prior to <input> tag.
  // Eg. <label>Some Text</label><input ...>
  if (inferred_label.empty()) {
    while (inferred_label.empty() && !previous.isNull()) {
      if (previous.isTextNode()) {
        inferred_label = previous.nodeValue();
        TrimWhitespace(inferred_label, TRIM_ALL, &inferred_label);
      } else if (previous.isElementNode()) {
        WebElement element = previous.to<WebElement>();
        if (element.hasTagName("label")) {
          inferred_label = FindChildText(element);
        } else {
          break;
        }
      } else {
        break;
      }

      previous = previous.previousSibling();
    }
  }

  return inferred_label;
}

// Helper for |InferLabelForElement()| that infers a label, if possible, from
// surrounding table structure.
// Eg. <tr><td>Some Text</td><td><input ...></td></tr>
// Eg. <tr><td><b>Some Text</b></td><td><b><input ...></b></td></tr>
string16 InferLabelFromTable(const WebFormControlElement& element) {
  string16 inferred_label;
  WebNode parent = element.parentNode();
  while (!parent.isNull() && parent.isElementNode() &&
         !parent.to<WebElement>().hasTagName("td"))
    parent = parent.parentNode();

  // Check all previous siblings, skipping non-element nodes, until we find a
  // non-empty text block.
  WebNode previous = parent;
  while (!previous.isNull()) {
    if (previous.isElementNode()) {
      WebElement e = previous.to<WebElement>();
      if (e.hasTagName("td")) {
        inferred_label = FindChildText(e);
        if (!inferred_label.empty())
          break;
      }
    }

    previous = previous.previousSibling();
  }

  return inferred_label;
}

// Helper for |InferLabelForElement()| that infers a label, if possible, from
// a surrounding div table.
// Eg. <div>Some Text<span><input ...></span></div>
string16 InferLabelFromDivTable(const WebFormControlElement& element) {
  WebNode parent = element.parentNode();
  while (!parent.isNull() && parent.isElementNode() &&
         !parent.to<WebElement>().hasTagName("div"))
    parent = parent.parentNode();

  if (parent.isNull() || !parent.isElementNode())
    return string16();

  WebElement e = parent.to<WebElement>();
  if (e.isNull() || !e.hasTagName("div"))
    return string16();

  return FindChildText(e);
}

// Helper for |InferLabelForElement()| that infers a label, if possible, from
// a surrounding definition list.
// Eg. <dl><dt>Some Text</dt><dd><input ...></dd></dl>
// Eg. <dl><dt><b>Some Text</b></dt><dd><b><input ...></b></dd></dl>
string16 InferLabelFromDefinitionList(const WebFormControlElement& element) {
  string16 inferred_label;
  WebNode parent = element.parentNode();
  while (!parent.isNull() && parent.isElementNode() &&
         !parent.to<WebElement>().hasTagName("dd"))
    parent = parent.parentNode();

  if (!parent.isNull() && parent.isElementNode()) {
    WebElement element = parent.to<WebElement>();
    if (element.hasTagName("dd")) {
      WebNode previous = parent.previousSibling();

      // Skip by any intervening text nodes.
      while (!previous.isNull() && previous.isTextNode())
        previous = previous.previousSibling();

      if (!previous.isNull() && previous.isElementNode()) {
        element = previous.to<WebElement>();
        if (element.hasTagName("dt")) {
          inferred_label = FindChildText(element);
        }
      }
    }
  }

  return inferred_label;
}

// Infers corresponding label for |element| from surrounding context in the DOM.
// Contents of preceding <p> tag or preceding text element found in the form.
string16 InferLabelForElement(const WebFormControlElement& element) {
  string16 inferred_label = InferLabelFromPrevious(element);

  // If we didn't find a label, check for table cell case.
  if (inferred_label.empty())
    inferred_label = InferLabelFromTable(element);

  // If we didn't find a label, check for div table case.
  if (inferred_label.empty())
    inferred_label = InferLabelFromDivTable(element);

  // If we didn't find a label, check for definition list case.
  if (inferred_label.empty())
    inferred_label = InferLabelFromDefinitionList(element);

  return inferred_label;
}

void GetOptionStringsFromElement(const WebSelectElement& select_element,
                                 std::vector<string16>* option_strings) {
  DCHECK(!select_element.isNull());
  DCHECK(option_strings);

  option_strings->clear();
  WebVector<WebElement> list_items = select_element.listItems();
  option_strings->reserve(list_items.size());
  for (size_t i = 0; i < list_items.size(); ++i) {
    if (IsOptionElement(list_items[i])) {
      option_strings->push_back(
          list_items[i].toConst<WebOptionElement>().value());
    }
  }
}

// Returns the form's |name| attribute if non-empty; otherwise the form's |id|
// attribute.
const string16 GetFormIdentifier(const WebFormElement& form) {
  string16 identifier = form.name();
  if (identifier.empty())
    identifier = form.getAttribute(WebString("id"));

  return identifier;
}

}  // namespace

namespace autofill {

struct FormManager::FormElement {
  WebKit::WebFormElement form_element;
  std::vector<WebKit::WebFormControlElement> control_elements;
  std::vector<string16> control_values;
};

FormManager::FormManager() {
}

FormManager::~FormManager() {
  Reset();
}

// static
void FormManager::WebFormControlElementToFormField(
    const WebFormControlElement& element,
    ExtractMask extract_mask,
    FormField* field) {
  DCHECK(field);
  DCHECK(!element.isNull());

  // The label is not officially part of a WebFormControlElement; however, the
  // labels for all form control elements are scraped from the DOM and set in
  // WebFormElementToFormData.
  field->name = element.nameForAutofill();
  field->form_control_type = element.formControlType();

  if (!IsAutofillableElement(element))
    return;

  const WebInputElement* input_element = toWebInputElement(&element);
  if (IsTextInput(input_element)) {
    field->max_length = input_element->maxLength();
    field->is_autofilled = input_element->isAutofilled();
  } else if (extract_mask & EXTRACT_OPTIONS) {
    // Set option strings on the field if available.
    DCHECK(IsSelectElement(element));
    const WebSelectElement select_element = element.toConst<WebSelectElement>();
    std::vector<string16> option_strings;
    GetOptionStringsFromElement(select_element, &option_strings);
    field->option_strings = option_strings;
  }

  if (!(extract_mask & EXTRACT_VALUE))
    return;

  string16 value;
  if (IsTextInput(input_element)) {
    value = input_element->value();
  } else {
    DCHECK(IsSelectElement(element));
    const WebSelectElement select_element = element.toConst<WebSelectElement>();
    value = select_element.value();

    // Convert the |select_element| value to text if requested.
    if (extract_mask & EXTRACT_OPTION_TEXT) {
      WebVector<WebElement> list_items = select_element.listItems();
      for (size_t i = 0; i < list_items.size(); ++i) {
        if (IsOptionElement(list_items[i])) {
          const WebOptionElement option_element =
              list_items[i].toConst<WebOptionElement>();
          if (option_element.value() == value) {
            value = option_element.text();
            break;
          }
        }
      }
    }
  }

  // TODO(jhawkins): This is a temporary stop-gap measure designed to prevent
  // a malicious site from DOS'ing the browser with extremely large profile
  // data.  The correct solution is to parse this data asynchronously.
  // See http://crbug.com/49332.
  if (value.size() > kMaxDataLength)
    value = value.substr(0, kMaxDataLength);

  field->value = value;
}

// static
string16 FormManager::LabelForElement(const WebFormControlElement& element) {
  // Don't scrape labels for elements we can't possibly autofill anyway.
  if (!IsAutofillableElement(element))
    return string16();

  WebNodeList labels = element.document().getElementsByTagName("label");
  for (unsigned i = 0; i < labels.length(); ++i) {
    WebLabelElement label = labels.item(i).to<WebLabelElement>();
    DCHECK(label.hasTagName("label"));
    if (label.correspondingControl() == element)
      return FindChildText(label);
  }

  // Infer the label from context if not found in label element.
  return InferLabelForElement(element);
}

// static
bool FormManager::WebFormElementToFormData(const WebFormElement& element,
                                           RequirementsMask requirements,
                                           ExtractMask extract_mask,
                                           FormData* form) {
  DCHECK(form);

  const WebFrame* frame = element.document().frame();
  if (!frame)
    return false;

  if (requirements & REQUIRE_AUTOCOMPLETE && !element.autoComplete())
    return false;

  form->name = GetFormIdentifier(element);
  form->method = element.method();
  form->origin = frame->url();
  form->action = frame->document().completeURL(element.action());
  form->user_submitted = element.wasUserSubmitted();

  // If the completed URL is not valid, just use the action we get from
  // WebKit.
  if (!form->action.is_valid())
    form->action = GURL(element.action());

  // A map from a FormField's name to the FormField itself.
  std::map<string16, FormField*> name_map;

  // The extracted FormFields.  We use pointers so we can store them in
  // |name_map|.
  ScopedVector<FormField> form_fields;

  WebVector<WebFormControlElement> control_elements;
  element.getFormControlElements(control_elements);

  // A vector of bools that indicate whether each field in the form meets the
  // requirements and thus will be in the resulting |form|.
  std::vector<bool> fields_extracted(control_elements.size(), false);

  for (size_t i = 0; i < control_elements.size(); ++i) {
    const WebFormControlElement& control_element = control_elements[i];

    if (!IsAutofillableElement(control_element))
      continue;

    const WebInputElement* input_element = toWebInputElement(&control_element);
    if (requirements & REQUIRE_AUTOCOMPLETE && IsTextInput(input_element) &&
        !input_element->autoComplete())
      continue;

    if (requirements & REQUIRE_ENABLED && !control_element.isEnabled())
      continue;

    // Create a new FormField, fill it out and map it to the field's name.
    FormField* field = new FormField;
    WebFormControlElementToFormField(control_element, extract_mask, field);
    form_fields.push_back(field);
    // TODO(jhawkins): A label element is mapped to a form control element's id.
    // field->name() will contain the id only if the name does not exist.  Add
    // an id() method to WebFormControlElement and use that here.
    name_map[field->name] = field;
    fields_extracted[i] = true;
  }

  // Don't extract field labels if we have no fields.
  if (form_fields.empty())
    return false;

  // Loop through the label elements inside the form element.  For each label
  // element, get the corresponding form control element, use the form control
  // element's name as a key into the <name, FormField> map to find the
  // previously created FormField and set the FormField's label to the
  // label.firstChild().nodeValue() of the label element.
  WebNodeList labels = element.getElementsByTagName("label");
  for (unsigned i = 0; i < labels.length(); ++i) {
    WebLabelElement label = labels.item(i).to<WebLabelElement>();
    WebFormControlElement field_element =
        label.correspondingControl().to<WebFormControlElement>();
    if (field_element.isNull() ||
        !field_element.isFormControlElement() ||
        field_element.formControlType() == WebString::fromUTF8("hidden"))
      continue;

    std::map<string16, FormField*>::iterator iter =
        name_map.find(field_element.nameForAutofill());
    // Concatenate labels because some sites might have multiple label
    // candidates.
    if (iter != name_map.end())
      iter->second->label += FindChildText(label);
  }

  // Loop through the form control elements, extracting the label text from the
  // DOM.  We use the |fields_extracted| vector to make sure we assign the
  // extracted label to the correct field, as it's possible |form_fields| will
  // not contain all of the elements in |control_elements|.
  for (size_t i = 0, field_idx = 0;
       i < control_elements.size() && field_idx < form_fields.size(); ++i) {
    // This field didn't meet the requirements, so don't try to find a label for
    // it.
    if (!fields_extracted[i])
      continue;

    const WebFormControlElement& control_element = control_elements[i];
    if (form_fields[field_idx]->label.empty())
      form_fields[field_idx]->label = InferLabelForElement(control_element);

    ++field_idx;
  }

  // Copy the created FormFields into the resulting FormData object.
  for (ScopedVector<FormField>::const_iterator iter = form_fields.begin();
       iter != form_fields.end(); ++iter) {
    form->fields.push_back(**iter);
  }

  return true;
}

void FormManager::ExtractForms(const WebFrame* frame) {
  DCHECK(frame);

  // Reset the vector of FormElements for this frame.
  ResetFrame(frame);

  WebVector<WebFormElement> web_forms;
  frame->forms(web_forms);

  for (size_t i = 0; i < web_forms.size(); ++i) {
    // Owned by |form_elements_|.
    FormElement* form_element = new FormElement;
    form_element->form_element = web_forms[i];

    WebVector<WebFormControlElement> control_elements;
    form_element->form_element.getFormControlElements(control_elements);
    for (size_t j = 0; j < control_elements.size(); ++j) {
      WebFormControlElement element = control_elements[j];
      if (!IsAutofillableElement(element))
        continue;

      form_element->control_elements.push_back(element);

      // Save original values of <select> elements so we can restore them
      // when |ClearFormWithNode()| is invoked.
      if (IsSelectElement(element)) {
        form_element->control_values.push_back(
            element.toConst<WebSelectElement>().value());
      } else {
        form_element->control_values.push_back(string16());
      }
    }

    form_elements_.push_back(form_element);
  }
}

void FormManager::GetFormsInFrame(const WebFrame* frame,
                                  RequirementsMask requirements,
                                  std::vector<FormData>* forms) {
  DCHECK(frame);
  DCHECK(forms);

  size_t num_fields_seen = 0;
  for (FormElementList::const_iterator form_iter = form_elements_.begin();
       form_iter != form_elements_.end(); ++form_iter) {
    FormElement* form_element = *form_iter;

    if (form_element->form_element.document().frame() != frame)
      continue;

    // To avoid overly expensive computation, we impose both a minimum and a
    // maximum number of allowable fields.
    if (form_element->control_elements.size() < kRequiredAutofillFields ||
        form_element->control_elements.size() > kMaxParseableFields)
      continue;

    if (requirements & REQUIRE_AUTOCOMPLETE &&
        !form_element->form_element.autoComplete())
      continue;

    FormData form;
    WebFormElementToFormData(
        form_element->form_element, requirements, EXTRACT_VALUE, &form);

    num_fields_seen += form.fields.size();
    if (num_fields_seen > kMaxParseableFields)
      break;

    if (form.fields.size() >= kRequiredAutofillFields)
      forms->push_back(form);
  }
}

bool FormManager::FindFormWithFormControlElement(
    const WebFormControlElement& element,
    RequirementsMask requirements,
    FormData* form) {
  DCHECK(form);

  const WebFrame* frame = element.document().frame();
  if (!frame)
    return false;

  for (FormElementList::const_iterator iter = form_elements_.begin();
       iter != form_elements_.end(); ++iter) {
    const FormElement* form_element = *iter;

    if (form_element->form_element.document().frame() != frame)
      continue;

    for (std::vector<WebFormControlElement>::const_iterator iter =
             form_element->control_elements.begin();
         iter != form_element->control_elements.end(); ++iter) {
      if (iter->nameForAutofill() == element.nameForAutofill()) {
        ExtractMask extract_mask =
            static_cast<ExtractMask>(EXTRACT_VALUE | EXTRACT_OPTIONS);
        return WebFormElementToFormData(form_element->form_element,
                                        requirements,
                                        extract_mask,
                                        form);
      }
    }
  }

  return false;
}

bool FormManager::FillForm(const FormData& form, const WebNode& node) {
  FormElement* form_element = NULL;
  if (!FindCachedFormElement(form, &form_element))
    return false;

  RequirementsMask requirements = static_cast<RequirementsMask>(
      REQUIRE_AUTOCOMPLETE | REQUIRE_ENABLED | REQUIRE_EMPTY);
  ForEachMatchingFormField(form_element,
                           node,
                           requirements,
                           form,
                           NewCallback(this, &FormManager::FillFormField));

  return true;
}

bool FormManager::PreviewForm(const FormData& form, const WebNode& node) {
  FormElement* form_element = NULL;
  if (!FindCachedFormElement(form, &form_element))
    return false;

  RequirementsMask requirements = static_cast<RequirementsMask>(
      REQUIRE_AUTOCOMPLETE | REQUIRE_ENABLED | REQUIRE_EMPTY);
  ForEachMatchingFormField(form_element,
                           node,
                           requirements,
                           form,
                           NewCallback(this, &FormManager::PreviewFormField));

  return true;
}

bool FormManager::ClearFormWithNode(const WebNode& node) {
  FormElement* form_element = NULL;
  if (!FindCachedFormElementWithNode(node, &form_element))
    return false;

  for (size_t i = 0; i < form_element->control_elements.size(); ++i) {
    WebFormControlElement element = form_element->control_elements[i];
    WebInputElement* input_element = toWebInputElement(&element);
    if (IsTextInput(input_element)) {

      // We don't modify the value of disabled fields.
      if (!input_element->isEnabled())
        continue;

      input_element->setValue(string16(), true);
      input_element->setAutofilled(false);

      // Clearing the value in the focused node (above) can cause selection
      // to be lost. We force selection range to restore the text cursor.
      if (node == *input_element) {
        int length = input_element->value().length();
        input_element->setSelectionRange(length, length);
      }
    } else {
      DCHECK(IsSelectElement(element));
      WebSelectElement select_element = element.to<WebSelectElement>();
      if (select_element.value() != form_element->control_values[i]) {
        select_element.setValue(form_element->control_values[i]);
        select_element.dispatchFormControlChangeEvent();
      }
    }
  }

  return true;
}

bool FormManager::ClearPreviewedFormWithNode(const WebNode& node,
                                             bool was_autofilled) {
  FormElement* form_element = NULL;
  if (!FindCachedFormElementWithNode(node, &form_element))
    return false;

  for (size_t i = 0; i < form_element->control_elements.size(); ++i) {
    WebInputElement* input_element =
        toWebInputElement(&form_element->control_elements[i]);
    // Only text input elements can be previewed.
    if (!IsTextInput(input_element))
      continue;

    // If the input element has not been auto-filled, FormManager has not
    // previewed this field, so we have nothing to reset.
    if (!input_element->isAutofilled())
      continue;

    // There might be unrelated elements in this form which have already been
    // auto-filled. For example, the user might have already filled the address
    // part of a form and now be dealing with the credit card section. We only
    // want to reset the auto-filled status for fields that were previewed.
    if (input_element->suggestedValue().isEmpty())
      continue;

    // Clear the suggested value. For the initiating node, also restore the
    // original value.
    input_element->setSuggestedValue(WebString());
    bool is_initiating_node = (node == *input_element);
    if (is_initiating_node)
      input_element->setAutofilled(was_autofilled);
    else
      input_element->setAutofilled(false);

    // Clearing the suggested value in the focused node (above) can cause
    // selection to be lost. We force selection range to restore the text
    // cursor.
    if (is_initiating_node) {
      int length = input_element->value().length();
      input_element->setSelectionRange(length, length);
    }
  }

  return true;
}

void FormManager::Reset() {
  form_elements_.reset();
}

void FormManager::ResetFrame(const WebFrame* frame) {
  FormElementList::iterator iter = form_elements_.begin();
  while (iter != form_elements_.end()) {
    if ((*iter)->form_element.document().frame() == frame)
      iter = form_elements_.erase(iter);
    else
      ++iter;
  }
}

bool FormManager::FormWithNodeIsAutofilled(const WebNode& node) {
  FormElement* form_element = NULL;
  if (!FindCachedFormElementWithNode(node, &form_element))
    return false;

  for (size_t i = 0; i < form_element->control_elements.size(); ++i) {
    WebInputElement* input_element =
        toWebInputElement(&form_element->control_elements[i]);
    if (!IsTextInput(input_element))
      continue;

    if (input_element->isAutofilled())
      return true;
  }

  return false;
}

bool FormManager::FindCachedFormElementWithNode(const WebNode& node,
                                                FormElement** form_element) {
  for (FormElementList::const_iterator form_iter = form_elements_.begin();
       form_iter != form_elements_.end(); ++form_iter) {
    for (std::vector<WebFormControlElement>::const_iterator iter =
             (*form_iter)->control_elements.begin();
         iter != (*form_iter)->control_elements.end(); ++iter) {
      if (*iter == node) {
        *form_element = *form_iter;
        return true;
      }
    }
  }

  return false;
}

bool FormManager::FindCachedFormElement(const FormData& form,
                                        FormElement** form_element) {
  for (FormElementList::iterator form_iter = form_elements_.begin();
       form_iter != form_elements_.end(); ++form_iter) {
    // TODO(dhollowa): matching on form name here which is not guaranteed to
    // be unique for the page, nor is it guaranteed to be non-empty.  Need to
    // find a way to uniquely identify the form cross-process.  For now we'll
    // check form name and form action for identity.
    // http://crbug.com/37990 test file sample8.html.
    // Also note that WebString() == WebString(string16()) does not evaluate to
    // |true| -- WebKit distinguishes between a "null" string (lhs) and an
    // "empty" string (rhs).  We don't want that distinction, so forcing to
    // string16.
    string16 element_name = GetFormIdentifier((*form_iter)->form_element);
    GURL action(
        (*form_iter)->form_element.document().completeURL(
            (*form_iter)->form_element.action()));
    if (element_name == form.name && action == form.action) {
      *form_element = *form_iter;
      return true;
    }
  }

  return false;
}

void FormManager::ForEachMatchingFormField(FormElement* form,
                                           const WebNode& node,
                                           RequirementsMask requirements,
                                           const FormData& data,
                                           Callback* callback) {
  // It's possible that the site has injected fields into the form after the
  // page has loaded, so we can't assert that the size of the cached control
  // elements is equal to the size of the fields in |form|.  Fortunately, the
  // one case in the wild where this happens, paypal.com signup form, the fields
  // are appended to the end of the form and are not visible.
  for (size_t i = 0, j = 0;
       i < form->control_elements.size() && j < data.fields.size();
       ++i) {
    WebFormControlElement* element = &form->control_elements[i];
    string16 element_name(element->nameForAutofill());

    // Search forward in the |form| for a corresponding field.
    size_t k = j;
    while (k < data.fields.size() && element_name != data.fields[k].name)
      k++;

    if (k >= data.fields.size())
      continue;

    DCHECK_EQ(data.fields[k].name, element_name);

    bool is_initiating_node = false;

    const WebInputElement* input_element = toWebInputElement(element);
    if (IsTextInput(input_element)) {
      // TODO(jhawkins): WebKit currently doesn't handle the autocomplete
      // attribute for select control elements, but it probably should.
      if (!input_element->autoComplete())
        continue;

      is_initiating_node = (*input_element == node);

      // Only autofill empty fields and the field that initiated the filling,
      // i.e. the field the user is currently editing and interacting with.
      if (!is_initiating_node && !input_element->value().isEmpty())
        continue;
    }

    if (!element->isEnabled() || element->isReadOnly() ||
        !element->isFocusable())
      continue;

    callback->Run(element, &data.fields[k], is_initiating_node);

    // We found a matching form field so move on to the next.
    ++j;
  }

  delete callback;
}

void FormManager::FillFormField(WebFormControlElement* field,
                                const FormField* data,
                                bool is_initiating_node) {
  // Nothing to fill.
  if (data->value.empty())
    return;

  WebInputElement* input_element = toWebInputElement(field);
  if (IsTextInput(input_element)) {
    // If the maxlength attribute contains a negative value, maxLength()
    // returns the default maxlength value.
    input_element->setValue(
        data->value.substr(0, input_element->maxLength()), true);
    input_element->setAutofilled(true);
    if (is_initiating_node) {
      int length = input_element->value().length();
      input_element->setSelectionRange(length, length);
    }
  } else {
    DCHECK(IsSelectElement(*field));
    WebSelectElement select_element = field->to<WebSelectElement>();
    if (select_element.value() != data->value) {
      select_element.setValue(data->value);
      select_element.dispatchFormControlChangeEvent();
    }
  }
}

void FormManager::PreviewFormField(WebFormControlElement* field,
                                   const FormField* data,
                                   bool is_initiating_node) {
  // Nothing to preview.
  if (data->value.empty())
    return;

  // Only preview input fields.
  WebInputElement* input_element = toWebInputElement(field);
  if (!IsTextInput(input_element))
    return;

  // If the maxlength attribute contains a negative value, maxLength()
  // returns the default maxlength value.
  input_element->setSuggestedValue(
      data->value.substr(0, input_element->maxLength()));
  input_element->setAutofilled(true);
  if (is_initiating_node) {
    // Select the part of the text that the user didn't type.
    input_element->setSelectionRange(input_element->value().length(),
                                     input_element->suggestedValue().length());
  }
}

}  // namespace autofill
