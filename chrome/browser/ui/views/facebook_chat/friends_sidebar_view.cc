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
	  FriendsSidebarView::extension_page_contents_->set_delegate(this);
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
  SetVisible(true);
}

gfx::Size FriendsSidebarView::GetPreferredSize() {
  gfx::Size prefsize(kFriendsSidebarWidth, 0);
  return prefsize;
}

void FriendsSidebarView::OnPaint(gfx::Canvas* canvas) {
  TabContentsContainer::OnPaint(canvas);
  canvas->FillRectInt(kBorderColor, 2, 0, 1, height());
}

void FriendsSidebarView::OpenURLFromTab(TabContents* source,
                              const GURL& url,
                              const GURL& referrer,
                              WindowOpenDisposition disposition,
                              PageTransition::Type transition) {
// do nothing
}
  
void FriendsSidebarView::NavigationStateChanged(const TabContents* source,
                                      unsigned changed_flags) {
// do nothing
}

void FriendsSidebarView::AddNewContents(TabContents* source,
                              TabContents* new_contents,
                              WindowOpenDisposition disposition,
                              const gfx::Rect& initial_pos,
                              bool user_gesture){
// do nothing
}

void FriendsSidebarView::ActivateContents(TabContents* contents){
// do nothing
}

void FriendsSidebarView::DeactivateContents(TabContents* contents){
// do nothing
}

void FriendsSidebarView::LoadingStateChanged(TabContents* source){
// do nothing
}

void FriendsSidebarView::CloseContents(TabContents* source) {
// do nothing
}

void FriendsSidebarView::MoveContents(TabContents* source, const gfx::Rect& pos) {
// do nothing
}

void FriendsSidebarView::DetachContents(TabContents* source) {
// do nothing
}

bool FriendsSidebarView::IsPopupOrPanel(const TabContents* source) const {
// do nothing
	return false;
}

bool FriendsSidebarView::CanReloadContents(TabContents* source) const {
// do nothing
	return false;
}

void FriendsSidebarView::UpdateTargetURL(TabContents* source, const GURL& url) {
// do nothing
}

void FriendsSidebarView::ContentsMouseEvent(
      TabContents* source, const gfx::Point& location, bool motion){
// do nothing
}

void FriendsSidebarView::ContentsZoomChange(bool zoom_in){
// do nothing
}

void FriendsSidebarView::SetTabContentBlocked(TabContents* contents, bool blocked){
// do nothing
}

void FriendsSidebarView::TabContentsFocused(TabContents* tab_content){
// do nothing
}

bool FriendsSidebarView::TakeFocus(bool reverse) {
// do nothing
	return false;
}
bool FriendsSidebarView::IsApplication() const {
// do nothing
	return false;
}

void FriendsSidebarView::ConvertContentsToApplication(TabContents* source) {
// do nothing
}

bool FriendsSidebarView::ShouldDisplayURLField() {
// do nothing
	return false;
}

void FriendsSidebarView::BeforeUnloadFired(TabContents* source,
                                 bool proceed,
                                 bool* proceed_to_fire_unload) {
// do nothing
}

void FriendsSidebarView::SetFocusToLocationBar(bool select_all) {
// do nothing
}

void FriendsSidebarView::RenderWidgetShowing() {
// do nothing
}

int FriendsSidebarView::GetExtraRenderViewHeight() const {
// do nothing
	return 0;
}

void FriendsSidebarView::ShowPageInfo(Profile* profile,
                            const GURL& url,
                            const NavigationEntry::SSLStatus& ssl,
                            bool show_history) {
// do nothing
}

void FriendsSidebarView::ViewSourceForTab(TabContents* source, const GURL& page_url) {
// do nothing
}

void FriendsSidebarView::ViewSourceForFrame(TabContents* source,
                                  const GURL& frame_url,
                                  const std::string& frame_content_state) {
// do nothing
}

bool FriendsSidebarView::PreHandleKeyboardEvent(const NativeWebKeyboardEvent& event,
                                        bool* is_keyboard_shortcut)	{
// do nothing
    return false;
}

void FriendsSidebarView::HandleKeyboardEvent(const NativeWebKeyboardEvent& event) {
// do nothing
}

void FriendsSidebarView::ShowRepostFormWarningDialog(TabContents* tab_contents) {
// do nothing
}

bool FriendsSidebarView::ShouldAddNavigationToHistory(
      const history::HistoryAddPageArgs& add_page_args,
      NavigationType::Type navigation_type) {
// do nothing
	return false;
}

void FriendsSidebarView::ContentRestrictionsChanged(TabContents* source) {
// do nothing
}

void FriendsSidebarView::WorkerCrashed() {
// do nothing
}