// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/tab_contents/interstitial_page.h"

#include <vector>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/message_loop.h"
#include "base/string_util.h"
#include "base/threading/thread.h"
#include "base/utf_string_conversions.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/renderer_host/render_widget_host_view.h"
#include "content/browser/renderer_host/resource_dispatcher_host.h"
#include "content/browser/site_instance_impl.h"
#include "content/browser/tab_contents/navigation_controller_impl.h"
#include "content/browser/tab_contents/navigation_entry_impl.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/common/dom_storage_common.h"
#include "content/common/view_messages.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/common/page_transition_types.h"
#include "content/public/common/view_type.h"
#include "net/base/escape.h"
#include "net/url_request/url_request_context_getter.h"

using content::BrowserThread;
using content::NavigationController;
using content::NavigationEntry;
using content::NavigationEntryImpl;
using content::RenderViewHostDelegate;
using content::SiteInstance;
using content::WebContents;
using content::WebContentsView;
using WebKit::WebDragOperation;
using WebKit::WebDragOperationsMask;

namespace {

void ResourceRequestHelper(ResourceDispatcherHost* resource_dispatcher_host,
                           int process_id,
                           int render_view_host_id,
                           ResourceRequestAction action) {
  switch (action) {
    case BLOCK:
      resource_dispatcher_host->BlockRequestsForRoute(
          process_id, render_view_host_id);
      break;
    case RESUME:
      resource_dispatcher_host->ResumeBlockedRequestsForRoute(
          process_id, render_view_host_id);
      break;
    case CANCEL:
      resource_dispatcher_host->CancelBlockedRequestsForRoute(
          process_id, render_view_host_id);
      break;
    default:
      NOTREACHED();
  }
}

}  // namespace

class InterstitialPage::InterstitialPageRVHViewDelegate
    : public RenderViewHostDelegate::View {
 public:
  explicit InterstitialPageRVHViewDelegate(InterstitialPage* page);

  // RenderViewHostDelegate::View implementation:
  virtual void CreateNewWindow(
      int route_id,
      const ViewHostMsg_CreateWindow_Params& params);
  virtual void CreateNewWidget(int route_id,
                               WebKit::WebPopupType popup_type);
  virtual void CreateNewFullscreenWidget(int route_id);
  virtual void ShowCreatedWindow(int route_id,
                                 WindowOpenDisposition disposition,
                                 const gfx::Rect& initial_pos,
                                 bool user_gesture);
  virtual void ShowCreatedWidget(int route_id,
                                 const gfx::Rect& initial_pos);
  virtual void ShowCreatedFullscreenWidget(int route_id);
  virtual void ShowContextMenu(const ContextMenuParams& params);
  virtual void ShowPopupMenu(const gfx::Rect& bounds,
                             int item_height,
                             double item_font_size,
                             int selected_item,
                             const std::vector<WebMenuItem>& items,
                             bool right_aligned);
  virtual void StartDragging(const WebDropData& drop_data,
                             WebDragOperationsMask operations_allowed,
                             const SkBitmap& image,
                             const gfx::Point& image_offset);
  virtual void UpdateDragCursor(WebDragOperation operation);
  virtual void GotFocus();
  virtual void TakeFocus(bool reverse);
  virtual void OnFindReply(int request_id,
                           int number_of_matches,
                           const gfx::Rect& selection_rect,
                           int active_match_ordinal,
                           bool final_update);

 private:
  InterstitialPage* interstitial_page_;

  DISALLOW_COPY_AND_ASSIGN(InterstitialPageRVHViewDelegate);
};

// static
InterstitialPage::InterstitialPageMap*
    InterstitialPage::tab_to_interstitial_page_ =  NULL;

InterstitialPage::InterstitialPage(WebContents* tab,
                                   bool new_navigation,
                                   const GURL& url)
    : tab_(static_cast<TabContents*>(tab)),
      url_(url),
      new_navigation_(new_navigation),
      should_discard_pending_nav_entry_(new_navigation),
      reload_on_dont_proceed_(false),
      enabled_(true),
      action_taken_(NO_ACTION),
      render_view_host_(NULL),
      original_child_id_(tab->GetRenderProcessHost()->GetID()),
      original_rvh_id_(tab->GetRenderViewHost()->routing_id()),
      should_revert_tab_title_(false),
      tab_was_loading_(false),
      resource_dispatcher_host_notified_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(rvh_view_delegate_(
          new InterstitialPageRVHViewDelegate(this))) {
  InitInterstitialPageMap();
  // It would be inconsistent to create an interstitial with no new navigation
  // (which is the case when the interstitial was triggered by a sub-resource on
  // a page) when we have a pending entry (in the process of loading a new top
  // frame).
  DCHECK(new_navigation || !tab->GetController().GetPendingEntry());
}

