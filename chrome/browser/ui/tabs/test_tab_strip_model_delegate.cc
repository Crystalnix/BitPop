// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/test_tab_strip_model_delegate.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/tabs/dock_info.h"
#include "ui/gfx/rect.h"

using content::SiteInstance;

TestTabStripModelDelegate::TestTabStripModelDelegate() {
}

TestTabStripModelDelegate::~TestTabStripModelDelegate() {
}

TabContents* TestTabStripModelDelegate::AddBlankTab(bool foreground) {
  return NULL;
}

TabContents* TestTabStripModelDelegate::AddBlankTabAt(int index,
                                                      bool foreground) {
  return NULL;
}

Browser* TestTabStripModelDelegate::CreateNewStripWithContents(
    TabContents* contents,
    const gfx::Rect& window_bounds,
    const DockInfo& dock_info,
    bool maximize) {
  return NULL;
}

int TestTabStripModelDelegate::GetDragActions() const {
  return 0;
}

TabContents* TestTabStripModelDelegate::CreateTabContentsForURL(
      const GURL& url,
      const content::Referrer& referrer,
      Profile* profile,
      content::PageTransition transition,
      bool defer_load,
      SiteInstance* instance) const {
  return NULL;
}

bool TestTabStripModelDelegate::CanDuplicateContentsAt(int index) {
  return false;
}

void TestTabStripModelDelegate::DuplicateContentsAt(int index) {
}

void TestTabStripModelDelegate::CloseFrameAfterDragSession() {
}

void TestTabStripModelDelegate::CreateHistoricalTab(TabContents* contents) {
}

bool TestTabStripModelDelegate::RunUnloadListenerBeforeClosing(
    TabContents* contents) {
  return true;
}

bool TestTabStripModelDelegate::CanRestoreTab() {
  return false;
}

void TestTabStripModelDelegate::RestoreTab() {
}

bool TestTabStripModelDelegate::CanBookmarkAllTabs() const {
  return true;
}

void TestTabStripModelDelegate::BookmarkAllTabs() {
}
