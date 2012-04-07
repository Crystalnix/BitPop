// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_modal_dialogs/message_box_handler.h"

#include <map>

#include "base/compiler_specific.h"
#include "base/memory/singleton.h"
#include "base/utf_string_conversions.h"
#include "base/i18n/rtl.h"
#include "chrome/browser/ui/app_modal_dialogs/app_modal_dialog_queue.h"
#include "chrome/browser/ui/app_modal_dialogs/js_modal_dialog.h"
#include "chrome/common/chrome_constants.h"
#include "grit/generated_resources.h"
#include "ui/base/javascript_message_type.h"
#include "ui/base/l10n/l10n_util.h"

using content::JavaScriptDialogCreator;
using content::WebContents;

class ChromeJavaScriptDialogCreator : public JavaScriptDialogCreator {
 public:
  static ChromeJavaScriptDialogCreator* GetInstance();

  virtual void RunJavaScriptDialog(
      WebContents* web_contents,
      TitleType title_type,
      const string16& title,
      ui::JavascriptMessageType javascript_message_type,
      const string16& message_text,
      const string16& default_prompt_text,
      const DialogClosedCallback& callback,
      bool* did_suppress_message) OVERRIDE;

  virtual void RunBeforeUnloadDialog(
      WebContents* web_contents,
      const string16& message_text,
      const DialogClosedCallback& callback) OVERRIDE;

  virtual void ResetJavaScriptState(WebContents* web_contents) OVERRIDE;

 private:
  explicit ChromeJavaScriptDialogCreator();
  virtual ~ChromeJavaScriptDialogCreator();

  friend struct DefaultSingletonTraits<ChromeJavaScriptDialogCreator>;

  string16 GetTitle(TitleType title_type,
                    const string16& title,
                    bool is_alert);

  void CancelPendingDialogs(WebContents* web_contents);

  // Mapping between the WebContents and their extra data. The key
  // is a void* because the pointer is just a cookie and is never dereferenced.
  typedef std::map<void*, ChromeJavaScriptDialogExtraData>
      JavaScriptDialogExtraDataMap;
  JavaScriptDialogExtraDataMap javascript_dialog_extra_data_;
};

//------------------------------------------------------------------------------

ChromeJavaScriptDialogCreator::ChromeJavaScriptDialogCreator() {
}

ChromeJavaScriptDialogCreator::~ChromeJavaScriptDialogCreator() {
}

/* static */
ChromeJavaScriptDialogCreator* ChromeJavaScriptDialogCreator::GetInstance() {
  return Singleton<ChromeJavaScriptDialogCreator>::get();
}

void ChromeJavaScriptDialogCreator::RunJavaScriptDialog(
    WebContents* web_contents,
    TitleType title_type,
    const string16& title,
    ui::JavascriptMessageType javascript_message_type,
    const string16& message_text,
    const string16& default_prompt_text,
    const DialogClosedCallback& callback,
    bool* did_suppress_message)  {
  *did_suppress_message = false;

  ChromeJavaScriptDialogExtraData* extra_data =
      &javascript_dialog_extra_data_[web_contents];

  if (extra_data->suppress_javascript_messages_) {
    *did_suppress_message = true;
    return;
  }

  base::TimeDelta time_since_last_message = base::TimeTicks::Now() -
      extra_data->last_javascript_message_dismissal_;
  bool display_suppress_checkbox = false;
  // Show a checkbox offering to suppress further messages if this message is
  // being displayed within kJavascriptMessageExpectedDelay of the last one.
  if (time_since_last_message <
      base::TimeDelta::FromMilliseconds(
          chrome::kJavascriptMessageExpectedDelay)) {
    display_suppress_checkbox = true;
  }

  bool is_alert = javascript_message_type == ui::JAVASCRIPT_MESSAGE_TYPE_ALERT;
  string16 dialog_title = GetTitle(title_type, title, is_alert);

  AppModalDialogQueue::GetInstance()->AddDialog(new JavaScriptAppModalDialog(
      web_contents,
      extra_data,
      dialog_title,
      javascript_message_type,
      message_text,
      default_prompt_text,
      display_suppress_checkbox,
      false,  // is_before_unload_dialog
      callback));
}

void ChromeJavaScriptDialogCreator::RunBeforeUnloadDialog(
    WebContents* web_contents,
    const string16& message_text,
    const DialogClosedCallback& callback) {
  ChromeJavaScriptDialogExtraData* extra_data =
      &javascript_dialog_extra_data_[web_contents];

  string16 full_message = message_text + ASCIIToUTF16("\n\n") +
      l10n_util::GetStringUTF16(IDS_BEFOREUNLOAD_MESSAGEBOX_FOOTER);

  AppModalDialogQueue::GetInstance()->AddDialog(new JavaScriptAppModalDialog(
      web_contents,
      extra_data,
      l10n_util::GetStringUTF16(IDS_BEFOREUNLOAD_MESSAGEBOX_TITLE),
      ui::JAVASCRIPT_MESSAGE_TYPE_CONFIRM,
      full_message,
      string16(),  // default_prompt_text
      false,       // display_suppress_checkbox
      true,        // is_before_unload_dialog
      callback));
}

void ChromeJavaScriptDialogCreator::ResetJavaScriptState(
    WebContents* web_contents) {
  CancelPendingDialogs(web_contents);
  javascript_dialog_extra_data_.erase(web_contents);
}

string16 ChromeJavaScriptDialogCreator::GetTitle(TitleType title_type,
                                                 const string16& title,
                                                 bool is_alert) {
  switch (title_type) {
    case DIALOG_TITLE_NONE: {
      return l10n_util::GetStringUTF16(
          is_alert ? IDS_JAVASCRIPT_ALERT_DEFAULT_TITLE
                   : IDS_JAVASCRIPT_MESSAGEBOX_DEFAULT_TITLE);
      break;
    }
    case DIALOG_TITLE_PLAIN_STRING: {
      return title;
      break;
    }
    case DIALOG_TITLE_FORMATTED_URL: {
      // Force URL to have LTR directionality.
      return l10n_util::GetStringFUTF16(
          is_alert ? IDS_JAVASCRIPT_ALERT_TITLE
                   : IDS_JAVASCRIPT_MESSAGEBOX_TITLE,
          base::i18n::GetDisplayStringInLTRDirectionality(title));
      break;
    }
  }
  NOTREACHED();
  return string16();
}

void ChromeJavaScriptDialogCreator::CancelPendingDialogs(
    WebContents* web_contents) {
  AppModalDialogQueue* queue = AppModalDialogQueue::GetInstance();
  AppModalDialog* active_dialog = queue->active_dialog();
  if (active_dialog && active_dialog->web_contents() == web_contents)
    active_dialog->Invalidate();
  for (AppModalDialogQueue::iterator i = queue->begin();
       i != queue->end(); ++i) {
    if ((*i)->web_contents() == web_contents)
      (*i)->Invalidate();
  }
}

//------------------------------------------------------------------------------

content::JavaScriptDialogCreator* GetJavaScriptDialogCreatorInstance() {
  return ChromeJavaScriptDialogCreator::GetInstance();
}
