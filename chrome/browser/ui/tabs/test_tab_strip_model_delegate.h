// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TABS_TEST_TAB_STRIP_MODEL_DELEGATE_H_
#define CHROME_BROWSER_UI_TABS_TEST_TAB_STRIP_MODEL_DELEGATE_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"

// Mock TabStripModelDelegate.
class TestTabStripModelDelegate : public TabStripModelDelegate {
 public:
  TestTabStripModelDelegate();
  virtual ~TestTabStripModelDelegate();

  // Overridden from TabStripModelDelegate:
  virtual TabContents* AddBlankTab(bool foreground) OVERRIDE;
  virtual TabContents* AddBlankTabAt(int index,
                                     bool foreground) OVERRIDE;
  virtual Browser* CreateNewStripWithContents(TabContents* contents,
                                              const gfx::Rect& window_bounds,
                                              const DockInfo& dock_info,
                                              bool maximize) OVERRIDE;
  virtual int GetDragActions() const OVERRIDE;
  virtual TabContents* CreateTabContentsForURL(
      const GURL& url,
      const content::Referrer& referrer,
      Profile* profile,
      content::PageTransition transition,
      bool defer_load,
      content::SiteInstance* instance) const OVERRIDE;
  virtual bool CanDuplicateContentsAt(int index) OVERRIDE;
  virtual void DuplicateContentsAt(int index) OVERRIDE;
  virtual void CloseFrameAfterDragSession() OVERRIDE;
  virtual void CreateHistoricalTab(TabContents* contents) OVERRIDE;
  virtual bool RunUnloadListenerBeforeClosing(TabContents* contents) OVERRIDE;
  virtual bool CanRestoreTab() OVERRIDE;
  virtual void RestoreTab() OVERRIDE;
  virtual bool CanBookmarkAllTabs() const OVERRIDE;
  virtual void BookmarkAllTabs() OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(TestTabStripModelDelegate);
};

#endif  // CHROME_BROWSER_UI_TABS_TEST_TAB_STRIP_MODEL_DELEGATE_H_
