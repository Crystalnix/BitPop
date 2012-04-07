// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_JUMPLIST_WIN_H_
#define CHROME_BROWSER_JUMPLIST_WIN_H_
#pragma once

#include <list>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/cancelable_request.h"
#include "chrome/browser/history/history.h"
#include "chrome/browser/history/history_types.h"
#include "chrome/browser/sessions/tab_restore_service.h"
#include "chrome/browser/sessions/tab_restore_service_observer.h"

class FilePath;
class Profile;
class PageUsageData;

// Represents a class used for creating an IShellLink object by the utility
// functions in this file.
// This class consists of three strings and a integer.
// * arguments (std::wstring)
//   The arguments for the application.
// * title (std::wstring)
//   The string to be displayed in a JumpList.
// * icon (std::wstring)
//   The absolute path to an icon to be displayed in a JumpList.
// * index (int)
//   The icon index in the icon file. If an icon file consists of two or more
//   icons, set this value to identify the icon. If an icon file consists of
// one icon, this value is 0.
// Even though an IShellLink also needs the absolute path to an application to
// be executed, this class does not have any variables for it because our
// utility functions always use "chrome.exe" as the application and we don't
// need it.
class ShellLinkItem : public base::RefCountedThreadSafe<ShellLinkItem> {
 public:
  ShellLinkItem() : index_(0), favicon_(false) {
  }

  const std::wstring& arguments() const { return arguments_; }
  const std::wstring& title() const { return title_; }
  const std::wstring& icon() const { return icon_; }
  int index() const { return index_; }
  scoped_refptr<RefCountedMemory> data() const { return data_; }

  void SetArguments(const std::wstring& arguments) {
    arguments_ = arguments;
  }

  void SetTitle(const std::wstring& title) {
    title_ = title;
  }

  void SetIcon(const std::wstring& icon, int index, bool favicon) {
    icon_ = icon;
    index_ = index;
    favicon_ = favicon;
  }

  void SetIconData(scoped_refptr<RefCountedMemory> data) {
    data_ = data;
  }

 private:
  friend class base::RefCountedThreadSafe<ShellLinkItem>;

  ~ShellLinkItem() {}

  std::wstring arguments_;
  std::wstring title_;
  std::wstring icon_;
  scoped_refptr<RefCountedMemory> data_;
  int index_;
  bool favicon_;

  DISALLOW_COPY_AND_ASSIGN(ShellLinkItem);
};

typedef std::vector<scoped_refptr<ShellLinkItem> > ShellLinkItemList;

