// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_modal_dialogs/javascript_dialog_creator.h"

#include <map>

#include "base/compiler_specific.h"
#include "base/i18n/rtl.h"
#include "base/memory/singleton.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/ui/app_modal_dialogs/app_modal_dialog_queue.h"
#include "chrome/browser/ui/app_modal_dialogs/javascript_app_modal_dialog.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/common/content_client.h"
#include "content/public/common/javascript_message_type.h"
#include "grit/generated_resources.h"
#include "net/base/net_util.h"
#include "ui/base/l10n/l10n_util.h"

using content::JavaScriptDialogCreator;
using content::WebContents;

namespace {

class ChromeJavaScriptDialogCreator : public JavaScriptDialogCreator,
                                      public content::NotificationObserver {
 public:
  static ChromeJavaScriptDialogCreator* GetInstance();

  explicit ChromeJavaScriptDialogCreator(
      extensions::ExtensionHost* extension_host);
  virtual ~ChromeJavaScriptDialogCreator();

  virtual void RunJavaScriptDialog(
      WebContents* web_contents,
      const GURL& origin_url,
      const std::string& accept_lang,
      content::JavaScriptMessageType message_type,
      const string16& message_text,
      const string16& default_prompt_text,
      const DialogClosedCallback& callback,
      bool* did_suppress_message) OVERRIDE;

  virtual void RunBeforeUnloadDialog(
      WebContents* web_contents,
      const string16& message_text,
      bool is_reload,
      const DialogClosedCallback& callback) OVERRIDE;

  virtual void ResetJavaScriptState(WebContents* web_contents) OVERRIDE;

 private:
  ChromeJavaScriptDialogCreator();

  friend struct DefaultSingletonTraits<ChromeJavaScriptDialogCreator>;

  // Overridden from content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  string16 GetTitle(const GURL& origin_url,
                    const std::string& accept_lang,
                    bool is_alert);

  void CancelPendingDialogs(WebContents* web_contents);

  // Wrapper around a DialogClosedCallback so that we can intercept it before
  // passing it onto the original callback.
  void OnDialogClosed(DialogClosedCallback callback,
                      bool success,
                      const string16& user_input);

  // Mapping between the WebContents and their extra data. The key
  // is a void* because the pointer is just a cookie and is never dereferenced.
  typedef std::map<void*, ChromeJavaScriptDialogExtraData>
      JavaScriptDialogExtraDataMap;
  JavaScriptDialogExtraDataMap javascript_dialog_extra_data_;

  // Extension Host which owns the ChromeJavaScriptDialogCreator instance.
  // It's used to get a extension name from a URL.
  // If it's not owned by any Extension, it should be NULL.
  extensions::ExtensionHost* extension_host_;

  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(ChromeJavaScriptDialogCreator);
};

////////////////////////////////////////////////////////////////////////////////
// ChromeJavaScriptDialogCreator, public:

ChromeJavaScriptDialogCreator::ChromeJavaScriptDialogCreator()
    : extension_host_(NULL) {
}

ChromeJavaScriptDialogCreator::~ChromeJavaScriptDialogCreator() {
  extension_host_ = NULL;
}

ChromeJavaScriptDialogCreator::ChromeJavaScriptDialogCreator(
    extensions::ExtensionHost* extension_host)
    : extension_host_(extension_host) {
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_HOST_DESTROYED,
                 content::Source<Profile>(extension_host_->profile()));
}

// static
ChromeJavaScriptDialogCreator* ChromeJavaScriptDialogCreator::GetInstance() {
  return Singleton<ChromeJavaScriptDialogCreator>::get();
}

void ChromeJavaScriptDialogCreator::RunJavaScriptDialog(
    WebContents* web_contents,
    const GURL& origin_url,
    const std::string& accept_lang,
    content::JavaScriptMessageType message_type,
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

  bool is_alert = message_type == content::JAVASCRIPT_MESSAGE_TYPE_ALERT;
  string16 dialog_title = GetTitle(origin_url, accept_lang, is_alert);

  if (extension_host_)
    extension_host_->WillRunJavaScriptDialog();

  AppModalDialogQueue::GetInstance()->AddDialog(new JavaScriptAppModalDialog(
      web_contents,
      extra_data,
      dialog_title,
      message_type,
      message_text,
      default_prompt_text,
      display_suppress_checkbox,
      false,  // is_before_unload_dialog
      false,  // is_reload
      base::Bind(&ChromeJavaScriptDialogCreator::OnDialogClosed,
                 base::Unretained(this), callback)));
}

