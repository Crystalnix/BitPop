// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_FRIENDS_SIDEBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_FRIENDS_SIDEBAR_VIEW_H_

#include "chrome/browser/ui/views/tab_contents/tab_contents_container.h"

class Browser;
class BrowserView;
class TabContents;

class FriendsSidebarView : public TabContentsContainer {
public:
  FriendsSidebarView(Browser* browser, BrowserView* parent);
  virtual ~FriendsSidebarView();

  // Implementation of View.
  virtual gfx::Size GetPreferredSize();
  // virtual void Layout();
  virtual void OnPaint(gfx::Canvas* canvas);

private:
  void Init();
  
  /* data */
  Browser* browser_;
  BrowserView* parent_;
  static scoped_ptr<TabContents> extension_page_contents_;
};
#endif // CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_FRIENDS_SIDEBAR_VIEW_H_

