// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "chrome/test/testing_profile.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/browser/tab_contents/tab_contents_delegate.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class MockTabContentsDelegate : public TabContentsDelegate {
 public:
  virtual ~MockTabContentsDelegate() {}

  virtual void OpenURLFromTab(TabContents* source,
                              const GURL& url, const GURL& referrer,
                              WindowOpenDisposition disposition,
                              PageTransition::Type transition) {}

  virtual void NavigationStateChanged(const TabContents* source,
                                      unsigned changed_flags) {}

  virtual std::string GetNavigationHeaders(const GURL& url) {
    return "";
  }

  virtual void AddNewContents(TabContents* source,
                              TabContents* new_contents,
                              WindowOpenDisposition disposition,
                              const gfx::Rect& initial_pos,
                              bool user_gesture) {}

  virtual void ActivateContents(TabContents* contents) {}

  virtual void DeactivateContents(TabContents* contents) {}

  virtual void LoadingStateChanged(TabContents* source) {}

  virtual void LoadProgressChanged(double progress) {}

  virtual void CloseContents(TabContents* source) {}

  virtual void MoveContents(TabContents* source, const gfx::Rect& pos) {}

  virtual void UpdateTargetURL(TabContents* source, const GURL& url) {}
};

TEST(TabContentsDelegateTest, UnregisterInDestructor) {
  MessageLoop loop(MessageLoop::TYPE_UI);
  scoped_ptr<MockTabContentsDelegate> delegate(new MockTabContentsDelegate());
  scoped_ptr<Profile> profile(new TestingProfile());
  scoped_ptr<TabContents> contents_a(
      new TabContents(profile.get(), NULL, 0, NULL, NULL));
  scoped_ptr<TabContents> contents_b(
      new TabContents(profile.get(), NULL, 0, NULL, NULL));
  EXPECT_TRUE(contents_a->delegate() == NULL);
  EXPECT_TRUE(contents_b->delegate() == NULL);

  // Setting a delegate should work correctly.
  contents_a->set_delegate(delegate.get());
  EXPECT_EQ(delegate.get(), contents_a->delegate());
  EXPECT_TRUE(contents_b->delegate() == NULL);

  // A delegate can be a delegate to multiple TabContents.
  contents_b->set_delegate(delegate.get());
  EXPECT_EQ(delegate.get(), contents_a->delegate());
  EXPECT_EQ(delegate.get(), contents_b->delegate());

  // Setting the same delegate multiple times should work correctly.
  contents_b->set_delegate(delegate.get());
  EXPECT_EQ(delegate.get(), contents_a->delegate());
  EXPECT_EQ(delegate.get(), contents_b->delegate());

  // Setting delegate to NULL should work correctly.
  contents_b->set_delegate(NULL);
  EXPECT_EQ(delegate.get(), contents_a->delegate());
  EXPECT_TRUE(contents_b->delegate() == NULL);

  // Destroying the delegate while it is still the delegate
  // for a TabContents should unregister it.
  contents_b->set_delegate(delegate.get());
  EXPECT_EQ(delegate.get(), contents_a->delegate());
  EXPECT_EQ(delegate.get(), contents_b->delegate());
  delegate.reset(NULL);
  EXPECT_TRUE(contents_a->delegate() == NULL);
  EXPECT_TRUE(contents_b->delegate() == NULL);

  // Destroy the tab contents and run the message loop to prevent leaks.
  contents_a.reset(NULL);
  contents_b.reset(NULL);
  loop.RunAllPending();
}

}  // namespace
