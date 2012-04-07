// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_AUTO_LOGIN_PROMPTER_H_
#define CHROME_BROWSER_UI_AUTO_LOGIN_PROMPTER_H_

#include <string>
#include "base/compiler_specific.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class GURL;

namespace content {
class WebContents;
}

namespace net {
class URLRequest;
}

// This class displays an infobar that allows the user to automatically login to
// the currently loaded page with one click.  This is used when the browser
// detects that the user has navigated to a login page and that there are stored
// tokens that would allow a one-click login.
class AutoLoginPrompter : public content::NotificationObserver {
 public:
  // Looks for the X-Auto-Login response header in the request, and if found,
  // tries to display an infobar in the tab contents identified by the
  // child/route id.
  static void ShowInfoBarIfPossible(net::URLRequest* request,
                                    int child_id,
                                    int route_id);

 private:
  AutoLoginPrompter(content::WebContents* web_contents,
                    const std::string& username,
                    const std::string& args,
                    const std::string& continue_url,
                    bool use_normal_auto_login_infobar);

  virtual ~AutoLoginPrompter();

  // The portion of ShowInfoBarIfPossible() that needs to run on the UI thread.
  static void ShowInfoBarUIThread(const std::string& account,
                                  const std::string& args,
                                  const GURL& original_url,
                                  int child_id,
                                  int route_id);

  // content::NotificationObserver override.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  content::WebContents* web_contents_;
  const std::string username_;
  const std::string args_;
  const std::string continue_url_;
  content::NotificationRegistrar registrar_;

  // There are two code flows for auto-login.  When the profile is connected
  // to a Google account, we want to show the infobar asking if the user would
  // like to automatically sign in.  This is the normal auto-login flow.
  // When the profile is not connected, we want to show an infobat asking if
  // the user would like to connect his profile instead.  This the reverse
  // auto-login flow.
  bool use_normal_auto_login_infobar_;

  DISALLOW_COPY_AND_ASSIGN(AutoLoginPrompter);
};

#endif  // CHROME_BROWSER_UI_AUTO_LOGIN_PROMPTER_H_