// A class which implements an application JumpList.
// This class encapsulates operations required for updating an application
// JumpList:
// * Retrieving "Most Visited" pages from HistoryService;
// * Retrieving strings from the application resource;
// * Creatng COM objects used by JumpList from PageUsageData objects;
// * Adding COM objects to JumpList, etc.
//
// This class also implements TabRestoreServiceObserver. So, once we call
// AddObserver() and register this class as an observer, it automatically
// updates a JumpList when a tab is added or removed.
//
// Updating a JumpList requires some file operations and it is not good to
// update it in a UI thread. To solve this problem, this class posts to a
// runnable method when it actually updates a JumpList.
class JumpList : public TabRestoreServiceObserver,
                 public content::NotificationObserver,
                 public base::RefCountedThreadSafe<JumpList> {
 public:
  JumpList();

  // NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details);

  // Registers (or unregisters) this object as an observer.
  // When the TabRestoreService object notifies the tab status is changed, this
  // class automatically updates an application JumpList.
  bool AddObserver(Profile* profile);
  void RemoveObserver();

  // Observer callback for TabRestoreService::Observer to notify when a tab is
  // added or removed.
  // This function sends a query that retrieves "Most Visited" pages to
  // HistoryService. When the query finishes successfully, HistoryService call
  // OnSegmentUsageAvailable().
  virtual void TabRestoreServiceChanged(TabRestoreService* service);

  // Observer callback to notice when our associated TabRestoreService
  // is destroyed.
  virtual void TabRestoreServiceDestroyed(TabRestoreService* service);

  // Cancel a pending jumplist update.
  void CancelPendingUpdate();

  // Terminate the jumplist: cancel any pending updates and remove observer
  // from TabRestoreService. This must be called before the profile provided
  // in the AddObserver method is destroyed.
  void Terminate();

  // Returns true if the custom JumpList is enabled.
  // We use the custom JumpList when we satisfy the following conditions:
  // * Chromium is running on Windows 7 and;
  // * Chromium is lauched without a "--disable-custom-jumplist" option.
  // TODO(hbono): to be enabled by default when we finalize the categories and
  // items of our JumpList.
  static bool Enabled();

 protected:
  // Creates a ShellLinkItem object from a tab (or a window) and add it to the
  // given list.
  // These functions are copied from the RecentlyClosedTabsHandler class for
  // compatibility with the new-tab page.
  bool AddTab(const TabRestoreService::Tab* tab,
              ShellLinkItemList* list,
              size_t max_items);
  void AddWindow(const TabRestoreService::Window* window,
                 ShellLinkItemList* list,
                 size_t max_items);

  // Starts loading a favicon for each URL in |icon_urls_|.
  // This function just sends a query to HistoryService.
  bool StartLoadingFavicon();

  // A callback function for HistoryService that notify when the "Most Visited"
  // list is available.
  // This function updates the ShellLinkItemList objects and send another query
  // that retrieves a favicon for each URL in the list.
  void OnSegmentUsageAvailable(CancelableRequestProvider::Handle handle,
                               std::vector<PageUsageData*>* data);

  // A callback function for HistoryService that notify when a requested favicon
  // is available.
  // To avoid file operations, this function just attaches the given data to
  // a ShellLinkItem object.
  // When finishing loading all favicons, this function posts a task that
  // decompresses collected favicons and updates a JumpList.
  void OnFaviconDataAvailable(HistoryService::Handle handle,
                              history::FaviconData favicon);

  // Callback for TopSites that notifies when the "Most
  // Visited" list is available. This function updates the ShellLinkItemList
  // objects and send another query that retrieves a favicon for each URL in
  // the list.
  void OnMostVisitedURLsAvailable(
      const history::MostVisitedURLList& data);

  // Runnable method that updates the jumplist, once all the data
  // has been fetched.
  void RunUpdate();

  // Helper method for RunUpdate to decode the data about the asynchrounously
  // loaded icons.
  void DecodeIconData(const ShellLinkItemList& item_list);

 private:
  friend class base::RefCountedThreadSafe<JumpList>;
  ~JumpList();

  // Our consumers for HistoryService.
  CancelableRequestConsumer most_visited_consumer_;
  CancelableRequestConsumer favicon_consumer_;
  CancelableRequestConsumer topsites_consumer_;

  // The Profile object is used to listen for events
  Profile* profile_;

  content::NotificationRegistrar registrar_;

  // App id to associate with the jump list.
  std::wstring app_id_;

  // The directory which contains JumpList icons.
  FilePath icon_dir_;

  // Items in the "Most Visited" category of the application JumpList,
  // protected by the list_lock_.
  ShellLinkItemList most_visited_pages_;

  // Items in the "Recently Closed" category of the application JumpList,
  // protected by the list_lock_.
  ShellLinkItemList recently_closed_pages_;

  // A list of URLs we need to retrieve their favicons,
  // protected by the list_lock_.
  typedef std::pair<std::string, scoped_refptr<ShellLinkItem> > URLPair;
  std::list<URLPair> icon_urls_;

  // Handle of last favicon request used to cancel if a new request
  // comes in before the current one returns.
  FaviconService::Handle handle_;

  // Lock for most_visited_pages_, recently_closed_pages_, icon_urls_
  // as they may be used by up to 3 threads.
  base::Lock list_lock_;
};

#endif  // CHROME_BROWSER_JUMPLIST_WIN_H_
