// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/bookmarks/bookmark_context_menu_controller_views_win.h"

#include "base/win/metro.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "grit/generated_resources.h"
#include "ui/views/widget/widget.h"

using content::OpenURLParams;
using content::UserMetricsAction;
using content::WebContents;

namespace {

// A PageNavigator implementation that creates a new Browser. This is used when
// opening a url and there is no Browser open. The Browser is created the first
// time the PageNavigator method is invoked.
class NewBrowserPageNavigator : public content::PageNavigator {
 public:
  explicit NewBrowserPageNavigator(Profile* profile)
      : profile_(profile),
        browser_(NULL) {}

  virtual ~NewBrowserPageNavigator() {
    if (browser_)
      browser_->window()->Show();
  }

  Browser* browser() const { return browser_; }

  virtual WebContents* OpenURL(const OpenURLParams& params) OVERRIDE {
    if (!browser_) {
      Profile* profile = (params.disposition == OFF_THE_RECORD) ?
          profile_->GetOffTheRecordProfile() : profile_;
      browser_ = new Browser(Browser::CreateParams(profile));
    }

    OpenURLParams forward_params = params;
    forward_params.disposition = NEW_FOREGROUND_TAB;
    return browser_->OpenURL(forward_params);
  }

 private:
  Profile* profile_;
  Browser* browser_;

  DISALLOW_COPY_AND_ASSIGN(NewBrowserPageNavigator);
};

}  // namespace

// static
BookmarkContextMenuControllerViews* BookmarkContextMenuControllerViews::Create(
      views::Widget* parent_widget,
      BookmarkContextMenuControllerViewsDelegate* delegate,
      Browser* browser,
      Profile* profile,
      content::PageNavigator* navigator,
      const BookmarkNode* parent,
      const std::vector<const BookmarkNode*>& selection) {
  return new BookmarkContextMenuControllerViewsWin(parent_widget, delegate,
                                                   browser, profile, navigator,
                                                   parent, selection);
}

BookmarkContextMenuControllerViewsWin::BookmarkContextMenuControllerViewsWin(
      views::Widget* parent_widget,
      BookmarkContextMenuControllerViewsDelegate* delegate,
      Browser* browser,
      Profile* profile,
      content::PageNavigator* navigator,
      const BookmarkNode* parent,
      const std::vector<const BookmarkNode*>& selection)
    : BookmarkContextMenuControllerViews(parent_widget, delegate, browser,
                                         profile, navigator, parent,
                                         selection) {
}

BookmarkContextMenuControllerViewsWin
    ::~BookmarkContextMenuControllerViewsWin() {
}

void BookmarkContextMenuControllerViewsWin::ExecuteCommand(int id) {
  if (base::win::IsMetroProcess()) {
    switch (id) {
      // We need to handle the open in new window and open in incognito window
      // commands to ensure that they first look for an existing browser object
      // to handle the request. If we find one then a new foreground tab is
      // opened, else a new browser object is created.
      case IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW:
      case IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO: {
        Profile* profile_to_use = profile();
        if (id == IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW) {
          if (profile_to_use->IsOffTheRecord())
            profile_to_use = profile_to_use->GetOriginalProfile();

          content::RecordAction(
              UserMetricsAction("BookmarkBar_ContextMenu_OpenAllInNewWindow"));
        } else {
          if (!profile_to_use->IsOffTheRecord())
            profile_to_use = profile_to_use->GetOffTheRecordProfile();

          content::RecordAction(
              UserMetricsAction("BookmarkBar_ContextMenu_OpenAllIncognito"));
        }

        NewBrowserPageNavigator navigator_impl(profile_to_use);
        Browser* browser = browser::FindTabbedBrowser(profile_to_use, false);
        content::PageNavigator* navigator = NULL;
        if (!browser || !chrome::GetActiveWebContents(browser)) {
          navigator = &navigator_impl;
        } else {
          browser->window()->Activate();
          navigator = chrome::GetActiveWebContents(browser);
        }

        bookmark_utils::OpenAll(parent_widget()->GetNativeWindow(), navigator,
                                selection(), NEW_FOREGROUND_TAB);
        bookmark_utils::RecordBookmarkLaunch(
            bookmark_utils::LAUNCH_CONTEXT_MENU);
        return;
      }

      default:
        break;
    }
  }
  BookmarkContextMenuControllerViews::ExecuteCommand(id);
}

bool BookmarkContextMenuControllerViewsWin::IsCommandEnabled(int id) const {
  // In Windows 8 metro mode no new window option on a regular chrome window
  // and no new incognito window option on an incognito chrome window.
  if (base::win::IsMetroProcess()) {
    if (id == IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW &&
        !profile()->IsOffTheRecord()) {
      return false;
    } else if (id == IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO &&
               profile()->IsOffTheRecord()) {
      return false;
    }
  }
  return BookmarkContextMenuControllerViews::IsCommandEnabled(id);
}
