// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_HISTORY_UI_H_
#define CHROME_BROWSER_UI_WEBUI_HISTORY_UI_H_
#pragma once

#include <string>

#include "base/string16.h"
#include "chrome/browser/history/history.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "content/browser/cancelable_request.h"
#include "content/browser/webui/web_ui.h"

class GURL;

// The handler for Javascript messages related to the "history" view.
class BrowsingHistoryHandler : public WebUIMessageHandler {
 public:
  BrowsingHistoryHandler();
  virtual ~BrowsingHistoryHandler();

  // WebUIMessageHandler implementation.
  virtual WebUIMessageHandler* Attach(WebUI* web_ui);
  virtual void RegisterMessages();

  // Callback for the "getHistory" message.
  void HandleGetHistory(const ListValue* args);

  // Callback for the "searchHistory" message.
  void HandleSearchHistory(const ListValue* args);

  // Callback for the "removeURLsOnOneDay" message.
  void HandleRemoveURLsOnOneDay(const ListValue* args);

  // Handle for "clearBrowsingData" message.
  void HandleClearBrowsingData(const ListValue* args);

 private:
  // Callback from the history system when the history list is available.
  void QueryComplete(HistoryService::Handle request_handle,
                     history::QueryResults* results);

  // Callback from the history system when visits were deleted.
  void RemoveComplete();

  // Extract the arguments from the call to HandleSearchHistory.
  void ExtractSearchHistoryArguments(const ListValue* args,
                                     int* month,
                                     string16* query);

  // Figure out the query options for a month-wide query.
  history::QueryOptions CreateMonthQueryOptions(int month);

  // Current search text.
  string16 search_text_;

  // Our consumer for search requests to the history service.
  CancelableRequestConsumerT<int, 0> cancelable_search_consumer_;

  // Our consumer for delete requests to the history service.
  CancelableRequestConsumerT<int, 0> cancelable_delete_consumer_;

  DISALLOW_COPY_AND_ASSIGN(BrowsingHistoryHandler);
};

class HistoryUI : public WebUI {
 public:
  explicit HistoryUI(TabContents* contents);

  // Return the URL for a given search term.
  static const GURL GetHistoryURLWithSearchText(const string16& text);

  static RefCountedMemory* GetFaviconResourceBytes();

 private:
  DISALLOW_COPY_AND_ASSIGN(HistoryUI);
};

#endif  // CHROME_BROWSER_UI_WEBUI_HISTORY_UI_H_
