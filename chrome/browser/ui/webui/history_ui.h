// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_HISTORY_UI_H_
#define CHROME_BROWSER_UI_WEBUI_HISTORY_UI_H_
#pragma once

#include <string>

#include "base/string16.h"
#include "chrome/browser/cancelable_request.h"
#include "chrome/browser/history/history.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"

// The handler for Javascript messages related to the "history" view.
class BrowsingHistoryHandler : public content::WebUIMessageHandler,
                               public content::NotificationObserver {
 public:
  BrowsingHistoryHandler();
  virtual ~BrowsingHistoryHandler();

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  // Callback for the "getHistory" message.
  void HandleGetHistory(const base::ListValue* args);

  // Callback for the "searchHistory" message.
  void HandleSearchHistory(const base::ListValue* args);

  // Callback for the "removeURLsOnOneDay" message.
  void HandleRemoveURLsOnOneDay(const base::ListValue* args);

  // Handle for "clearBrowsingData" message.
  void HandleClearBrowsingData(const base::ListValue* args);

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 private:
  // Callback from the history system when the history list is available.
  void QueryComplete(HistoryService::Handle request_handle,
                     history::QueryResults* results);

  // Callback from the history system when visits were deleted.
  void RemoveComplete();

  // Extract the arguments from the call to HandleSearchHistory.
  void ExtractSearchHistoryArguments(const base::ListValue* args,
                                     int* month,
                                     string16* query);

  // Figure out the query options for a month-wide query.
  history::QueryOptions CreateMonthQueryOptions(int month);

  content::NotificationRegistrar registrar_;

  // Current search text.
  string16 search_text_;

  // Our consumer for search requests to the history service.
  CancelableRequestConsumerT<int, 0> cancelable_search_consumer_;

  // Our consumer for delete requests to the history service.
  CancelableRequestConsumerT<int, 0> cancelable_delete_consumer_;

  // The list of URLs that are in the process of being deleted.
  std::set<GURL> urls_to_be_deleted_;

  DISALLOW_COPY_AND_ASSIGN(BrowsingHistoryHandler);
};

class HistoryUI : public content::WebUIController {
 public:
  explicit HistoryUI(content::WebUI* web_ui);

  // Return the URL for a given search term.
  static const GURL GetHistoryURLWithSearchText(const string16& text);

  static RefCountedMemory* GetFaviconResourceBytes();

 private:
  DISALLOW_COPY_AND_ASSIGN(HistoryUI);
};

#endif  // CHROME_BROWSER_UI_WEBUI_HISTORY_UI_H_
