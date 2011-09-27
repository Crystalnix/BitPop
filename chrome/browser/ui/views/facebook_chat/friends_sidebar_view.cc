#include "chrome/browser/ui/views/facebook_chat/friends_sidebar_view.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "ui/gfx/canvas.h"

// TODO: move this to chromium url constants header
const char kFriendsSidebarExtensionPageUrl[] = "http://www.google.com";

// Border color.
static const SkColor kBorderColor = SkColorSetRGB(214, 214, 214);

// Sidebar width
static const int kFriendsSidebarWidth = 180;

scoped_ptr<TabContents> FriendsSidebarView::extension_page_contents_;

FriendsSidebarView::FriendsSidebarView(Browser* browser, BrowserView *parent) :
  browser_(browser),
  parent_(parent) {
    SetID(VIEW_ID_FACEBOOK_FRIENDS_SIDE_BAR_CONTAINER);
    parent->AddChildView(this);
    if (!FriendsSidebarView::extension_page_contents_.get()) {
      FriendsSidebarView::extension_page_contents_.reset(
          new TabContents(browser->GetProfile(), NULL, MSG_ROUTING_NONE, 
            NULL, NULL));
      FriendsSidebarView::extension_page_contents_->controller()
         .LoadURL(GURL(kFriendsSidebarExtensionPageUrl),
           GURL(), PageTransition::START_PAGE);
    }
    Init();
}

FriendsSidebarView::~FriendsSidebarView() {
  parent_->RemoveChildView(this);
}

void FriendsSidebarView::Init() {
  ChangeTabContents(FriendsSidebarView::extension_page_contents_.get());
}

gfx::Size FriendsSidebarView::GetPreferredSize() {
  gfx::Size prefsize(kFriendsSidebarWidth, 0);
  return prefsize;
}

void FriendsSidebarView::OnPaint(gfx::Canvas* canvas) {
  TabContentsContainer::OnPaint(canvas);
  canvas->FillRectInt(kBorderColor, 0, 0, 1, height());
}
