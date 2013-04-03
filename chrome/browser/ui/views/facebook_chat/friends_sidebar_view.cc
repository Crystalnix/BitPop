#include "chrome/browser/ui/views/facebook_chat/friends_sidebar_view.h"

#include "base/logging.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "ui/gfx/canvas.h"
#include "ui/views/layout/fill_layout.h"

using extensions::Extension;
using content::WebContents;

// Sidebar width
static const int kFriendsSidebarWidth = 185;

FriendsSidebarView::FriendsSidebarView(Browser* browser, BrowserView *parent) :
  views::View(),
  browser_(browser),
  parent_(parent) {
    set_id(VIEW_ID_FACEBOOK_FRIENDS_SIDE_BAR_CONTAINER);
    parent->AddChildView(this);
    SetLayoutManager(new views::FillLayout());
    set_background(views::Background::CreateSolidBackground(0xe8, 0xe8, 0xe8, 0xff));
    this->InitializeExtensionHost();

    Init();
}

FriendsSidebarView::~FriendsSidebarView() {
  parent_->RemoveChildView(this);
}

void FriendsSidebarView::Init() {
}

gfx::Size FriendsSidebarView::GetPreferredSize() {
  gfx::Size prefsize(kFriendsSidebarWidth, 0);
  return prefsize;
}

void FriendsSidebarView::OnExtensionSizeChanged(ExtensionView* view) {
  // IGNORE
}

void FriendsSidebarView::InitializeExtensionHost() {
  std::string url = std::string("chrome-extension://") + std::string(chrome::kFacebookChatExtensionId) +
        std::string("/friends_sidebar.html");
  ExtensionProcessManager* manager =
      browser_->profile()->GetExtensionProcessManager();
  extension_host_.reset(manager->CreateViewHost(GURL(url), browser_, chrome::VIEW_TYPE_PANEL));

  registrar_.RemoveAll();
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSIONS_READY,
                content::Source<Profile>(browser_->profile()->GetOriginalProfile()));

  if (extension_host_.get()) {
    AddChildView(extension_host_->view());
    extension_host_->view()->SetContainer(this);

    // Wait to show the popup until the contained host finishes loading.
    registrar_.Add(this, content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
                   content::Source<WebContents>(extension_host_->host_contents()));

    // Listen for the containing view calling window.close();
    registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_HOST_VIEW_SHOULD_CLOSE,
                   content::Source<Profile>(extension_host_->profile()));
  }
}

void FriendsSidebarView::Observe(int type,
      const content::NotificationSource& source,
      const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_EXTENSIONS_READY: {
      //const Extension* extension = content::Details<const Extension>(details).ptr();
      //if (extension->id() == chrome::kFacebookChatExtensionId) {
        this->RemoveAllChildViews(false);
        this->InitializeExtensionHost();
      //}

      break;
    }

    case content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME: {
      DCHECK(content::Source<WebContents>(extension_host_->host_contents()) == source);
      //this->AddChildView(extension_host_->view());
      //Layout();
      break;
    }

    case chrome::NOTIFICATION_EXTENSION_HOST_VIEW_SHOULD_CLOSE: {
      if (content::Details<extensions::ExtensionHost>(extension_host_.get()) == details) {
        this->RemoveAllChildViews(false);
      }
      break;
    }

    default:
      DCHECK(false) << "Not reached";
  }
}
