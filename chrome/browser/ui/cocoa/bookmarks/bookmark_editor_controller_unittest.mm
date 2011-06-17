// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include "base/string16.h"
#include "base/sys_string_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#import "chrome/browser/ui/cocoa/bookmarks/bookmark_editor_controller.h"
#include "chrome/browser/ui/cocoa/browser_test_helper.h"
#import "chrome/browser/ui/cocoa/cocoa_test_helper.h"
#include "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"
#include "testing/platform_test.h"

class BookmarkEditorControllerTest : public CocoaTest {
 public:
  BrowserTestHelper browser_helper_;
  const BookmarkNode* default_node_;
  const BookmarkNode* default_parent_;
  const char* default_name_;
  string16 default_title_;
  BookmarkEditorController* controller_;

  virtual void SetUp() {
    CocoaTest::SetUp();
    BookmarkModel* model = browser_helper_.profile()->GetBookmarkModel();
    default_parent_ = model->GetBookmarkBarNode();
    default_name_ = "http://www.zim-bop-a-dee.com/";
    default_title_ = ASCIIToUTF16("ooh title");
    const BookmarkNode* default_node = model->AddURL(default_parent_, 0,
                                                     default_title_,
                                                     GURL(default_name_));
    controller_ = [[BookmarkEditorController alloc]
                   initWithParentWindow:test_window()
                                profile:browser_helper_.profile()
                                 parent:default_parent_
                                   node:default_node
                          configuration:BookmarkEditor::NO_TREE];
    [controller_ runAsModalSheet];
  }

  virtual void TearDown() {
    controller_ = NULL;
    CocoaTest::TearDown();
  }
};

TEST_F(BookmarkEditorControllerTest, NoEdit) {
  [controller_ cancel:nil];
  ASSERT_EQ(default_parent_->child_count(), 1);
  const BookmarkNode* child = default_parent_->GetChild(0);
  EXPECT_EQ(child->GetTitle(), default_title_);
  EXPECT_EQ(child->GetURL(), GURL(default_name_));
}

TEST_F(BookmarkEditorControllerTest, EditTitle) {
  [controller_ setDisplayName:@"whamma jamma bamma"];
  [controller_ ok:nil];
  ASSERT_EQ(default_parent_->child_count(), 1);
  const BookmarkNode* child = default_parent_->GetChild(0);
  EXPECT_EQ(child->GetTitle(), ASCIIToUTF16("whamma jamma bamma"));
  EXPECT_EQ(child->GetURL(), GURL(default_name_));
}

TEST_F(BookmarkEditorControllerTest, EditURL) {
  EXPECT_TRUE([controller_ okButtonEnabled]);
  [controller_ setDisplayURL:@"http://yellow-sneakers.com/"];
  EXPECT_TRUE([controller_ okButtonEnabled]);
  [controller_ ok:nil];
  ASSERT_EQ(default_parent_->child_count(), 1);
  const BookmarkNode* child = default_parent_->GetChild(0);
  EXPECT_EQ(child->GetTitle(), default_title_);
  EXPECT_EQ(child->GetURL(), GURL("http://yellow-sneakers.com/"));
}

TEST_F(BookmarkEditorControllerTest, EditAndFixPrefix) {
  [controller_ setDisplayURL:@"x"];
  [controller_ ok:nil];
  ASSERT_EQ(default_parent_->child_count(), 1);
  const BookmarkNode* child = default_parent_->GetChild(0);
  EXPECT_TRUE(child->GetURL().is_valid());
}

TEST_F(BookmarkEditorControllerTest, NodeDeleted) {
  // Delete the bookmark being edited and verify the sheet cancels itself:
  ASSERT_TRUE([test_window() attachedSheet]);
  BookmarkModel* model = browser_helper_.profile()->GetBookmarkModel();
  model->Remove(default_parent_, 0);
  ASSERT_FALSE([test_window() attachedSheet]);
}

TEST_F(BookmarkEditorControllerTest, EditAndConfirmOKButton) {
  // Confirm OK button enabled/disabled as appropriate:
  // First test the URL.
  EXPECT_TRUE([controller_ okButtonEnabled]);
  [controller_ setDisplayURL:@""];
  EXPECT_FALSE([controller_ okButtonEnabled]);
  [controller_ setDisplayURL:@"http://www.cnn.com"];
  EXPECT_TRUE([controller_ okButtonEnabled]);
  // Then test the name.
  [controller_ setDisplayName:@""];
  EXPECT_TRUE([controller_ okButtonEnabled]);
  [controller_ setDisplayName:@"                   "];
  EXPECT_TRUE([controller_ okButtonEnabled]);
  // Then little mix of both.
  [controller_ setDisplayName:@"name"];
  EXPECT_TRUE([controller_ okButtonEnabled]);
  [controller_ setDisplayURL:@""];
  EXPECT_FALSE([controller_ okButtonEnabled]);
  [controller_ cancel:nil];
}