InterstitialPage::~InterstitialPage() {
  InterstitialPageMap::iterator iter = tab_to_interstitial_page_->find(tab_);
  DCHECK(iter != tab_to_interstitial_page_->end());
  if (iter != tab_to_interstitial_page_->end())
    tab_to_interstitial_page_->erase(iter);
  DCHECK(!render_view_host_);
}

void InterstitialPage::Show() {
  // If an interstitial is already showing or about to be shown, close it before
  // showing the new one.
  // Be careful not to take an action on the old interstitial more than once.
  InterstitialPageMap::const_iterator iter =
      tab_to_interstitial_page_->find(tab_);
  if (iter != tab_to_interstitial_page_->end()) {
    InterstitialPage* interstitial = iter->second;
    if (interstitial->action_taken_ != NO_ACTION) {
      interstitial->Hide();
    } else {
      // If we are currently showing an interstitial page for which we created
      // a transient entry and a new interstitial is shown as the result of a
      // new browser initiated navigation, then that transient entry has already
      // been discarded and a new pending navigation entry created.
      // So we should not discard that new pending navigation entry.
      // See http://crbug.com/9791
      if (new_navigation_ && interstitial->new_navigation_)
        interstitial->should_discard_pending_nav_entry_= false;
      interstitial->DontProceed();
    }
  }

  // Block the resource requests for the render view host while it is hidden.
  TakeActionOnResourceDispatcher(BLOCK);
  // We need to be notified when the RenderViewHost is destroyed so we can
  // cancel the blocked requests.  We cannot do that on
  // NOTIFY_TAB_CONTENTS_DESTROYED as at that point the RenderViewHost has
  // already been destroyed.
  notification_registrar_.Add(
      this, content::NOTIFICATION_RENDER_WIDGET_HOST_DESTROYED,
      content::Source<RenderWidgetHost>(tab_->GetRenderViewHost()));

  // Update the tab_to_interstitial_page_ map.
  iter = tab_to_interstitial_page_->find(tab_);
  DCHECK(iter == tab_to_interstitial_page_->end());
  (*tab_to_interstitial_page_)[tab_] = this;

  if (new_navigation_) {
    NavigationEntryImpl* entry = new NavigationEntryImpl;
    entry->SetURL(url_);
    entry->SetVirtualURL(url_);
    entry->set_page_type(content::PAGE_TYPE_INTERSTITIAL);

    // Give sub-classes a chance to set some states on the navigation entry.
    UpdateEntry(entry);

    tab_->GetControllerImpl().AddTransientEntry(entry);
  }

  DCHECK(!render_view_host_);
  render_view_host_ = CreateRenderViewHost();
  CreateWebContentsView();

  std::string data_url = "data:text/html;charset=utf-8," +
                         net::EscapePath(GetHTMLContents());
  render_view_host_->NavigateToURL(GURL(data_url));

  notification_registrar_.Add(this,
                              content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
                              content::Source<WebContents>(tab_));
  notification_registrar_.Add(this, content::NOTIFICATION_NAV_ENTRY_COMMITTED,
      content::Source<NavigationController>(&tab_->GetController()));
  notification_registrar_.Add(this, content::NOTIFICATION_NAV_ENTRY_PENDING,
      content::Source<NavigationController>(&tab_->GetController()));
}

