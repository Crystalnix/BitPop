// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/applescript/browsercrapplication+applescript.h"

#include "base/logging.h"
#import "base/memory/scoped_nsobject.h"
#import "chrome/browser/app_controller_mac.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_list.h"
#import "chrome/browser/ui/cocoa/applescript/bookmark_folder_applescript.h"
#import "chrome/browser/ui/cocoa/applescript/constants_applescript.h"
#import "chrome/browser/ui/cocoa/applescript/error_applescript.h"
#import "chrome/browser/ui/cocoa/applescript/window_applescript.h"

@implementation BrowserCrApplication (AppleScriptAdditions)

- (NSArray*)appleScriptWindows {
  NSMutableArray* appleScriptWindows = [NSMutableArray
      arrayWithCapacity:BrowserList::size()];
  // Iterate through all browsers and check if it closing,
  // if not add it to list.
  for (BrowserList::const_iterator browserIterator = BrowserList::begin();
       browserIterator != BrowserList::end(); ++browserIterator) {
    if ((*browserIterator)->IsAttemptingToCloseBrowser())
      continue;

    scoped_nsobject<WindowAppleScript> window(
        [[WindowAppleScript alloc] initWithBrowser:*browserIterator]);
    [window setContainer:NSApp
                property:AppleScript::kWindowsProperty];
    [appleScriptWindows addObject:window];
  }
  // Windows sorted by their index value, which is obtained by calling
  // orderedIndex: on each window.
  [appleScriptWindows sortUsingSelector:@selector(windowComparator:)];
  return appleScriptWindows;
}

- (void)insertInAppleScriptWindows:(WindowAppleScript*)aWindow {
  // This method gets called when a new window is created so
  // the container and property are set here.
  [aWindow setContainer:self
               property:AppleScript::kWindowsProperty];
}

- (void)insertInAppleScriptWindows:(WindowAppleScript*)aWindow
                           atIndex:(int)index {
  // This method gets called when a new window is created so
  // the container and property are set here.
  [aWindow setContainer:self
               property:AppleScript::kWindowsProperty];
  // Note: AppleScript is 1-based.
  index--;
  [aWindow setOrderedIndex:[NSNumber numberWithInt:index]];
}

- (void)removeFromAppleScriptWindowsAtIndex:(int)index {
  [[[self appleScriptWindows] objectAtIndex:index]
      handlesCloseScriptCommand:nil];
}

- (NSScriptObjectSpecifier*)objectSpecifier {
  return nil;
}

- (BookmarkFolderAppleScript*)otherBookmarks {
  AppController* appDelegate = [NSApp delegate];

  Profile* defaultProfile = [appDelegate defaultProfile];
  if (!defaultProfile) {
    AppleScript::SetError(AppleScript::errGetProfile);
    return nil;
  }

  BookmarkModel* model = defaultProfile->GetBookmarkModel();
  if (!model->IsLoaded()) {
    AppleScript::SetError(AppleScript::errBookmarkModelLoad);
    return nil;
  }

  BookmarkFolderAppleScript* otherBookmarks =
      [[[BookmarkFolderAppleScript alloc]
          initWithBookmarkNode:model->other_node()] autorelease];
  [otherBookmarks setContainer:self
                      property:AppleScript::kBookmarkFoldersProperty];
  return otherBookmarks;
}

- (BookmarkFolderAppleScript*)bookmarksBar {
  AppController* appDelegate = [NSApp delegate];

  Profile* defaultProfile = [appDelegate defaultProfile];
  if (!defaultProfile) {
    AppleScript::SetError(AppleScript::errGetProfile);
    return nil;
  }

  BookmarkModel* model = defaultProfile->GetBookmarkModel();
  if (!model->IsLoaded()) {
    AppleScript::SetError(AppleScript::errBookmarkModelLoad);
    return NULL;
  }

  BookmarkFolderAppleScript* bookmarksBar =
      [[[BookmarkFolderAppleScript alloc]
          initWithBookmarkNode:model->GetBookmarkBarNode()] autorelease];
  [bookmarksBar setContainer:self
                    property:AppleScript::kBookmarkFoldersProperty];
  return bookmarksBar;
}

- (NSArray*)bookmarkFolders {
  BookmarkFolderAppleScript* otherBookmarks = [self otherBookmarks];
  BookmarkFolderAppleScript* bookmarksBar = [self bookmarksBar];
  NSArray* folderArray = [NSArray arrayWithObjects:otherBookmarks,
                                                   bookmarksBar,
                                                   nil];
  return folderArray;
}

- (void)insertInBookmarksFolders:(id)aBookmarkFolder {
  NOTIMPLEMENTED();
}

- (void)insertInBookmarksFolders:(id)aBookmarkFolder atIndex:(int)index {
  NOTIMPLEMENTED();
}

- (void)removeFromBookmarksFoldersAtIndex:(int)index {
  NOTIMPLEMENTED();
}

@end
