// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_sidebar_controller.h"

#include <Cocoa/Cocoa.h>

#include "base/logging.h"
#include "base/memory/scoped_nsobject.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/cocoa/extensions/extension_view_mac.h"
#import "chrome/browser/ui/cocoa/view_id_util.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/web_contents.h"

using content::WebContents;

namespace {

// Width of the facebook friends sidebar is constant and cannot be manipulated
// by user. When time comes we may change this decision.
const int kFriendsSidebarWidth = 186;

}  // end namespace

@interface FacebookSidebarController (Private)
//- (void)resizeSidebarToNewWidth:(CGFloat)width;
- (void)showSidebarContents:(WebContents*)sidebarContents;
- (void)initializeExtensionHost;
- (void)sizeChanged:(NSNotification*)notification;
- (void)onViewDidShow;
@end

@interface BackgroundSidebarView : NSView {}
@end

@implementation BackgroundSidebarView

- (void)drawRect:(NSRect)dirtyRect {
    // set any NSColor for filling, say white:
    [[NSColor grayColor] setFill];
    NSRectFill(dirtyRect);
}

@end

// NOTE: this class does nothing for now
class SidebarExtensionContainer : public ExtensionViewMac::Container {
 public:
  explicit SidebarExtensionContainer(FacebookSidebarController* controller)
       : controller_(controller) {
  }

  virtual void OnExtensionSizeChanged(
      ExtensionViewMac* view,
      const gfx::Size& new_size) OVERRIDE {
   [controller_ onSizeChanged:nil];
 }

  virtual void OnExtensionViewDidShow(ExtensionViewMac* view) OVERRIDE {
    [controller_ onViewDidShow];
  }

 private:
  FacebookSidebarController* controller_; // Weak; owns this.
};

class SidebarExtensionNotificationBridge : public content::NotificationObserver {
 public:
  explicit SidebarExtensionNotificationBridge(FacebookSidebarController* controller)
    : controller_(controller) {}

  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) {
    switch (type) {
      case chrome::NOTIFICATION_EXTENSION_HOST_DID_STOP_LOADING: {
        if (content::Details<extensions::ExtensionHost>(
                [controller_ extension_host]) == details) {
          //[controller_ showDevTools];
          // NOTE: do nothing here
        }
        break;
      }
      case chrome::NOTIFICATION_EXTENSIONS_READY: {
        [controller_ removeAllChildViews];
        [controller_ initializeExtensionHost];
        break;
      }

      case chrome::NOTIFICATION_EXTENSION_HOST_VIEW_SHOULD_CLOSE: {
        if (content::Details<extensions::ExtensionHost>([controller_ extension_host]) == details) {
          [controller_ removeAllChildViews];
        }

        break;
      }

      default: {
        NOTREACHED() << "Received unexpected notification";
        break;
      }
    };
  }

 private:
  FacebookSidebarController* controller_;
};

@implementation FacebookSidebarController

@synthesize visible = sidebarVisible_;

- (id)initWithBrowser:(Browser*)browser {
  if ((self = [super initWithNibName:nil bundle:nil])) {
    browser_ = browser;
    sidebarVisible_ = NO;
    NSRect rc = [self view].frame;
    rc.size.width = 0;
    [[self view] setFrame:rc];

    view_id_util::SetID(
        [self view],
        VIEW_ID_FACEBOOK_FRIENDS_SIDE_BAR_CONTAINER);

    extension_container_.reset(new SidebarExtensionContainer(self));
    notification_bridge_.reset(new SidebarExtensionNotificationBridge(self));

    [[NSNotificationCenter defaultCenter]
        addObserver:self
        selector:@selector(sizeChanged:)
        name:NSViewFrameDidChangeNotification
        object:[self view]
    ];

    [self initializeExtensionHost];
  }
  return self;
}

- (void)loadView {
  scoped_nsobject<NSView> view([[BackgroundSidebarView alloc] initWithFrame:NSZeroRect]);
  [view setAutoresizingMask:NSViewMinXMargin | NSViewHeightSizable];
  [view setAutoresizesSubviews:NO];
  [view setPostsFrameChangedNotifications:YES];

  [self setView:view];
}

- (void)dealloc {
  [super dealloc];
}

- (extensions::ExtensionHost*)extension_host {
  return extension_host_.get();
}

- (CGFloat)maxWidth {
  return kFriendsSidebarWidth;
}

- (void)initializeExtensionHost {
  std::string url = std::string("chrome-extension://") +
      std::string(chrome::kFacebookChatExtensionId) +
      std::string("/friends_sidebar.html");
  ExtensionProcessManager* manager =
      browser_->profile()->GetExtensionProcessManager();
  extension_host_.reset(manager->CreateViewHost(GURL(url), browser_,
                                                chrome::VIEW_TYPE_PANEL));

  registrar_.RemoveAll();
  registrar_.Add(notification_bridge_.get(),
                  chrome::NOTIFICATION_EXTENSIONS_READY,
                  content::Source<Profile>(
                      browser_->profile()->GetOriginalProfile()));

  if (extension_host_.get()) {
    gfx::NativeView native_view = extension_host_->view()->native_view();
    NSRect container_bounds = [[self view] bounds];
    [native_view setFrame:container_bounds];
    [[self view] addSubview:native_view];
    extension_host_->view()->set_container(extension_container_.get());

    [native_view setNeedsDisplay:YES];

    // Wait to show the popup until the contained host finishes loading.
    registrar_.Add(notification_bridge_.get(),
                   chrome::NOTIFICATION_EXTENSION_HOST_DID_STOP_LOADING,
                   content::Source<extensions::ExtensionHost>(
                      extension_host_.get()));

    // Listen for the containing view calling window.close();
    registrar_.Add(notification_bridge_.get(),
                   chrome::NOTIFICATION_EXTENSION_HOST_VIEW_SHOULD_CLOSE,
                   content::Source<Profile>(extension_host_->profile()));
  }
}

- (void)removeAllChildViews {
  for (NSView* view in [[self view] subviews])
    [view removeFromSuperview];
}

- (void)setVisible:(BOOL)visible {
   sidebarVisible_ = visible;
   //[[self view] setHidden:!visible];

   NSRect frame = [self view].frame;
   frame.size.width = (visible ? [self maxWidth] : 0);
   [self view].frame = frame;

   [self sizeChanged:nil];
}

- (void)sizeChanged:(NSNotification*)notification {
  gfx::NativeView native_view = extension_host_->view()->native_view();
  NSRect container_bounds = [[self view] bounds];
  [native_view setFrame:container_bounds];

  [native_view setNeedsDisplay:YES];
}

- (void)onViewDidShow {
  [self sizeChanged:nil];
}

@end
