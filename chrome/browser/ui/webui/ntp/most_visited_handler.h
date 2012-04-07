// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_NTP_MOST_VISITED_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_NTP_MOST_VISITED_HANDLER_H_
#pragma once

#include <string>
#include <vector>

#include "chrome/browser/cancelable_request.h"
#include "chrome/browser/history/history_types.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/web_ui_message_handler.h"

class GURL;
class PageUsageData;
class PrefService;

namespace base {
class ListValue;
class Value;
}

// The handler for Javascript messages related to the "most visited" view.
//
// This class manages one preference:
// - The URL blacklist: URLs we do not want to show in the thumbnails list.  It
//   is a dictionary for quick access (it associates a dummy boolean to the URL
//   string).
class MostVisitedHandler : public content::WebUIMessageHandler,
                           public content::NotificationObserver {
 public:

  MostVisitedHandler();
  virtual ~MostVisitedHandler();

  // WebUIMessageHandler override and implementation.
  virtual void RegisterMessages() OVERRIDE;

  // Callback for the "getMostVisited" message.
  void HandleGetMostVisited(const base::ListValue* args);

  // Callback for the "blacklistURLFromMostVisited" message.
  void HandleBlacklistURL(const base::ListValue* args);

  // Callback for the "removeURLsFromMostVisitedBlacklist" message.
  void HandleRemoveURLsFromBlacklist(const base::ListValue* args);

  // Callback for the "clearMostVisitedURLsBlacklist" message.
  void HandleClearBlacklist(const base::ListValue* args);

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  const std::vector<GURL>& most_visited_urls() const {
    return most_visited_urls_;
  }

  static void RegisterUserPrefs(PrefService* prefs);

 private:
  struct MostVisitedPage;

  // Send a request to the HistoryService to get the most visited pages.
  void StartQueryForMostVisited();

  // Sets pages_value_ from a format produced by TopSites.
  void SetPagesValueFromTopSites(const history::MostVisitedURLList& data);

  // Callback for TopSites.
  void OnMostVisitedURLsAvailable(const history::MostVisitedURLList& data);

  // Puts the passed URL in the blacklist (so it does not show as a thumbnail).
  void BlacklistURL(const GURL& url);

  // Returns the key used in url_blacklist_ for the passed |url|.
  std::string GetDictionaryKeyForURL(const std::string& url);

  // Sends pages_value_ to the javascript side to and resets page_value_.
  void SendPagesValue();

  content::NotificationRegistrar registrar_;

  // Our consumer for the history service.
  CancelableRequestConsumerTSimple<PageUsageData*> cancelable_consumer_;
  CancelableRequestConsumer topsites_consumer_;

  // The most visited URLs, in priority order.
  // Only used for matching up clicks on the page to which most visited entry
  // was clicked on for metrics purposes.
  std::vector<GURL> most_visited_urls_;

  // We pre-fetch the first set of result pages.  This variable is false until
  // we get the first getMostVisited() call.
  bool got_first_most_visited_request_;

  // Keep the results of the db query here.
  scoped_ptr<base::ListValue> pages_value_;

  DISALLOW_COPY_AND_ASSIGN(MostVisitedHandler);
};

#endif  // CHROME_BROWSER_UI_WEBUI_NTP_MOST_VISITED_HANDLER_H_
