// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/click_handler.h"

#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "ui/views/view.h"

using content::NavigationEntry;
using content::WebContents;

ClickHandler::ClickHandler(const views::View* owner,
                           LocationBarView* location_bar)
    : owner_(owner),
      location_bar_(location_bar) {
}

void ClickHandler::OnMouseReleased(const views::MouseEvent& event) {
  if (!owner_->HitTest(event.location()))
    return;

  // Do not show page info if the user has been editing the location
  // bar, or the location bar is at the NTP.
  if (location_bar_->location_entry()->IsEditingOrEmpty())
    return;

  WebContents* tab = location_bar_->GetTabContentsWrapper()->web_contents();
  NavigationEntry* nav_entry = tab->GetController().GetActiveEntry();
  if (!nav_entry) {
    NOTREACHED();
    return;
  }
  tab->ShowPageInfo(nav_entry->GetURL(), nav_entry->GetSSL(), true);
}
