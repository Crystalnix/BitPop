// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/utf_string_conversions.h"
#include "chrome/common/autofill_messages.h"
#include "chrome/test/render_view_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebInputElement.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebString.h"
#include "webkit/glue/form_data.h"
#include "webkit/glue/form_field.h"

using WebKit::WebDocument;
using WebKit::WebFrame;
using WebKit::WebInputElement;
using WebKit::WebString;
using webkit_glue::FormData;
using webkit_glue::FormField;

TEST_F(RenderViewTest, SendForms) {
  // Don't want any delay for form state sync changes. This will still post a
  // message so updates will get coalesced, but as soon as we spin the message
  // loop, it will generate an update.
  view_->set_send_content_state_immediately(true);

  LoadHTML("<form method=\"POST\">"
           "  <input type=\"text\" id=\"firstname\"/>"
           "  <input type=\"text\" id=\"middlename\" autoComplete=\"off\"/>"
           "  <input type=\"hidden\" id=\"lastname\"/>"
           "  <select id=\"state\"/>"
           "    <option>?</option>"
           "    <option>California</option>"
           "    <option>Texas</option>"
           "  </select>"
           "</form>");

  // Verify that "FormsSeen" sends the expected number of fields.
  ProcessPendingMessages();
  const IPC::Message* message = render_thread_.sink().GetFirstMessageMatching(
      AutofillHostMsg_FormsSeen::ID);
  ASSERT_NE(static_cast<IPC::Message*>(NULL), message);
  AutofillHostMsg_FormsSeen::Param params;
  AutofillHostMsg_FormsSeen::Read(message, &params);
  const std::vector<FormData>& forms = params.a;
  ASSERT_EQ(1UL, forms.size());
  ASSERT_EQ(3UL, forms[0].fields.size());
  EXPECT_TRUE(forms[0].fields[0].StrictlyEqualsHack(
      FormField(string16(),
                ASCIIToUTF16("firstname"),
                string16(),
                ASCIIToUTF16("text"),
                WebInputElement::defaultMaxLength(),
                false))) << forms[0].fields[0];
  EXPECT_TRUE(forms[0].fields[1].StrictlyEqualsHack(
      FormField(string16(),
                ASCIIToUTF16("middlename"),
                string16(),
                ASCIIToUTF16("text"),
                WebInputElement::defaultMaxLength(),
                false))) << forms[0].fields[1];
  EXPECT_TRUE(forms[0].fields[2].StrictlyEqualsHack(
      FormField(string16(),
                ASCIIToUTF16("state"),
                ASCIIToUTF16("?"),
                ASCIIToUTF16("select-one"),
                0,
                false))) << forms[0].fields[2];

  // Verify that |didAcceptAutoFillSuggestion()| sends the expected number of
  // fields.
  WebFrame* web_frame = GetMainFrame();
  WebDocument document = web_frame->document();
  WebInputElement firstname =
      document.getElementById("firstname").to<WebInputElement>();

  // Accept suggestion that contains a label.  Labeled items indicate Autofill
  // as opposed to Autocomplete.  We're testing this distinction below with
  // the |AutofillHostMsg_FillAutofillFormData::ID| message.
  autofill_agent_->didAcceptAutoFillSuggestion(
      firstname,
      WebKit::WebString::fromUTF8("Johnny"),
      WebKit::WebString::fromUTF8("Home"),
      1,
      -1);

  ProcessPendingMessages();
  const IPC::Message* message2 = render_thread_.sink().GetUniqueMessageMatching(
      AutofillHostMsg_FillAutofillFormData::ID);
  ASSERT_NE(static_cast<IPC::Message*>(NULL), message2);
  AutofillHostMsg_FillAutofillFormData::Param params2;
  AutofillHostMsg_FillAutofillFormData::Read(message2, &params2);
  const FormData& form2 = params2.b;
  ASSERT_EQ(3UL, form2.fields.size());
  EXPECT_TRUE(form2.fields[0].StrictlyEqualsHack(
      FormField(string16(),
                ASCIIToUTF16("firstname"),
                string16(),
                ASCIIToUTF16("text"),
                WebInputElement::defaultMaxLength(),
                false))) << form2.fields[0];
  EXPECT_TRUE(form2.fields[1].StrictlyEqualsHack(
      FormField(string16(),
                ASCIIToUTF16("middlename"),
                string16(),
                ASCIIToUTF16("text"),
                WebInputElement::defaultMaxLength(),
                false))) << form2.fields[1];
  EXPECT_TRUE(form2.fields[2].StrictlyEqualsHack(
      FormField(string16(),
                ASCIIToUTF16("state"),
                ASCIIToUTF16("?"),
                ASCIIToUTF16("select-one"),
                0,
                false))) << form2.fields[2];
}

TEST_F(RenderViewTest, FillFormElement) {
  // Don't want any delay for form state sync changes. This will still post a
  // message so updates will get coalesced, but as soon as we spin the message
  // loop, it will generate an update.
  view_->set_send_content_state_immediately(true);

  LoadHTML("<form method=\"POST\">"
           "  <input type=\"text\" id=\"firstname\"/>"
           "  <input type=\"text\" id=\"middlename\"/>"
           "</form>");

  // Verify that "FormsSeen" isn't sent, as there are too few fields.
  ProcessPendingMessages();
  const IPC::Message* message = render_thread_.sink().GetFirstMessageMatching(
      AutofillHostMsg_FormsSeen::ID);
  ASSERT_EQ(static_cast<IPC::Message*>(NULL), message);

  // Verify that |didAcceptAutoFillSuggestion()| sets the value of the expected
  // field.
  WebFrame* web_frame = GetMainFrame();
  WebDocument document = web_frame->document();
  WebInputElement firstname =
      document.getElementById("firstname").to<WebInputElement>();
  WebInputElement middlename =
      document.getElementById("middlename").to<WebInputElement>();
  middlename.setAutofilled(true);

  // Accept a suggestion in a form that has been auto-filled.  This triggers
  // the direct filling of the firstname element with value parameter.
  autofill_agent_->didAcceptAutoFillSuggestion(firstname,
                                               WebString::fromUTF8("David"),
                                               WebString(),
                                               0,
                                               0);

  ProcessPendingMessages();
  const IPC::Message* message2 = render_thread_.sink().GetUniqueMessageMatching(
      AutofillHostMsg_FillAutofillFormData::ID);

  // No message should be sent in this case.  |firstname| is filled directly.
  ASSERT_EQ(static_cast<IPC::Message*>(NULL), message2);
  EXPECT_EQ(firstname.value(), WebKit::WebString::fromUTF8("David"));
}