void InterstitialPage::Hide() {
  RenderWidgetHostView* old_view = tab_->GetRenderViewHost()->view();
  if (tab_->GetInterstitialPage() == this &&
      old_view && !old_view->IsShowing()) {
    // Show the original RVH since we're going away.  Note it might not exist if
    // the renderer crashed while the interstitial was showing.
    // Note that it is important that we don't call Show() if the view is
    // already showing. That would result in bad things (unparented HWND on
    // Windows for example) happening.
    old_view->Show();
  }

  // If the focus was on the interstitial, let's keep it to the page.
  // (Note that in unit-tests the RVH may not have a view).
  if (render_view_host_->view() && render_view_host_->view()->HasFocus() &&
      tab_->GetRenderViewHost()->view()) {
    tab_->GetRenderViewHost()->view()->Focus();
  }

  render_view_host_->Shutdown();
  render_view_host_ = NULL;
  if (tab_->GetInterstitialPage())
    tab_->remove_interstitial_page();
  // Let's revert to the original title if necessary.
  NavigationEntry* entry = tab_->GetController().GetActiveEntry();
  if (!new_navigation_ && should_revert_tab_title_) {
    entry->SetTitle(original_tab_title_);
    tab_->NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TITLE);
  }

  content::NotificationService::current()->Notify(
      content::NOTIFICATION_INTERSTITIAL_DETACHED,
      content::Source<WebContents>(tab_),
      content::NotificationService::NoDetails());

  delete this;
}

void InterstitialPage::Observe(int type,
                               const content::NotificationSource& source,
                               const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_NAV_ENTRY_PENDING:
      // We are navigating away from the interstitial (the user has typed a URL
      // in the location bar or clicked a bookmark).  Make sure clicking on the
      // interstitial will have no effect.  Also cancel any blocked requests
      // on the ResourceDispatcherHost.  Note that when we get this notification
      // the RenderViewHost has not yet navigated so we'll unblock the
      // RenderViewHost before the resource request for the new page we are
      // navigating arrives in the ResourceDispatcherHost.  This ensures that
      // request won't be blocked if the same RenderViewHost was used for the
      // new navigation.
      Disable();
      TakeActionOnResourceDispatcher(CANCEL);
      break;
    case content::NOTIFICATION_RENDER_WIDGET_HOST_DESTROYED:
      if (action_taken_ == NO_ACTION) {
        // The RenderViewHost is being destroyed (as part of the tab being
        // closed); make sure we clear the blocked requests.
        RenderViewHost* rvh = static_cast<RenderViewHost*>(
            content::Source<RenderWidgetHost>(source).ptr());
        DCHECK(rvh->process()->GetID() == original_child_id_ &&
               rvh->routing_id() == original_rvh_id_);
        TakeActionOnResourceDispatcher(CANCEL);
      }
      break;
    case content::NOTIFICATION_WEB_CONTENTS_DESTROYED:
    case content::NOTIFICATION_NAV_ENTRY_COMMITTED:
      if (action_taken_ == NO_ACTION) {
        // We are navigating away from the interstitial or closing a tab with an
        // interstitial.  Default to DontProceed(). We don't just call Hide as
        // subclasses will almost certainly override DontProceed to do some work
        // (ex: close pending connections).
        DontProceed();
      } else {
        // User decided to proceed and either the navigation was committed or
        // the tab was closed before that.
        Hide();
        // WARNING: we are now deleted!
      }
      break;
    default:
      NOTREACHED();
  }
}

RenderViewHostDelegate::View* InterstitialPage::GetViewDelegate() {
  return rvh_view_delegate_.get();
}

const GURL& InterstitialPage::GetURL() const {
  return url_;
}

void InterstitialPage::RenderViewGone(RenderViewHost* render_view_host,
                                      base::TerminationStatus status,
                                      int error_code) {
  // Our renderer died. This should not happen in normal cases.
  // Just dismiss the interstitial.
  DontProceed();
}

