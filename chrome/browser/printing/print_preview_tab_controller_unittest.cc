// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/printing/print_preview_tab_controller.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/test/browser_with_test_window_test.h"
#include "chrome/test/testing_profile.h"
#include "content/browser/tab_contents/tab_contents.h"

typedef BrowserWithTestWindowTest PrintPreviewTabControllerTest;

// Create/Get a preview tab for initiator tab.
TEST_F(PrintPreviewTabControllerTest, GetOrCreatePreviewTab) {
  ASSERT_TRUE(browser());
  BrowserList::SetLastActive(browser());
  ASSERT_TRUE(BrowserList::GetLastActive());

  // Lets start with one window with one tab.
  EXPECT_EQ(1u, BrowserList::size());
  EXPECT_EQ(0, browser()->tab_count());
  browser()->NewTab();
  EXPECT_EQ(1, browser()->tab_count());

  // Create a reference to initiator tab contents.
  TabContents* initiator_tab = browser()->GetSelectedTabContents();

  scoped_refptr<printing::PrintPreviewTabController>
      tab_controller(new printing::PrintPreviewTabController());
  ASSERT_TRUE(tab_controller);

  // Get the preview tab for initiator tab.
  TabContents* preview_tab =
      tab_controller->GetOrCreatePreviewTab(initiator_tab);

  // New print preview tab is created. Current focus is on preview tab.
  EXPECT_EQ(2, browser()->tab_count());
  EXPECT_NE(initiator_tab, preview_tab);

  // Activate initiator_tab.
  initiator_tab->Activate();

  // Get the print preview tab for initiator tab.
  TabContents* new_preview_tab =
      tab_controller->GetOrCreatePreviewTab(initiator_tab);

  // Preview tab already exists. Tab count remains the same.
  EXPECT_EQ(2, browser()->tab_count());

  // 1:1 relationship between initiator and preview tab.
  EXPECT_EQ(new_preview_tab, preview_tab);
}

// To show multiple print preview tabs exist in the same browser for
// different initiator tabs. If preview tab already exists for an initiator, it
// gets focused.
TEST_F(PrintPreviewTabControllerTest, MultiplePreviewTabs) {
  ASSERT_TRUE(browser());
  BrowserList::SetLastActive(browser());
  ASSERT_TRUE(BrowserList::GetLastActive());

  // Lets start with one window and two tabs.
  EXPECT_EQ(1u, BrowserList::size());
  EXPECT_EQ(0, browser()->tab_count());

  browser()->NewTab();
  TabContents* tab_contents_1 = browser()->GetSelectedTabContents();
  ASSERT_TRUE(tab_contents_1);

  browser()->NewTab();
  TabContents* tab_contents_2 = browser()->GetSelectedTabContents();
  ASSERT_TRUE(tab_contents_2);
  EXPECT_EQ(2, browser()->tab_count());

  scoped_refptr<printing::PrintPreviewTabController>
      tab_controller(new printing::PrintPreviewTabController());
  ASSERT_TRUE(tab_controller);

  // Create preview tab for |tab_contents_1|
  TabContents* preview_tab_1 =
      tab_controller->GetOrCreatePreviewTab(tab_contents_1);

  EXPECT_NE(tab_contents_1, preview_tab_1);
  EXPECT_EQ(3, browser()->tab_count());

  // Create preview tab for |tab_contents_2|
  TabContents* preview_tab_2 =
      tab_controller->GetOrCreatePreviewTab(tab_contents_2);

  EXPECT_NE(tab_contents_2, preview_tab_2);
  // 2 initiator tab and 2 preview tabs exist in the same browser.
  EXPECT_EQ(4, browser()->tab_count());

  TabStripModel* model = browser()->tabstrip_model();
  ASSERT_TRUE(model);

  int preview_tab_1_index = model->GetWrapperIndex(preview_tab_1);
  int preview_tab_2_index = model->GetWrapperIndex(preview_tab_2);

  EXPECT_NE(-1, preview_tab_1_index);
  EXPECT_NE(-1, preview_tab_2_index);
  // Current tab is |preview_tab_2|.
  EXPECT_EQ(preview_tab_2_index, browser()->active_index());

  // Activate |tab_contents_1| tab.
  tab_contents_1->Activate();

  // When we get the preview tab for |tab_contents_1|,
  // |preview_tab_1| is activated and focused.
  tab_controller->GetOrCreatePreviewTab(tab_contents_1);
  EXPECT_EQ(preview_tab_1_index, browser()->active_index());
}