TEST_F(BookmarkEditorControllerTest, GoodAndBadURLsChangeColor) {
  // Confirm that the background color of the URL edit field changes
  // based on whether it contains a valid or invalid URL.
  [controller_ setDisplayURL:@"http://www.cnn.com"];
  NSColor *urlColorA = [controller_ urlFieldColor];
  EXPECT_TRUE(urlColorA);
  [controller_ setDisplayURL:@""];
  NSColor *urlColorB = [controller_ urlFieldColor];
  EXPECT_TRUE(urlColorB);
  EXPECT_NSNE(urlColorA, urlColorB);
  [controller_ setDisplayURL:@"http://www.google.com"];
  [controller_ cancel:nil];
  urlColorB = [controller_ urlFieldColor];
  EXPECT_TRUE(urlColorB);
  EXPECT_NSEQ(urlColorA, urlColorB);
}

class BookmarkEditorControllerNoNodeTest : public CocoaTest {
 public:
  BrowserTestHelper browser_helper_;
  BookmarkEditorController* controller_;

  virtual void SetUp() {
    CocoaTest::SetUp();
    BookmarkModel* model = browser_helper_.profile()->GetBookmarkModel();
    const BookmarkNode* parent = model->GetBookmarkBarNode();
    controller_ = [[BookmarkEditorController alloc]
                   initWithParentWindow:test_window()
                                profile:browser_helper_.profile()
                                 parent:parent
                                   node:NULL
                          configuration:BookmarkEditor::NO_TREE];

    [controller_ runAsModalSheet];
  }

  virtual void TearDown() {
    controller_ = NULL;
    CocoaTest::TearDown();
  }
};

TEST_F(BookmarkEditorControllerNoNodeTest, NoNodeNoTree) {
  EXPECT_EQ(@"", [controller_ displayName]);
  EXPECT_EQ(@"", [controller_ displayURL]);
  EXPECT_FALSE([controller_ okButtonEnabled]);
  [controller_ cancel:nil];
}

class BookmarkEditorControllerYesNodeTest : public CocoaTest {
 public:
  BrowserTestHelper browser_helper_;
  string16 default_title_;
  const char* url_name_;
  BookmarkEditorController* controller_;

  virtual void SetUp() {
    CocoaTest::SetUp();
    BookmarkModel* model = browser_helper_.profile()->GetBookmarkModel();
    const BookmarkNode* parent = model->GetBookmarkBarNode();
    default_title_ = ASCIIToUTF16("wooh title");
    url_name_ = "http://www.zoom-baby-doo-da.com/";
    const BookmarkNode* node = model->AddURL(parent, 0, default_title_,
                                             GURL(url_name_));
    controller_ = [[BookmarkEditorController alloc]
                   initWithParentWindow:test_window()
                                profile:browser_helper_.profile()
                                 parent:parent
                                   node:node
                          configuration:BookmarkEditor::NO_TREE];

    [controller_ runAsModalSheet];
  }

  virtual void TearDown() {
    controller_ = NULL;
    CocoaTest::TearDown();
  }
};

TEST_F(BookmarkEditorControllerYesNodeTest, YesNodeShowTree) {
  EXPECT_NSEQ(base::SysUTF16ToNSString(default_title_),
              [controller_ displayName]);
  EXPECT_NSEQ([NSString stringWithCString:url_name_
                                 encoding:NSUTF8StringEncoding],
              [controller_ displayURL]);
  [controller_ cancel:nil];
}

class BookmarkEditorControllerTreeTest : public CocoaTest {

 public:
  BrowserTestHelper browser_helper_;
  BookmarkEditorController* controller_;
  const BookmarkNode* folder_a_;
  const BookmarkNode* folder_b_;
  const BookmarkNode* folder_bb_;
  const BookmarkNode* folder_c_;
  const BookmarkNode* bookmark_bb_3_;
  GURL bb3_url_1_;
  GURL bb3_url_2_;

