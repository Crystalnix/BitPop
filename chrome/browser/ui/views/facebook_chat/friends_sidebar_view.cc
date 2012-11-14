#include "chrome/browser/ui/views/facebook_chat/friends_sidebar_view.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "ui/gfx/canvas.h"

// Sidebar width
static const int kFriendsSidebarWidth = 185;

FriendsSidebarView::FriendsSidebarView(Browser* browser, BrowserView *parent) :
  browser_(browser),
  parent_(parent) {
    set_id(VIEW_ID_FACEBOOK_FRIENDS_SIDE_BAR_CONTAINER);
    parent->AddChildView(this);

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

// TODO: remove this as it's currently just winds up to parent method
void FriendsSidebarView::OnPaint(gfx::Canvas* canvas) {
  TabContentsContainer::OnPaint(canvas);
  //canvas->FillRectInt(kBorderColor, 0, 0, 1, height());
}

void FriendsSidebarView::Layout() {
  if (has_children()) {
    child_at(0)->SetBounds(0, 0, width(), height());
    child_at(0)->Layout();
  }
}
