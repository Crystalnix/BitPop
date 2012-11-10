// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS2_CLEAR_BROWSER_DATA_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS2_CLEAR_BROWSER_DATA_HANDLER_H_

#include "chrome/browser/browsing_data/browsing_data_remover.h"
#include "chrome/browser/prefs/pref_member.h"
#include "chrome/browser/ui/webui/options2/options_ui.h"

namespace options2 {

// Clear browser data handler page UI handler.
class ClearBrowserDataHandler : public OptionsPageUIHandler,
                                public BrowsingDataRemover::Observer {
 public:
  ClearBrowserDataHandler();
  virtual ~ClearBrowserDataHandler();

  // OptionsPageUIHandler implementation.
  virtual void GetLocalizedValues(DictionaryValue* localized_strings) OVERRIDE;
  virtual void InitializeHandler() OVERRIDE;

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

 private:
  // Javascript callback to start clearing data.
  void HandleClearBrowserData(const ListValue* value);

  // BrowsingDataRemover::Observer implementation.
  // Closes the dialog once all requested data has been removed.
  virtual void OnBrowsingDataRemoverDone() OVERRIDE;

  // If non-null it means removal is in progress. BrowsingDataRemover takes care
  // of deleting itself when done.
  BrowsingDataRemover* remover_;

  // Keeps track of whether clearing LSO data is supported.
  BooleanPrefMember clear_plugin_lso_data_enabled_;

  DISALLOW_COPY_AND_ASSIGN(ClearBrowserDataHandler);
};

}  // namespace options2

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS2_CLEAR_BROWSER_DATA_HANDLER_H_