  BookmarkEditorControllerTreeTest() {
    // Set up a small bookmark hierarchy, which will look as follows:
    //    a      b      c    d
    //     a-0    b-0    c-0
    //     a-1     bb-0  c-1
    //     a-2     bb-1  c-2
    //             bb-2
    //             bb-3
    //             bb-4
    //            b-1
    //            b-2
    BookmarkModel& model(*(browser_helper_.profile()->GetBookmarkModel()));
    const BookmarkNode* root = model.GetBookmarkBarNode();
    folder_a_ = model.AddFolder(root, 0, ASCIIToUTF16("a"));
    model.AddURL(folder_a_, 0, ASCIIToUTF16("a-0"), GURL("http://a-0.com"));
    model.AddURL(folder_a_, 1, ASCIIToUTF16("a-1"), GURL("http://a-1.com"));
    model.AddURL(folder_a_, 2, ASCIIToUTF16("a-2"), GURL("http://a-2.com"));

    folder_b_ = model.AddFolder(root, 1, ASCIIToUTF16("b"));
    model.AddURL(folder_b_, 0, ASCIIToUTF16("b-0"), GURL("http://b-0.com"));
    folder_bb_ = model.AddFolder(folder_b_, 1, ASCIIToUTF16("bb"));
    model.AddURL(folder_bb_, 0, ASCIIToUTF16("bb-0"), GURL("http://bb-0.com"));
    model.AddURL(folder_bb_, 1, ASCIIToUTF16("bb-1"), GURL("http://bb-1.com"));
    model.AddURL(folder_bb_, 2, ASCIIToUTF16("bb-2"), GURL("http://bb-2.com"));

    // To find it later, this bookmark name must always have a URL
    // of http://bb-3.com or https://bb-3.com
    bb3_url_1_ = GURL("http://bb-3.com");
    bb3_url_2_ = GURL("https://bb-3.com");
    bookmark_bb_3_ = model.AddURL(folder_bb_, 3, ASCIIToUTF16("bb-3"),
                                  bb3_url_1_);

    model.AddURL(folder_bb_, 4, ASCIIToUTF16("bb-4"), GURL("http://bb-4.com"));
    model.AddURL(folder_b_, 2, ASCIIToUTF16("b-1"), GURL("http://b-2.com"));
    model.AddURL(folder_b_, 3, ASCIIToUTF16("b-2"), GURL("http://b-3.com"));

    folder_c_ = model.AddFolder(root, 2, ASCIIToUTF16("c"));
    model.AddURL(folder_c_, 0, ASCIIToUTF16("c-0"), GURL("http://c-0.com"));
    model.AddURL(folder_c_, 1, ASCIIToUTF16("c-1"), GURL("http://c-1.com"));
    model.AddURL(folder_c_, 2, ASCIIToUTF16("c-2"), GURL("http://c-2.com"));
    model.AddURL(folder_c_, 3, ASCIIToUTF16("c-3"), GURL("http://c-3.com"));

    model.AddURL(root, 3, ASCIIToUTF16("d"), GURL("http://d-0.com"));
  }

  virtual BookmarkEditorController* CreateController() {
    return [[BookmarkEditorController alloc]
               initWithParentWindow:test_window()
                            profile:browser_helper_.profile()
                             parent:folder_bb_
                               node:bookmark_bb_3_
                      configuration:BookmarkEditor::SHOW_TREE];
  }

  virtual void SetUp() {
    controller_ = CreateController();
    [controller_ runAsModalSheet];
  }

  virtual void TearDown() {
    controller_ = NULL;
    CocoaTest::TearDown();
  }

  // After changing a node, pointers to the node may be invalid.  This
  // is because the node itself may not be updated; it may removed and
  // a new one is added in that location.  (Implementation detail of
  // BookmarkEditorController).  This method updates the class's
  // bookmark_bb_3_ so that it points to the new node for testing.
  void UpdateBB3() {
    std::vector<const BookmarkNode*> nodes;
    BookmarkModel* model = browser_helper_.profile()->GetBookmarkModel();
    model->GetNodesByURL(bb3_url_1_, &nodes);
    if (nodes.empty())
      model->GetNodesByURL(bb3_url_2_, &nodes);
    DCHECK(nodes.size());
    bookmark_bb_3_ = nodes[0];
  }

};

