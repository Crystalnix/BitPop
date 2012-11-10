// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/page_info_helper.h"

#include "chrome/browser/ui/omnibox/omnibox_view.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "ui/views/view.h"

using content::NavigationController;
using content::NavigationEntry;
using content::WebContents;

PageInfoHelper::PageInfoHelper(const views::View* owner,
                               LocationBarView* location_bar)
    : owner_(owner),
      location_bar_(location_bar) {
}

void PageInfoHelper::ProcessEvent(const views::LocatedEvent& event) {
  if (!owner_->HitTest(event.location()))
    return;

  // Do not show page info if the user has been editing the location
  // bar, or the location bar is at the NTP.
  if (location_bar_->GetLocationEntry()->IsEditingOrEmpty())
    return;

  WebContents* tab = location_bar_->GetTabContents()->web_contents();
  const NavigationController& controller = tab->GetController();
  NavigationEntry* nav_entry = controller.GetActiveEntry();
  if (!nav_entry) {
    NOTREACHED();
    return;
  }

  location_bar_->delegate()->ShowPageInfo(
      tab, nav_entry->GetURL(), nav_entry->GetSSL(), true);
}
