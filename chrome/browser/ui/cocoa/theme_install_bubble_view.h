// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_THEME_INSTALL_BUBBLE_VIEW_H_
#define CHROME_BROWSER_UI_COCOA_THEME_INSTALL_BUBBLE_VIEW_H_
#pragma once

#include "content/common/notification_observer.h"
#include "content/common/notification_registrar.h"

@class NSWindow;
@class ThemeInstallBubbleViewCocoa;

// ThemeInstallBubbleView is a view that provides a "Loading..." bubble in the
// center of a browser window for use when an extension or theme is loaded.
// (The Browser class only calls it to install itself into the currently active
// browser window.)  If an extension is being applied, the bubble goes away
// immediately.  If a theme is being applied, it disappears when the theme has
// been loaded.  The purpose of this bubble is to warn the user that the browser
// may be unresponsive while the theme is being installed.
//
// Edge case: note that if one installs a theme in one window and then switches
// rapidly to another window to install a theme there as well (in the short time
// between install begin and theme caching seizing the UI thread), the loading
// bubble will only appear over the first window, as there is only ever one
// instance of the bubble.
class ThemeInstallBubbleView : public NotificationObserver {
 public:
  virtual ~ThemeInstallBubbleView();

  // NotificationObserver
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // Show the loading bubble.
  static void Show(NSWindow* window);

 private:
  explicit ThemeInstallBubbleView(NSWindow* window);

  // The one copy of the loading bubble.
  static ThemeInstallBubbleView* view_;

  // A scoped container for notification registries.
  NotificationRegistrar registrar_;

  // Shut down the popup and remove our notifications.
  void Close();

  // The actual Cocoa view implementing the bubble.
  ThemeInstallBubbleViewCocoa* cocoa_view_;

  // Multiple loads can be started at once.  Only show one bubble, and keep
  // track of number of loads happening.  Close bubble when num_loads < 1.
  int num_loads_extant_;

  DISALLOW_COPY_AND_ASSIGN(ThemeInstallBubbleView);
};

#endif  // CHROME_BROWSER_UI_COCOA_THEME_INSTALL_BUBBLE_VIEW_H_
