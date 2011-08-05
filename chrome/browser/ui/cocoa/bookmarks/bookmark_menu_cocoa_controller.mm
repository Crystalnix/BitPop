// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/bookmarks/bookmark_menu_cocoa_controller.h"

#include "base/sys_string_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"  // IDC_BOOKMARK_MENU
#import "chrome/browser/app_controller_mac.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/ui/browser.h"
#import "chrome/browser/ui/cocoa/bookmarks/bookmark_menu_bridge.h"
#include "chrome/browser/ui/cocoa/event_utils.h"
#include "content/browser/user_metrics.h"
#include "ui/base/text/text_elider.h"

namespace {

// Menus more than this many pixels wide will get trimmed
// TODO(jrg): ask UI dudes what a good value is.
const NSUInteger kMaximumMenuPixelsWide = 300;

}

@implementation BookmarkMenuCocoaController

+ (NSString*)menuTitleForNode:(const BookmarkNode*)node {
  NSFont* nsfont = [NSFont menuBarFontOfSize:0];  // 0 means "default"
  gfx::Font font(base::SysNSStringToUTF16([nsfont fontName]),
                 static_cast<int>([nsfont pointSize]));
  string16 title = ui::ElideText(node->GetTitle(),
                                 font,
                                 kMaximumMenuPixelsWide,
                                 false);
  return base::SysUTF16ToNSString(title);
}

+ (NSString*)tooltipForNode:(const BookmarkNode*)node {
  NSString* title = base::SysUTF16ToNSString(node->GetTitle());
  std::string url_string = node->GetURL().possibly_invalid_spec();
  NSString* url = [NSString stringWithUTF8String:url_string.c_str()];
  if ([title length] == 0)
    return url;
  else if ([url length] == 0 || [url isEqualToString:title])
    return title;
  else
    return [NSString stringWithFormat:@"%@\n%@", title, url];
}

- (id)initWithBridge:(BookmarkMenuBridge *)bridge
             andMenu:(NSMenu*)menu {
  if ((self = [super init])) {
    bridge_ = bridge;
    DCHECK(bridge_);
    menu_ = menu;
    [[self menu] setDelegate:self];
  }
  return self;
}

- (void)dealloc {
  [[self menu] setDelegate:nil];
  [super dealloc];
}

- (NSMenu*)menu {
  return menu_;
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem {
  AppController* controller = [NSApp delegate];
  return [controller keyWindowIsNotModal];
}

// NSMenu delegate method: called just before menu is displayed.
- (void)menuNeedsUpdate:(NSMenu*)menu {
  bridge_->UpdateMenu(menu);
}

// Return the a BookmarkNode that has the given id (called
// "identifier" here to avoid conflict with objc's concept of "id").
- (const BookmarkNode*)nodeForIdentifier:(int)identifier {
  return bridge_->GetBookmarkModel()->GetNodeByID(identifier);
}

// Open the URL of the given BookmarkNode in the current tab.
- (void)openURLForNode:(const BookmarkNode*)node {
  Browser* browser = Browser::GetTabbedBrowser(bridge_->GetProfile(), true);
  if (!browser)
    browser = Browser::Create(bridge_->GetProfile());
  WindowOpenDisposition disposition =
      event_utils::WindowOpenDispositionFromNSEvent([NSApp currentEvent]);
  browser->OpenURL(node->GetURL(), GURL(), disposition,
                   PageTransition::AUTO_BOOKMARK);
}

// Open sites under BookmarkNode with the specified disposition.
- (void)openAll:(NSInteger)tag
    withDisposition:(WindowOpenDisposition)disposition {
  int identifier = tag;

  const BookmarkNode* node = [self nodeForIdentifier:identifier];
  DCHECK(node);

  Browser* browser = Browser::GetTabbedBrowser(bridge_->GetProfile(), true);
  if (!browser)
    browser = Browser::Create(bridge_->GetProfile());
  DCHECK(browser);

  if (!node || !browser)
    return; // shouldn't be reached

  bookmark_utils::OpenAll(NULL, browser->profile(), browser, node,
                          disposition);

  const char* metrics_action = NULL;
  if (disposition == NEW_FOREGROUND_TAB) {
    metrics_action = "OpenAllBookmarks";
  } else if (disposition == NEW_WINDOW) {
    metrics_action = "OpenAllBookmarksNewWindow";
  } else {
    metrics_action = "OpenAllBookmarksIncognitoWindow";
  }
  UserMetrics::RecordAction(UserMetricsAction(metrics_action));
}

- (IBAction)openBookmarkMenuItem:(id)sender {
  NSInteger tag = [sender tag];
  int identifier = tag;
  const BookmarkNode* node = [self nodeForIdentifier:identifier];
  DCHECK(node);
  if (!node)
    return;  // shouldn't be reached

  [self openURLForNode:node];
}

- (IBAction)openAllBookmarks:(id)sender {
  [self openAll:[sender tag] withDisposition:NEW_FOREGROUND_TAB];
}

- (IBAction)openAllBookmarksNewWindow:(id)sender {
  [self openAll:[sender tag] withDisposition:NEW_WINDOW];
}

- (IBAction)openAllBookmarksIncognitoWindow:(id)sender {
  [self openAll:[sender tag] withDisposition:OFF_THE_RECORD];
}

@end  // BookmarkMenuCocoaController

