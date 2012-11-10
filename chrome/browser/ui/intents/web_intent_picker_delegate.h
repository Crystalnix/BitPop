// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_INTENTS_WEB_INTENT_PICKER_DELEGATE_H_
#define CHROME_BROWSER_UI_INTENTS_WEB_INTENT_PICKER_DELEGATE_H_

#include <string>
#include "chrome/browser/ui/intents/web_intent_picker_model.h"

namespace content {
class WebContents;
}

// A class used to notify the delegate when the user has chosen a web intent
// service.
class WebIntentPickerDelegate {
 public:
  typedef WebIntentPickerModel::Disposition Disposition;

  // Base destructor.
  virtual ~WebIntentPickerDelegate() {}

  // Called when the user has chosen a service.
  virtual void OnServiceChosen(const GURL& url, Disposition disposition) = 0;

  // Called when the picker has created WebContents to use for inline
  // disposition.
  virtual void OnInlineDispositionWebContentsCreated(
      content::WebContents* web_contents) = 0;

  // Called when the user has chosen to install a suggested extension.
  virtual void OnExtensionInstallRequested(const std::string& id) = 0;

  // Called when the user has chosen to visit the CWS entry for an extension.
  virtual void OnExtensionLinkClicked(const std::string& id) = 0;

  // Called when the user chooses to get more suggestions from CWS.
  virtual void OnSuggestionsLinkClicked() = 0;

  // Called when the user cancels out of the dialog.
  virtual void OnPickerClosed() = 0;

  // Called when the user wants to pick another service from within inline
  // disposition.
  virtual void OnChooseAnotherService() = 0;

  // Called when the dialog stops showing.
  virtual void OnClosing() = 0;
};

#endif  // CHROME_BROWSER_UI_INTENTS_WEB_INTENT_PICKER_DELEGATE_H_