TEST_F(BookmarkEditorControllerTreeTest, VerifyBookmarkTestModel) {
  BookmarkModel& model(*(browser_helper_.profile()->GetBookmarkModel()));
  model.root_node();
  const BookmarkNode& root(*model.GetBookmarkBarNode());
  EXPECT_EQ(4, root.child_count());
  const BookmarkNode* child = root.GetChild(0);
  EXPECT_EQ(3, child->child_count());
  const BookmarkNode* subchild = child->GetChild(0);
  EXPECT_EQ(0, subchild->child_count());
  subchild = child->GetChild(1);
  EXPECT_EQ(0, subchild->child_count());
  subchild = child->GetChild(2);
  EXPECT_EQ(0, subchild->child_count());

  child = root.GetChild(1);
  EXPECT_EQ(4, child->child_count());
  subchild = child->GetChild(0);
  EXPECT_EQ(0, subchild->child_count());
  subchild = child->GetChild(1);
  EXPECT_EQ(5, subchild->child_count());
  const BookmarkNode* subsubchild = subchild->GetChild(0);
  EXPECT_EQ(0, subsubchild->child_count());
  subsubchild = subchild->GetChild(1);
  EXPECT_EQ(0, subsubchild->child_count());
  subsubchild = subchild->GetChild(2);
  EXPECT_EQ(0, subsubchild->child_count());
  subsubchild = subchild->GetChild(3);
  EXPECT_EQ(0, subsubchild->child_count());
  subsubchild = subchild->GetChild(4);
  EXPECT_EQ(0, subsubchild->child_count());
  subchild = child->GetChild(2);
  EXPECT_EQ(0, subchild->child_count());
  subchild = child->GetChild(3);
  EXPECT_EQ(0, subchild->child_count());

  child = root.GetChild(2);
  EXPECT_EQ(4, child->child_count());
  subchild = child->GetChild(0);
  EXPECT_EQ(0, subchild->child_count());
  subchild = child->GetChild(1);
  EXPECT_EQ(0, subchild->child_count());
  subchild = child->GetChild(2);
  EXPECT_EQ(0, subchild->child_count());
  subchild = child->GetChild(3);
  EXPECT_EQ(0, subchild->child_count());

  child = root.GetChild(3);
  EXPECT_EQ(0, child->child_count());
  [controller_ cancel:nil];
}

TEST_F(BookmarkEditorControllerTreeTest, RenameBookmarkInPlace) {
  const BookmarkNode* oldParent = bookmark_bb_3_->parent();
  [controller_ setDisplayName:@"NEW NAME"];
  [controller_ ok:nil];
  UpdateBB3();
  const BookmarkNode* newParent = bookmark_bb_3_->parent();
  ASSERT_EQ(newParent, oldParent);
  int childIndex = newParent->GetIndexOf(bookmark_bb_3_);
  ASSERT_EQ(3, childIndex);
}

TEST_F(BookmarkEditorControllerTreeTest, ChangeBookmarkURLInPlace) {
  const BookmarkNode* oldParent = bookmark_bb_3_->parent();
  [controller_ setDisplayURL:@"https://bb-3.com"];
  [controller_ ok:nil];
  UpdateBB3();
  const BookmarkNode* newParent = bookmark_bb_3_->parent();
  ASSERT_EQ(newParent, oldParent);
  int childIndex = newParent->GetIndexOf(bookmark_bb_3_);
  ASSERT_EQ(3, childIndex);
}

TEST_F(BookmarkEditorControllerTreeTest, ChangeBookmarkFolder) {
  [controller_ selectTestNodeInBrowser:folder_c_];
  [controller_ ok:nil];
  UpdateBB3();
  const BookmarkNode* parent = bookmark_bb_3_->parent();
  ASSERT_EQ(parent, folder_c_);
  int childIndex = parent->GetIndexOf(bookmark_bb_3_);
  ASSERT_EQ(4, childIndex);
}

TEST_F(BookmarkEditorControllerTreeTest, ChangeNameAndBookmarkFolder) {
  [controller_ setDisplayName:@"NEW NAME"];
  [controller_ selectTestNodeInBrowser:folder_c_];
  [controller_ ok:nil];
  UpdateBB3();
  const BookmarkNode* parent = bookmark_bb_3_->parent();
  ASSERT_EQ(parent, folder_c_);
  int childIndex = parent->GetIndexOf(bookmark_bb_3_);
  ASSERT_EQ(4, childIndex);
  EXPECT_EQ(bookmark_bb_3_->GetTitle(), ASCIIToUTF16("NEW NAME"));
}

TEST_F(BookmarkEditorControllerTreeTest, AddFolderWithFolderSelected) {
  // Folders are NOT added unless the OK button is pressed.
  [controller_ newFolder:nil];
  [controller_ cancel:nil];
  EXPECT_EQ(5, folder_bb_->child_count());
}

class BookmarkEditorControllerTreeNoNodeTest :
    public BookmarkEditorControllerTreeTest {
 public:
  virtual BookmarkEditorController* CreateController() {
    return [[BookmarkEditorController alloc]
               initWithParentWindow:test_window()
                            profile:browser_helper_.profile()
                             parent:folder_bb_
                               node:nil
                      configuration:BookmarkEditor::SHOW_TREE];
  }

};

TEST_F(BookmarkEditorControllerTreeNoNodeTest, NewBookmarkNoNode) {
  [controller_ setDisplayName:@"NEW BOOKMARK"];
  [controller_ setDisplayURL:@"http://NEWURL.com"];
  [controller_ ok:nil];
  const BookmarkNode* new_node = folder_bb_->GetChild(5);
  ASSERT_EQ(0, new_node->child_count());
  EXPECT_EQ(new_node->GetTitle(), ASCIIToUTF16("NEW BOOKMARK"));
  EXPECT_EQ(new_node->GetURL(), GURL("http://NEWURL.com"));
}
