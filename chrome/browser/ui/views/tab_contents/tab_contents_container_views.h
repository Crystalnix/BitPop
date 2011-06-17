// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TAB_CONTENTS_TAB_CONTENTS_CONTAINER_VIEWS_H_
#define CHROME_BROWSER_UI_VIEWS_TAB_CONTENTS_TAB_CONTENTS_CONTAINER_VIEWS_H_
#pragma once

#include "content/common/notification_observer.h"
#include "content/common/notification_registrar.h"
#include "views/view.h"

class NativeTabContentsContainer;
class RenderViewHost;
class RenderWidgetHostView;
class TabContents;

class TabContentsContainer : public views::View,
                             public NotificationObserver {
 public:
  TabContentsContainer();
  virtual ~TabContentsContainer();

  // Changes the TabContents associated with this view.
  void ChangeTabContents(TabContents* contents);

  View* GetFocusView() { return this; }

  // Accessor for |tab_contents_|.
  TabContents* tab_contents() const { return tab_contents_; }

  // Called by the BrowserView to notify that |tab_contents| got the focus.
  void TabContentsFocused(TabContents* tab_contents);

  // Tells the container to update less frequently during resizing operations
  // so performance is better.
  void SetFastResize(bool fast_resize);

  // Updates the current reserved rect in view coordinates where contents
  // should not be rendered to draw the resize corner, sidebar mini tabs etc.
  void SetReservedContentsRect(const gfx::Rect& reserved_rect);

  // Overridden from NotificationObserver:
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details) OVERRIDE;

  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE;

 private:
  // Add or remove observers for events that we care about.
  void AddObservers();
  void RemoveObservers();

  // Called when the RenderViewHost of the hosted TabContents has changed, e.g.
  // to show an interstitial page.
  void RenderViewHostChanged(RenderViewHost* old_host,
                             RenderViewHost* new_host);

  // Called when a TabContents is destroyed. This gives us a chance to clean
  // up our internal state if the TabContents is somehow destroyed before we
  // get notified.
  void TabContentsDestroyed(TabContents* contents);

  // Called when the RenderWidgetHostView of the hosted TabContents has changed.
  void RenderWidgetHostViewChanged(RenderWidgetHostView* new_view);

  // The attached TabContents.
  TabContents* tab_contents_;

  // Handles registering for our notifications.
  NotificationRegistrar registrar_;

  // The current reserved rect in view coordinates where contents should not be
  // rendered to draw the resize corner, sidebar mini tabs etc.
  // Cached here to update ever changing renderers.
  gfx::Rect cached_reserved_rect_;

  DISALLOW_COPY_AND_ASSIGN(TabContentsContainer);
};

#endif  // CHROME_BROWSER_UI_VIEWS_TAB_CONTENTS_TAB_CONTENTS_CONTAINER_VIEWS_H_
