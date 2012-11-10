// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ZOOM_ZOOM_CONTROLLER_H_
#define CHROME_BROWSER_UI_ZOOM_ZOOM_CONTROLLER_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/prefs/pref_member.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/web_contents_observer.h"

class TabContents;
class ZoomObserver;

// Per-tab class to manage the Omnibox zoom icon.
class ZoomController : public content::NotificationObserver,
                       public content::WebContentsObserver {
 public:
  enum ZoomIconState {
    NONE = 0,
    ZOOM_PLUS_ICON,
    ZOOM_MINUS_ICON,
  };

  explicit ZoomController(TabContents* tab_contents);
  virtual ~ZoomController();

  ZoomIconState zoom_icon_state() const { return zoom_icon_state_; }
  int zoom_percent() const { return zoom_percent_; }

  void set_observer(ZoomObserver* observer) { observer_ = observer; }

 private:
  // content::WebContentsObserver overrides:
  virtual void DidNavigateMainFrame(
      const content::LoadCommittedDetails& details,
      const content::FrameNavigateParams& params) OVERRIDE;

  // content::NotificationObserver overrides:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Updates the zoom icon and zoom percentage based on current values and
  // notifies the observer if changes have occurred. |can_show_bubble| will be
  // true only if the active window changes the zoom on the current page (i.e.,
  // inactive window zoom changes, creating a new tab/window, or shifting
  // between tabs/windows, although they may involve a change in the zoom, will
  // not trigger the bubble to be shown).
  void UpdateState(bool can_show_bubble);

  // The current zoom icon state.
  ZoomIconState zoom_icon_state_;

  // The current zoom percentage.
  int zoom_percent_;

  content::NotificationRegistrar registrar_;

  // Used to access the default zoom level preference.
  DoublePrefMember default_zoom_level_;

  // TabContents that owns this instance.
  TabContents* tab_contents_;

  // Observer receiving notifications on state changes.
  ZoomObserver* observer_;

  DISALLOW_COPY_AND_ASSIGN(ZoomController);
};

#endif  // CHROME_BROWSER_UI_ZOOM_ZOOM_CONTROLLER_H_
