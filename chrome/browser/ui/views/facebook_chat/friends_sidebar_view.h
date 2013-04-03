// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_FRIENDS_SIDEBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_FRIENDS_SIDEBAR_VIEW_H_

#include "chrome/browser/ui/views/extensions/extension_view.h"
//#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
//#include "content/public/browser/notification_source.h"
#include "ui/views/view.h"

class Browser;
class BrowserView;

namespace extensions {
  class ExtensionHost;
}

class FriendsSidebarView : public views::View,
                           public content::NotificationObserver,
                           public ExtensionView::Container {
public:
  FriendsSidebarView(Browser* browser, BrowserView* parent);
  virtual ~FriendsSidebarView();

  // Implementation of View.
  virtual gfx::Size GetPreferredSize() OVERRIDE;

protected:
  // content::NotificationObserver override
  virtual void Observe(int type,
      const content::NotificationSource& source,
      const content::NotificationDetails& details) OVERRIDE;

  // ExtensionView::Container override
  virtual void OnExtensionSizeChanged(ExtensionView* view) OVERRIDE;

private:
  void Init();

  void InitializeExtensionHost();

  /* data */
  Browser* browser_;
  BrowserView* parent_;
  scoped_ptr<extensions::ExtensionHost> extension_host_;

  content::NotificationRegistrar registrar_;

  //static scoped_ptr<TabContents> extension_page_contents_;
};
#endif // CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_FRIENDS_SIDEBAR_VIEW_H_