void ChromeJavaScriptDialogCreator::RunBeforeUnloadDialog(
    WebContents* web_contents,
    const string16& message_text,
    bool is_reload,
    const DialogClosedCallback& callback) {
  ChromeJavaScriptDialogExtraData* extra_data =
      &javascript_dialog_extra_data_[web_contents];

  const string16 title = l10n_util::GetStringUTF16(is_reload ?
      IDS_BEFORERELOAD_MESSAGEBOX_TITLE : IDS_BEFOREUNLOAD_MESSAGEBOX_TITLE);
  const string16 footer = l10n_util::GetStringUTF16(is_reload ?
      IDS_BEFORERELOAD_MESSAGEBOX_FOOTER : IDS_BEFOREUNLOAD_MESSAGEBOX_FOOTER);

  string16 full_message = message_text + ASCIIToUTF16("\n\n") + footer;

  if (extension_host_)
    extension_host_->WillRunJavaScriptDialog();

  AppModalDialogQueue::GetInstance()->AddDialog(new JavaScriptAppModalDialog(
      web_contents,
      extra_data,
      title,
      content::JAVASCRIPT_MESSAGE_TYPE_CONFIRM,
      full_message,
      string16(),  // default_prompt_text
      false,       // display_suppress_checkbox
      true,        // is_before_unload_dialog
      is_reload,
      base::Bind(&ChromeJavaScriptDialogCreator::OnDialogClosed,
                 base::Unretained(this), callback)));
}

void ChromeJavaScriptDialogCreator::ResetJavaScriptState(
    WebContents* web_contents) {
  CancelPendingDialogs(web_contents);
  javascript_dialog_extra_data_.erase(web_contents);
}

void ChromeJavaScriptDialogCreator::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(type, chrome::NOTIFICATION_EXTENSION_HOST_DESTROYED);
  extension_host_ = NULL;
}

string16 ChromeJavaScriptDialogCreator::GetTitle(const GURL& origin_url,
                                                 const std::string& accept_lang,
                                                 bool is_alert) {
  // If the URL hasn't any host, return the default string.
  if (!origin_url.has_host()) {
      return l10n_util::GetStringUTF16(
          is_alert ? IDS_JAVASCRIPT_ALERT_DEFAULT_TITLE
                   : IDS_JAVASCRIPT_MESSAGEBOX_DEFAULT_TITLE);
  }

  // If the URL is a chrome extension one, return the extension name.
  if (extension_host_) {
    const extensions::Extension* extension = extension_host_->
      profile()->GetExtensionService()->extensions()->
      GetExtensionOrAppByURL(ExtensionURLInfo(origin_url));
    if (extension) {
      return UTF8ToUTF16(base::StringPiece(extension->name()));
    }
  }

  // Otherwise, return the formatted URL.
  // In this case, force URL to have LTR directionality.
  string16 url_string = net::FormatUrl(origin_url, accept_lang);
  return l10n_util::GetStringFUTF16(
      is_alert ? IDS_JAVASCRIPT_ALERT_TITLE
      : IDS_JAVASCRIPT_MESSAGEBOX_TITLE,
      base::i18n::GetDisplayStringInLTRDirectionality(url_string));
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

void ChromeJavaScriptDialogCreator::OnDialogClosed(
    DialogClosedCallback callback,
    bool success,
    const string16& user_input) {
  if (extension_host_)
    extension_host_->DidCloseJavaScriptDialog();
  callback.Run(success, user_input);
}

}  // namespace

content::JavaScriptDialogCreator* GetJavaScriptDialogCreatorInstance() {
  return ChromeJavaScriptDialogCreator::GetInstance();
}

content::JavaScriptDialogCreator* CreateJavaScriptDialogCreatorInstance(
    extensions::ExtensionHost* extension_host) {
  return new ChromeJavaScriptDialogCreator(extension_host);
}