void InterstitialPage::DidNavigate(
    RenderViewHost* render_view_host,
    const ViewHostMsg_FrameNavigate_Params& params) {
  // A fast user could have navigated away from the page that triggered the
  // interstitial while the interstitial was loading, that would have disabled
  // us. In that case we can dismiss ourselves.
  if (!enabled_) {
    DontProceed();
    return;
  }
  if (params.transition == content::PAGE_TRANSITION_AUTO_SUBFRAME) {
    // No need to handle navigate message from iframe in the interstitial page.
    return;
  }

  // The RenderViewHost has loaded its contents, we can show it now.
  render_view_host_->view()->Show();
  tab_->set_interstitial_page(this);

  // This notification hides the bookmark bar. Note that this has to happen
  // after the interstitial page was registered with |tab_|, since there will be
  // a callback to |tab_| testing if an interstitial page is showing before
  // hiding the bookmark bar.
  content::NotificationService::current()->Notify(
      content::NOTIFICATION_INTERSTITIAL_ATTACHED,
      content::Source<WebContents>(tab_),
      content::NotificationService::NoDetails());

  RenderWidgetHostView* rwh_view = tab_->GetRenderViewHost()->view();

  // The RenderViewHost may already have crashed before we even get here.
  if (rwh_view) {
    // If the page has focus, focus the interstitial.
    if (rwh_view->HasFocus())
      Focus();

    // Hide the original RVH since we're showing the interstitial instead.
    rwh_view->Hide();
  }

  // Notify the tab we are not loading so the throbber is stopped. It also
  // causes a NOTIFY_LOAD_STOP notification, that the AutomationProvider (used
  // by the UI tests) expects to consider a navigation as complete. Without
  // this, navigating in a UI test to a URL that triggers an interstitial would
  // hang.
  tab_was_loading_ = tab_->IsLoading();
  tab_->SetIsLoading(false, NULL);
}

void InterstitialPage::UpdateTitle(RenderViewHost* render_view_host,
                                   int32 page_id,
                                   const string16& title,
                                   base::i18n::TextDirection title_direction) {
  DCHECK(render_view_host == render_view_host_);
  NavigationEntry* entry = tab_->GetController().GetActiveEntry();
  if (!entry) {
    // Crash reports from the field indicate this can be NULL.
    // This is unexpected as InterstitialPages constructed with the
    // new_navigation flag set to true create a transient navigation entry
    // (that is returned as the active entry). And the only case so far of
    // interstitial created with that flag set to false is with the
    // SafeBrowsingBlockingPage, when the resource triggering the interstitial
    // is a sub-resource, meaning the main page has already been loaded and a
    // navigation entry should have been created.
    NOTREACHED();
    return;
  }

  // If this interstitial is shown on an existing navigation entry, we'll need
  // to remember its title so we can revert to it when hidden.
  if (!new_navigation_ && !should_revert_tab_title_) {
    original_tab_title_ = entry->GetTitle();
    should_revert_tab_title_ = true;
  }
  // TODO(evan): make use of title_direction.
  // http://code.google.com/p/chromium/issues/detail?id=27094
  entry->SetTitle(title);
  tab_->NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TITLE);
}

content::RendererPreferences InterstitialPage::GetRendererPrefs(
    content::BrowserContext* browser_context) const {
  return renderer_preferences_;
}

WebPreferences InterstitialPage::GetWebkitPrefs() {
  return content::GetContentClient()->browser()->GetWebkitPrefs(
      render_view_host());
}

bool InterstitialPage::PreHandleKeyboardEvent(
    const NativeWebKeyboardEvent& event,
    bool* is_keyboard_shortcut) {
  return tab_->PreHandleKeyboardEvent(event, is_keyboard_shortcut);
}

void InterstitialPage::HandleKeyboardEvent(
      const NativeWebKeyboardEvent& event) {
  return tab_->HandleKeyboardEvent(event);
}

WebContents* InterstitialPage::tab() const {
  return tab_;
}

RenderViewHost* InterstitialPage::CreateRenderViewHost() {
  RenderViewHost* render_view_host = new RenderViewHost(
      SiteInstance::Create(tab()->GetBrowserContext()),
      this, MSG_ROUTING_NONE, kInvalidSessionStorageNamespaceId);
  return render_view_host;
}

WebContentsView* InterstitialPage::CreateWebContentsView() {
  WebContentsView* web_contents_view = tab()->GetView();
  RenderWidgetHostView* view =
      web_contents_view->CreateViewForWidget(render_view_host_);
  render_view_host_->SetView(view);
  render_view_host_->AllowBindings(content::BINDINGS_POLICY_DOM_AUTOMATION);

  int32 max_page_id =
      tab()->GetMaxPageIDForSiteInstance(render_view_host_->site_instance());
  render_view_host_->CreateRenderView(string16(), max_page_id);
  view->SetSize(web_contents_view->GetContainerSize());
  // Don't show the interstitial until we have navigated to it.
  view->Hide();
  return web_contents_view;
}

