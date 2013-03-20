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
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_system_factory.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/cocoa/extensions/extension_view_mac.h"
#import "chrome/browser/ui/cocoa/view_id_util.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/web_contents.h"

using content::WebContents;
using extensions::Extension;
using extensions::ExtensionSystem;
using extensions::ExtensionSystemFactory;
using extensions::UnloadedExtensionInfo;

namespace {

// Width of the facebook friends sidebar is constant and cannot be manipulated
// by user. When time comes we may change this decision.
const int kFriendsSidebarWidth = 186;

}  // end namespace

@interface FacebookSidebarController (Private)
- (void)showSidebarContents:(WebContents*)sidebarContents;
- (void)initializeExtensionHostWithExtensionLoaded:(BOOL)loaded;
- (void)sizeChanged;
- (void)onViewDidShow;
- (void)invalidateExtensionHost;
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

  virtual void OnExtensionSizeChanged(ExtensionViewMac* view,
                                      const gfx::Size& new_size) OVERRIDE {}

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
          // ---
        }
        break;
      }

      case chrome::NOTIFICATION_EXTENSION_BACKGROUND_PAGE_READY: {
        [controller_ initializeExtensionHostWithExtensionLoaded:NO];
        break;
      }

      case chrome::NOTIFICATION_EXTENSION_LOADED: {
        Extension* extension = content::Details<Extension>(details).ptr();
        if (extension->id() == chrome::kFacebookChatExtensionId) {
          [controller_ initializeExtensionHostWithExtensionLoaded:YES];
        }
        break;
      }

      case chrome::NOTIFICATION_EXTENSION_UNLOADED: {
        UnloadedExtensionInfo* info =
            content::Details<UnloadedExtensionInfo>(details).ptr();
        if (info->extension->id() == chrome::kFacebookChatExtensionId) {
          [controller_ removeAllChildViews];
          [controller_ invalidateExtensionHost];
        }
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
    rc.size.width = kFriendsSidebarWidth;
    [[self view] setFrame:rc];

    view_id_util::SetID(
        [self view],
        VIEW_ID_FACEBOOK_FRIENDS_SIDE_BAR_CONTAINER);

    extension_container_.reset(new SidebarExtensionContainer(self));
    notification_bridge_.reset(new SidebarExtensionNotificationBridge(self));

    [[NSNotificationCenter defaultCenter]
        addObserver:self
        selector:@selector(sizeChanged)
        name:NSViewFrameDidChangeNotification
        object:[self view]
    ];

    [self initializeExtensionHostWithExtensionLoaded:NO];
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
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [super dealloc];
}

- (extensions::ExtensionHost*)extension_host {
  return extension_host_.get();
}

- (CGFloat)maxWidth {
  return kFriendsSidebarWidth;
}

- (void)initializeExtensionHostWithExtensionLoaded:(BOOL)loaded {
  Profile *profile = browser_->profile()->GetOriginalProfile();
  ExtensionService* service = profile->GetExtensionService();
  const Extension* sidebar_extension =
      service->extensions()->GetByID(chrome::kFacebookChatExtensionId);

  if (!sidebar_extension) {
    NOTREACHED() << "Empty extension.";
    return;
  }

  if (!service->IsBackgroundPageReady(sidebar_extension)) {
    registrar_.RemoveAll();
    registrar_.Add(notification_bridge_.get(),
                   chrome::NOTIFICATION_EXTENSION_BACKGROUND_PAGE_READY,
                   content::Source<Extension>(sidebar_extension));
    if (!loaded)
      registrar_.Add(notification_bridge_.get(),
                     chrome::NOTIFICATION_EXTENSION_LOADED,
                     content::Source<Profile>(profile));
    registrar_.Add(notification_bridge_.get(),
                   chrome::NOTIFICATION_EXTENSION_UNLOADED,
                   content::Source<Profile>(profile));
    return;
  }

  std::string url = std::string("chrome-extension://") +
      std::string(chrome::kFacebookChatExtensionId) +
      std::string("/friends_sidebar.html");
  ExtensionProcessManager* manager =
      profile->GetExtensionProcessManager();
  extension_host_.reset(manager->CreateViewHost(GURL(url), browser_,
                                                chrome::VIEW_TYPE_PANEL));
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
  NSMutableArray *viewsToRemove = [[NSMutableArray alloc] init];
  for (NSView* childView in [[self view] subviews])
    [viewsToRemove addObject:childView];
  [viewsToRemove makeObjectsPerformSelector:@selector(removeFromSuperview)];
  [viewsToRemove release];
}

- (void)setVisible:(BOOL)visible {
  sidebarVisible_ = visible;
  [[self view] setHidden:!visible];

  if (!extension_host_.get())
    return;

  gfx::NativeView native_view = extension_host_->view()->native_view();
  [native_view setNeedsDisplay:YES];
  [[self view] setNeedsDisplay:YES];
}

- (void)sizeChanged {
  if (!extension_host_.get())
    return;

  gfx::NativeView native_view = extension_host_->view()->native_view();
  NSRect container_bounds = [[self view] bounds];
  [native_view setFrame:container_bounds];

  [native_view setNeedsDisplay:YES];
  [[self view] setNeedsDisplay:YES];
}

- (void)onViewDidShow {
  [self sizeChanged];
}

- (void)invalidateExtensionHost {
  extension_host_.reset();
}

@end
