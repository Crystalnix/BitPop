// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TAB_CONTENTS_TAB_CONTENTS_CONTAINER_H_
#define CHROME_BROWSER_UI_VIEWS_TAB_CONTENTS_TAB_CONTENTS_CONTAINER_H_
#pragma once

#include "chrome/browser/ui/views/tab_contents/native_tab_contents_container.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/views/view.h"

class NativeTabContentsContainer;
class RenderViewHost;

namespace content {
class WebContents;
}

class TabContentsContainer : public views::View,
                             public content::NotificationObserver {
 public:
  TabContentsContainer();
  virtual ~TabContentsContainer();

  // Changes the WebContents associated with this view.
  void ChangeWebContents(content::WebContents* contents);

  View* GetFocusView() { return native_container_->GetView(); }

  // TODO(jam): move web_contents() to header.
  content::WebContents* web_contents();

  // Called by the BrowserView to notify that |contents| got the focus.
  void WebContentsFocused(content::WebContents* contents);

  // Tells the container to update less frequently during resizing operations
  // so performance is better.
  void SetFastResize(bool fast_resize);

  // Overridden from content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Overridden from views::View:
  virtual void Layout() OVERRIDE;
  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE;
#if defined(HAVE_XINPUT2)
  virtual bool OnMousePressed(const views::MouseEvent& event) OVERRIDE;
#endif

 protected:
  // Overridden from views::View:
  virtual void ViewHierarchyChanged(bool is_add, views::View* parent,
                                    views::View* child) OVERRIDE;

 private:
  // Add or remove observers for events that we care about.
  void AddObservers();
  void RemoveObservers();

  // Called when the RenderViewHost of the hosted TabContents has changed, e.g.
  // to show an interstitial page.
  void RenderViewHostChanged(RenderViewHost* old_host,
                             RenderViewHost* new_host);

  // Called when a WebContents is destroyed. This gives us a chance to clean
  // up our internal state if the TabContents is somehow destroyed before we
  // get notified.
  void TabContentsDestroyed(content::WebContents* contents);

  // An instance of a NativeTabContentsContainer object that holds the native
  // view handle associated with the attached TabContents.
  NativeTabContentsContainer* native_container_;

  // The attached WebContents.
  content::WebContents* web_contents_;

  // Handles registering for our notifications.
  content::NotificationRegistrar registrar_;

  // The current reserved rect in view coordinates where contents should not be
  // rendered to draw the resize corner, etc.
  // Cached here to update ever changing renderers.
  gfx::Rect cached_reserved_rect_;

  DISALLOW_COPY_AND_ASSIGN(TabContentsContainer);
};

#endif  // CHROME_BROWSER_UI_VIEWS_TAB_CONTENTS_TAB_CONTENTS_CONTAINER_H_