void InterstitialPage::Proceed() {
  if (action_taken_ != NO_ACTION) {
    NOTREACHED();
    return;
  }
  Disable();
  action_taken_ = PROCEED_ACTION;

  // Resumes the throbber, if applicable.
  if (tab_was_loading_)
    tab_->SetIsLoading(true, NULL);

  // If this is a new navigation, the old page is going away, so we cancel any
  // blocked requests for it.  If it is not a new navigation, then it means the
  // interstitial was shown as a result of a resource loading in the page.
  // Since the user wants to proceed, we'll let any blocked request go through.
  if (new_navigation_)
    TakeActionOnResourceDispatcher(CANCEL);
  else
    TakeActionOnResourceDispatcher(RESUME);

  // No need to hide if we are a new navigation, we'll get hidden when the
  // navigation is committed.
  if (!new_navigation_) {
    Hide();
    // WARNING: we are now deleted!
  }
}

std::string InterstitialPage::GetHTMLContents() {
  return std::string();
}

void InterstitialPage::DontProceed() {
  DCHECK(action_taken_ != DONT_PROCEED_ACTION);

  Disable();
  action_taken_ = DONT_PROCEED_ACTION;

  // If this is a new navigation, we are returning to the original page, so we
  // resume blocked requests for it.  If it is not a new navigation, then it
  // means the interstitial was shown as a result of a resource loading in the
  // page and we won't return to the original page, so we cancel blocked
  // requests in that case.
  if (new_navigation_)
    TakeActionOnResourceDispatcher(RESUME);
  else
    TakeActionOnResourceDispatcher(CANCEL);

  if (should_discard_pending_nav_entry_) {
    // Since no navigation happens we have to discard the transient entry
    // explicitely.  Note that by calling DiscardNonCommittedEntries() we also
    // discard the pending entry, which is what we want, since the navigation is
    // cancelled.
    tab_->GetController().DiscardNonCommittedEntries();
  }

  if (reload_on_dont_proceed_)
    tab_->GetController().Reload(true);

  Hide();
  // WARNING: we are now deleted!
}

void InterstitialPage::CancelForNavigation() {
  // The user is trying to navigate away.  We should unblock the renderer and
  // disable the interstitial, but keep it visible until the navigation
  // completes.
  Disable();
  // If this interstitial was shown for a new navigation, allow any navigations
  // on the original page to resume (e.g., subresource requests, XHRs, etc).
  // Otherwise, cancel the pending, possibly dangerous navigations.
  if (new_navigation_)
    TakeActionOnResourceDispatcher(RESUME);
  else
    TakeActionOnResourceDispatcher(CANCEL);
}

void InterstitialPage::SetSize(const gfx::Size& size) {
#if !defined(OS_MACOSX)
  // When a tab is closed, we might be resized after our view was NULLed
  // (typically if there was an info-bar).
  if (render_view_host_->view())
    render_view_host_->view()->SetSize(size);
#else
  // TODO(port): Does Mac need to SetSize?
  NOTIMPLEMENTED();
#endif
}

void InterstitialPage::Focus() {
  // Focus the native window.
  render_view_host_->view()->Focus();
}

void InterstitialPage::FocusThroughTabTraversal(bool reverse) {
  render_view_host_->SetInitialFocus(reverse);
}

content::ViewType InterstitialPage::GetRenderViewType() const {
  return content::VIEW_TYPE_INTERSTITIAL_PAGE;
}

gfx::Rect InterstitialPage::GetRootWindowResizerRect() const {
  return gfx::Rect();
}

void InterstitialPage::Disable() {
  enabled_ = false;
}

void InterstitialPage::TakeActionOnResourceDispatcher(
    ResourceRequestAction action) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI)) <<
      "TakeActionOnResourceDispatcher should be called on the main thread.";

  if (action == CANCEL || action == RESUME) {
    if (resource_dispatcher_host_notified_)
      return;
    resource_dispatcher_host_notified_ = true;
  }

  // The tab might not have a render_view_host if it was closed (in which case,
  // we have taken care of the blocked requests when processing
  // NOTIFY_RENDER_WIDGET_HOST_DESTROYED.
  // Also we need to test there is a ResourceDispatcherHost, as when unit-tests
  // we don't have one.
  RenderViewHost* rvh = RenderViewHost::FromID(original_child_id_,
                                               original_rvh_id_);
  if (!rvh || !ResourceDispatcherHost::Get())
    return;

  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(
          &ResourceRequestHelper,
          ResourceDispatcherHost::Get(),
          original_child_id_,
          original_rvh_id_,
          action));
}

// static
void InterstitialPage::InitInterstitialPageMap() {
  if (!tab_to_interstitial_page_)
    tab_to_interstitial_page_ = new InterstitialPageMap;
}

// static
InterstitialPage* InterstitialPage::GetInterstitialPage(
    WebContents* web_contents) {
  InitInterstitialPageMap();
  TabContents* tab_contents = static_cast<TabContents*>(web_contents);
  InterstitialPageMap::const_iterator iter =
      tab_to_interstitial_page_->find(tab_contents);
  if (iter == tab_to_interstitial_page_->end())
    return NULL;

  return iter->second;
}

InterstitialPage::InterstitialPageRVHViewDelegate::
    InterstitialPageRVHViewDelegate(InterstitialPage* page)
    : interstitial_page_(page) {
}

void InterstitialPage::InterstitialPageRVHViewDelegate::CreateNewWindow(
    int route_id,
    const ViewHostMsg_CreateWindow_Params& params) {
  NOTREACHED() << "InterstitialPage does not support showing popups yet.";
}

void InterstitialPage::InterstitialPageRVHViewDelegate::CreateNewWidget(
    int route_id, WebKit::WebPopupType popup_type) {
  NOTREACHED() << "InterstitialPage does not support showing drop-downs yet.";
}

void
InterstitialPage::InterstitialPageRVHViewDelegate::CreateNewFullscreenWidget(
    int route_id) {
  NOTREACHED()
      << "InterstitialPage does not support showing full screen popups.";
}

void InterstitialPage::InterstitialPageRVHViewDelegate::ShowCreatedWindow(
    int route_id, WindowOpenDisposition disposition,
    const gfx::Rect& initial_pos, bool user_gesture) {
  NOTREACHED() << "InterstitialPage does not support showing popups yet.";
}

void InterstitialPage::InterstitialPageRVHViewDelegate::ShowCreatedWidget(
    int route_id, const gfx::Rect& initial_pos) {
  NOTREACHED() << "InterstitialPage does not support showing drop-downs yet.";
}

void
InterstitialPage::InterstitialPageRVHViewDelegate::ShowCreatedFullscreenWidget(
    int route_id) {
  NOTREACHED()
      << "InterstitialPage does not support showing full screen popups.";
}

void InterstitialPage::InterstitialPageRVHViewDelegate::ShowContextMenu(
    const ContextMenuParams& params) {
}

void InterstitialPage::InterstitialPageRVHViewDelegate::ShowPopupMenu(
    const gfx::Rect& bounds,
    int item_height,
    double item_font_size,
    int selected_item,
    const std::vector<WebMenuItem>& items,
    bool right_aligned) {
}

void InterstitialPage::InterstitialPageRVHViewDelegate::StartDragging(
    const WebDropData& drop_data,
    WebDragOperationsMask allowed_operations,
    const SkBitmap& image,
    const gfx::Point& image_offset) {
  NOTREACHED() << "InterstitialPage does not support dragging yet.";
}

void InterstitialPage::InterstitialPageRVHViewDelegate::UpdateDragCursor(
    WebDragOperation) {
  NOTREACHED() << "InterstitialPage does not support dragging yet.";
}

void InterstitialPage::InterstitialPageRVHViewDelegate::GotFocus() {
}

void InterstitialPage::InterstitialPageRVHViewDelegate::TakeFocus(
    bool reverse) {
  if (!interstitial_page_->tab())
    return;
  TabContents* tab = static_cast<TabContents*>(interstitial_page_->tab());
  if (!tab->GetViewDelegate())
    return;

  tab->GetViewDelegate()->TakeFocus(reverse);
}

void InterstitialPage::InterstitialPageRVHViewDelegate::OnFindReply(
    int request_id, int number_of_matches, const gfx::Rect& selection_rect,
    int active_match_ordinal, bool final_update) {
}
