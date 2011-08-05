// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/render_view_host.h"

#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/i18n/rtl.h"
#include "base/json/json_reader.h"
#include "base/string_util.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "content/browser/browser_message_filter.h"
#include "content/browser/child_process_security_policy.h"
#include "content/browser/content_browser_client.h"
#include "content/browser/cross_site_request_manager.h"
#include "content/browser/host_zoom_map.h"
#include "content/browser/in_process_webkit/session_storage_namespace.h"
#include "content/browser/renderer_host/render_process_host.h"
#include "content/browser/renderer_host/render_view_host_delegate.h"
#include "content/browser/renderer_host/render_view_host_observer.h"
#include "content/browser/renderer_host/render_widget_host.h"
#include "content/browser/renderer_host/render_widget_host_view.h"
#include "content/browser/site_instance.h"
#include "content/browser/user_metrics.h"
#include "content/common/bindings_policy.h"
#include "content/common/content_constants.h"
#include "content/common/drag_messages.h"
#include "content/common/native_web_keyboard_event.h"
#include "content/common/notification_details.h"
#include "content/common/notification_service.h"
#include "content/common/notification_type.h"
#include "content/common/result_codes.h"
#include "content/common/swapped_out_messages.h"
#include "content/common/url_constants.h"
#include "content/common/view_messages.h"
#include "net/base/net_util.h"
#include "net/url_request/url_request_context_getter.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/native_widget_types.h"
#include "webkit/glue/context_menu.h"
#include "webkit/glue/webaccessibility.h"
#include "webkit/glue/webdropdata.h"

using base::TimeDelta;
using WebKit::WebConsoleMessage;
using WebKit::WebDragOperation;
using WebKit::WebDragOperationNone;
using WebKit::WebDragOperationsMask;
using WebKit::WebInputEvent;
using WebKit::WebMediaPlayerAction;
using WebKit::WebTextDirection;

namespace {

// Delay to wait on closing the tab for a beforeunload/unload handler to fire.
const int kUnloadTimeoutMS = 1000;

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// RenderViewHost, public:

// static
RenderViewHost* RenderViewHost::FromID(int render_process_id,
                                       int render_view_id) {
  RenderProcessHost* process = RenderProcessHost::FromID(render_process_id);
  if (!process)
    return NULL;
  RenderWidgetHost* widget = static_cast<RenderWidgetHost*>(
      process->GetListenerByID(render_view_id));
  if (!widget || !widget->IsRenderView())
    return NULL;
  return static_cast<RenderViewHost*>(widget);
}

RenderViewHost::RenderViewHost(SiteInstance* instance,
                               RenderViewHostDelegate* delegate,
                               int routing_id,
                               SessionStorageNamespace* session_storage)
    : RenderWidgetHost(instance->GetProcess(), routing_id),
      instance_(instance),
      delegate_(delegate),
      waiting_for_drag_context_response_(false),
      enabled_bindings_(0),
      pending_request_id_(-1),
      navigations_suspended_(false),
      suspended_nav_message_(NULL),
      is_swapped_out_(false),
      run_modal_reply_msg_(NULL),
      is_waiting_for_beforeunload_ack_(false),
      is_waiting_for_unload_ack_(false),
      unload_ack_is_for_cross_site_transition_(false),
      are_javascript_messages_suppressed_(false),
      sudden_termination_allowed_(false),
      session_storage_namespace_(session_storage),
      save_accessibility_tree_for_testing_(false),
      render_view_termination_status_(base::TERMINATION_STATUS_STILL_RUNNING) {
  if (!session_storage_namespace_) {
    session_storage_namespace_ =
        new SessionStorageNamespace(process()->profile()->GetWebKitContext());
  }

  DCHECK(instance_);
  CHECK(delegate_);  // http://crbug.com/82827

  process()->EnableSendQueue();

  content::GetContentClient()->browser()->RenderViewHostCreated(this);

  NotificationService::current()->Notify(
      NotificationType::RENDER_VIEW_HOST_CREATED,
      Source<RenderViewHost>(this),
      NotificationService::NoDetails());
}

RenderViewHost::~RenderViewHost() {
  FOR_EACH_OBSERVER(
      RenderViewHostObserver, observers_, RenderViewHostDestruction());

  NotificationService::current()->Notify(
      NotificationType::RENDER_VIEW_HOST_DELETED,
      Source<RenderViewHost>(this),
      NotificationService::NoDetails());

  delegate()->RenderViewDeleted(this);

  // Be sure to clean up any leftover state from cross-site requests.
  CrossSiteRequestManager::GetInstance()->SetHasPendingCrossSiteRequest(
      process()->id(), routing_id(), false);
}

bool RenderViewHost::CreateRenderView(const string16& frame_name) {
  DCHECK(!IsRenderViewLive()) << "Creating view twice";

  // The process may (if we're sharing a process with another host that already
  // initialized it) or may not (we have our own process or the old process
  // crashed) have been initialized. Calling Init multiple times will be
  // ignored, so this is safe.
  if (!process()->Init(renderer_accessible()))
    return false;
  DCHECK(process()->HasConnection());
  DCHECK(process()->profile());

  if (BindingsPolicy::is_web_ui_enabled(enabled_bindings_)) {
    ChildProcessSecurityPolicy::GetInstance()->GrantWebUIBindings(
        process()->id());
  }

  if (BindingsPolicy::is_extension_enabled(enabled_bindings_)) {
    ChildProcessSecurityPolicy::GetInstance()->GrantExtensionBindings(
        process()->id());
  }

  renderer_initialized_ = true;

  ViewMsg_New_Params params;
  params.parent_window = GetNativeViewId();
  params.compositing_surface = GetCompositingSurface();
  params.renderer_preferences =
      delegate_->GetRendererPrefs(process()->profile());
  params.web_preferences = delegate_->GetWebkitPrefs();
  params.view_id = routing_id();
  params.session_storage_namespace_id = session_storage_namespace_->id();
  params.frame_name = frame_name;
  Send(new ViewMsg_New(params));

  // If it's enabled, tell the renderer to set up the Javascript bindings for
  // sending messages back to the browser.
  Send(new ViewMsg_AllowBindings(routing_id(), enabled_bindings_));
  // Let our delegate know that we created a RenderView.
  delegate_->RenderViewCreated(this);

  FOR_EACH_OBSERVER(
      RenderViewHostObserver, observers_, RenderViewHostInitialized());

  return true;
}

bool RenderViewHost::IsRenderViewLive() const {
  return process()->HasConnection() && renderer_initialized_;
}

void RenderViewHost::SyncRendererPrefs() {
  Send(new ViewMsg_SetRendererPrefs(routing_id(),
                                    delegate_->GetRendererPrefs(
                                        process()->profile())));
}

void RenderViewHost::Navigate(const ViewMsg_Navigate_Params& params) {
  ChildProcessSecurityPolicy::GetInstance()->GrantRequestURL(
      process()->id(), params.url);

  ViewMsg_Navigate* nav_message = new ViewMsg_Navigate(routing_id(), params);

  // Only send the message if we aren't suspended at the start of a cross-site
  // request.
  if (navigations_suspended_) {
    // Shouldn't be possible to have a second navigation while suspended, since
    // navigations will only be suspended during a cross-site request.  If a
    // second navigation occurs, TabContents will cancel this pending RVH
    // create a new pending RVH.
    DCHECK(!suspended_nav_message_.get());
    suspended_nav_message_.reset(nav_message);
  } else {
    // Unset this, otherwise if true and the hang monitor fires we'll
    // incorrectly close the tab.
    is_waiting_for_unload_ack_ = false;

    Send(nav_message);

    // Force the throbber to start. We do this because WebKit's "started
    // loading" message will be received asynchronously from the UI of the
    // browser. But we want to keep the throbber in sync with what's happening
    // in the UI. For example, we want to start throbbing immediately when the
    // user naivgates even if the renderer is delayed. There is also an issue
    // with the throbber starting because the WebUI (which controls whether the
    // favicon is displayed) happens synchronously. If the start loading
    // messages was asynchronous, then the default favicon would flash in.
    //
    // WebKit doesn't send throb notifications for JavaScript URLs, so we
    // don't want to either.
    if (!params.url.SchemeIs(chrome::kJavaScriptScheme))
      delegate_->DidStartLoading();
  }

  FOR_EACH_OBSERVER(
      RenderViewHostObserver, observers_, Navigate(params));
}

void RenderViewHost::NavigateToURL(const GURL& url) {
  ViewMsg_Navigate_Params params;
  params.page_id = -1;
  params.pending_history_list_offset = -1;
  params.current_history_list_offset = -1;
  params.current_history_list_length = 0;
  params.url = url;
  params.transition = PageTransition::LINK;
  params.navigation_type = ViewMsg_Navigate_Type::NORMAL;
  Navigate(params);
}

void RenderViewHost::SetNavigationsSuspended(bool suspend) {
  // This should only be called to toggle the state.
  DCHECK(navigations_suspended_ != suspend);

  navigations_suspended_ = suspend;
  if (!suspend && suspended_nav_message_.get()) {
    // There's a navigation message waiting to be sent.  Now that we're not
    // suspended anymore, resume navigation by sending it.  If we were swapped
    // out, we should also stop filtering out the IPC messages now.
    is_swapped_out_ = false;
    Send(suspended_nav_message_.release());
  }
}

void RenderViewHost::CancelSuspendedNavigations() {
  // Clear any state if a pending navigation is canceled or pre-empted.
  if (suspended_nav_message_.get())
    suspended_nav_message_.reset();
  navigations_suspended_ = false;
}

void RenderViewHost::FirePageBeforeUnload(bool for_cross_site_transition) {
  if (!IsRenderViewLive()) {
    // This RenderViewHost doesn't have a live renderer, so just skip running
    // the onbeforeunload handler.
    is_waiting_for_beforeunload_ack_ = true;  // Checked by OnMsgShouldCloseACK.
    unload_ack_is_for_cross_site_transition_ = for_cross_site_transition;
    OnMsgShouldCloseACK(true);
    return;
  }

  // This may be called more than once (if the user clicks the tab close button
  // several times, or if she clicks the tab close button then the browser close
  // button), and we only send the message once.
  if (is_waiting_for_beforeunload_ack_) {
    // Some of our close messages could be for the tab, others for cross-site
    // transitions. We always want to think it's for closing the tab if any
    // of the messages were, since otherwise it might be impossible to close
    // (if there was a cross-site "close" request pending when the user clicked
    // the close button). We want to keep the "for cross site" flag only if
    // both the old and the new ones are also for cross site.
    unload_ack_is_for_cross_site_transition_ =
        unload_ack_is_for_cross_site_transition_ && for_cross_site_transition;
  } else {
    // Start the hang monitor in case the renderer hangs in the beforeunload
    // handler.
    is_waiting_for_beforeunload_ack_ = true;
    unload_ack_is_for_cross_site_transition_ = for_cross_site_transition;
    StartHangMonitorTimeout(TimeDelta::FromMilliseconds(kUnloadTimeoutMS));
    Send(new ViewMsg_ShouldClose(routing_id()));
  }
}

void RenderViewHost::SwapOut(int new_render_process_host_id,
                             int new_request_id) {
  // Start filtering IPC messages to avoid confusing the delegate.  This will
  // prevent any dialogs from appearing during unload handlers, but we've
  // already decided to silence them in crbug.com/68780.  We will set it back
  // to false in SetNavigationsSuspended if we swap back in.
  is_swapped_out_ = true;

  // This will be set back to false in OnSwapOutACK, just before we replace
  // this RVH with the pending RVH.
  is_waiting_for_unload_ack_ = true;
  // Start the hang monitor in case the renderer hangs in the unload handler.
  StartHangMonitorTimeout(TimeDelta::FromMilliseconds(kUnloadTimeoutMS));

  ViewMsg_SwapOut_Params params;
  params.closing_process_id = process()->id();
  params.closing_route_id = routing_id();
  params.new_render_process_host_id = new_render_process_host_id;
  params.new_request_id = new_request_id;
  if (IsRenderViewLive()) {
    Send(new ViewMsg_SwapOut(routing_id(), params));
  } else {
    // This RenderViewHost doesn't have a live renderer, so just skip the unload
    // event.  We must notify the ResourceDispatcherHost on the IO thread,
    // which we will do through the RenderProcessHost's widget helper.
    process()->CrossSiteSwapOutACK(params);
  }
}

void RenderViewHost::OnSwapOutACK() {
  // Stop the hang monitor now that the unload handler has finished.
  StopHangMonitorTimeout();
  is_waiting_for_unload_ack_ = false;
}

void RenderViewHost::WasSwappedOut() {
  // Don't bother reporting hung state anymore.
  StopHangMonitorTimeout();

  // Inform the renderer that it can exit if no one else is using it.
  Send(new ViewMsg_WasSwappedOut(routing_id()));
}

void RenderViewHost::ClosePage() {
  // Start the hang monitor in case the renderer hangs in the unload handler.
  is_waiting_for_unload_ack_ = true;
  StartHangMonitorTimeout(TimeDelta::FromMilliseconds(kUnloadTimeoutMS));

  if (IsRenderViewLive()) {
    // TODO(creis): Should this be moved to Shutdown?  It may not be called for
    // RenderViewHosts that have been swapped out.
    NotificationService::current()->Notify(
        NotificationType::RENDER_VIEW_HOST_WILL_CLOSE_RENDER_VIEW,
        Source<RenderViewHost>(this),
        NotificationService::NoDetails());

    Send(new ViewMsg_ClosePage(routing_id()));
  } else {
    // This RenderViewHost doesn't have a live renderer, so just skip the unload
    // event and close the page.
    ClosePageIgnoringUnloadEvents();
  }
}

void RenderViewHost::ClosePageIgnoringUnloadEvents() {
  StopHangMonitorTimeout();
  is_waiting_for_beforeunload_ack_ = false;
  is_waiting_for_unload_ack_ = false;

  sudden_termination_allowed_ = true;
  delegate_->Close(this);
}

void RenderViewHost::SetHasPendingCrossSiteRequest(bool has_pending_request,
                                                   int request_id) {
  CrossSiteRequestManager::GetInstance()->SetHasPendingCrossSiteRequest(
      process()->id(), routing_id(), has_pending_request);
  pending_request_id_ = request_id;
}

int RenderViewHost::GetPendingRequestId() {
  return pending_request_id_;
}

void RenderViewHost::DragTargetDragEnter(
    const WebDropData& drop_data,
    const gfx::Point& client_pt,
    const gfx::Point& screen_pt,
    WebDragOperationsMask operations_allowed) {
  // Grant the renderer the ability to load the drop_data.
  ChildProcessSecurityPolicy* policy =
      ChildProcessSecurityPolicy::GetInstance();
  policy->GrantRequestURL(process()->id(), drop_data.url);
  for (std::vector<string16>::const_iterator iter(drop_data.filenames.begin());
       iter != drop_data.filenames.end(); ++iter) {
    FilePath path = FilePath::FromWStringHack(UTF16ToWideHack(*iter));
    policy->GrantRequestURL(process()->id(),
                            net::FilePathToFileURL(path));
    policy->GrantReadFile(process()->id(), path);

    // Allow dragged directories to be enumerated by the child process.
    // Note that we can't tell a file from a directory at this point.
    policy->GrantReadDirectory(process()->id(), path);
  }
  Send(new DragMsg_TargetDragEnter(routing_id(), drop_data, client_pt,
                                   screen_pt, operations_allowed));
}

void RenderViewHost::DragTargetDragOver(
    const gfx::Point& client_pt, const gfx::Point& screen_pt,
    WebDragOperationsMask operations_allowed) {
  Send(new DragMsg_TargetDragOver(routing_id(), client_pt, screen_pt,
                                  operations_allowed));
}

void RenderViewHost::DragTargetDragLeave() {
  Send(new DragMsg_TargetDragLeave(routing_id()));
}

void RenderViewHost::DragTargetDrop(
    const gfx::Point& client_pt, const gfx::Point& screen_pt) {
  Send(new DragMsg_TargetDrop(routing_id(), client_pt, screen_pt));
}

void RenderViewHost::ExecuteJavascriptInWebFrame(
    const string16& frame_xpath,
    const string16& jscript) {
  Send(new ViewMsg_ScriptEvalRequest(routing_id(), frame_xpath, jscript,
                                     0, false));
}

int RenderViewHost::ExecuteJavascriptInWebFrameNotifyResult(
    const string16& frame_xpath,
    const string16& jscript) {
  static int next_id = 1;
  Send(new ViewMsg_ScriptEvalRequest(routing_id(), frame_xpath, jscript,
                                     next_id, true));
  return next_id++;
}

void RenderViewHost::Undo() {
  Send(new ViewMsg_Undo(routing_id()));
  UserMetrics::RecordAction(UserMetricsAction("Undo"));
}

void RenderViewHost::Redo() {
  Send(new ViewMsg_Redo(routing_id()));
  UserMetrics::RecordAction(UserMetricsAction("Redo"));
}

void RenderViewHost::Cut() {
  Send(new ViewMsg_Cut(routing_id()));
  UserMetrics::RecordAction(UserMetricsAction("Cut"));
}

void RenderViewHost::Copy() {
  Send(new ViewMsg_Copy(routing_id()));
  UserMetrics::RecordAction(UserMetricsAction("Copy"));
}

void RenderViewHost::CopyToFindPboard() {
#if defined(OS_MACOSX)
  // Windows/Linux don't have the concept of a find pasteboard.
  Send(new ViewMsg_CopyToFindPboard(routing_id()));
  UserMetrics::RecordAction(UserMetricsAction("CopyToFindPboard"));
#endif
}

void RenderViewHost::Paste() {
  Send(new ViewMsg_Paste(routing_id()));
  UserMetrics::RecordAction(UserMetricsAction("Paste"));
}

void RenderViewHost::Delete() {
  Send(new ViewMsg_Delete(routing_id()));
  UserMetrics::RecordAction(UserMetricsAction("DeleteSelection"));
}

void RenderViewHost::SelectAll() {
  Send(new ViewMsg_SelectAll(routing_id()));
  UserMetrics::RecordAction(UserMetricsAction("SelectAll"));
}

void RenderViewHost::JavaScriptDialogClosed(IPC::Message* reply_msg,
                                            bool success,
                                            const string16& user_input) {
  process()->set_ignore_input_events(false);
  bool is_waiting =
      is_waiting_for_beforeunload_ack_ || is_waiting_for_unload_ack_;
  if (is_waiting)
    StartHangMonitorTimeout(TimeDelta::FromMilliseconds(kUnloadTimeoutMS));

  ViewHostMsg_RunJavaScriptMessage::WriteReplyParams(reply_msg,
                                                     success, user_input);
  Send(reply_msg);

  // If we are waiting for an unload or beforeunload ack and the user has
  // suppressed messages, kill the tab immediately; a page that's spamming
  // alerts in onbeforeunload is presumably malicious, so there's no point in
  // continuing to run its script and dragging out the process.
  // This must be done after sending the reply since RenderView can't close
  // correctly while waiting for a response.
  if (is_waiting && are_javascript_messages_suppressed_)
    delegate_->RendererUnresponsive(this, is_waiting);
}

void RenderViewHost::DragSourceEndedAt(
    int client_x, int client_y, int screen_x, int screen_y,
    WebDragOperation operation) {
  Send(new DragMsg_SourceEndedOrMoved(
      routing_id(),
      gfx::Point(client_x, client_y),
      gfx::Point(screen_x, screen_y),
      true, operation));
}

void RenderViewHost::DragSourceMovedTo(
    int client_x, int client_y, int screen_x, int screen_y) {
  Send(new DragMsg_SourceEndedOrMoved(
      routing_id(),
      gfx::Point(client_x, client_y),
      gfx::Point(screen_x, screen_y),
      false, WebDragOperationNone));
}

void RenderViewHost::DragSourceSystemDragEnded() {
  Send(new DragMsg_SourceSystemDragEnded(routing_id()));
}

void RenderViewHost::AllowBindings(int bindings_flags) {
  DCHECK(!renderer_initialized_);
  enabled_bindings_ |= bindings_flags;
}

void RenderViewHost::SetWebUIProperty(const std::string& name,
                                      const std::string& value) {
  DCHECK(BindingsPolicy::is_web_ui_enabled(enabled_bindings_));
  Send(new ViewMsg_SetWebUIProperty(routing_id(), name, value));
}

void RenderViewHost::GotFocus() {
  RenderWidgetHost::GotFocus();  // Notifies the renderer it got focus.

  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view)
    view->GotFocus();
}

void RenderViewHost::LostCapture() {
  RenderWidgetHost::LostCapture();

  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view)
    view->LostCapture();
}

void RenderViewHost::SetInitialFocus(bool reverse) {
  Send(new ViewMsg_SetInitialFocus(routing_id(), reverse));
}

void RenderViewHost::FilesSelectedInChooser(
    const std::vector<FilePath>& files) {
  // Grant the security access requested to the given files.
  for (std::vector<FilePath>::const_iterator file = files.begin();
       file != files.end(); ++file) {
    ChildProcessSecurityPolicy::GetInstance()->GrantReadFile(
        process()->id(), *file);
  }
  Send(new ViewMsg_RunFileChooserResponse(routing_id(), files));
}

void RenderViewHost::DirectoryEnumerationFinished(
    int request_id,
    const std::vector<FilePath>& files) {
  // Grant the security access requested to the given files.
  for (std::vector<FilePath>::const_iterator file = files.begin();
       file != files.end(); ++file) {
    ChildProcessSecurityPolicy::GetInstance()->GrantReadFile(
        process()->id(), *file);
  }
  Send(new ViewMsg_EnumerateDirectoryResponse(routing_id(),
                                              request_id,
                                              files));
}

void RenderViewHost::LoadStateChanged(const GURL& url,
                                      net::LoadState load_state,
                                      uint64 upload_position,
                                      uint64 upload_size) {
  delegate_->LoadStateChanged(url, load_state, upload_position, upload_size);
}

bool RenderViewHost::SuddenTerminationAllowed() const {
  return sudden_termination_allowed_ || process()->sudden_termination_allowed();
}

///////////////////////////////////////////////////////////////////////////////
// RenderViewHost, IPC message handlers:

bool RenderViewHost::OnMessageReceived(const IPC::Message& msg) {
  if (!BrowserMessageFilter::CheckCanDispatchOnUI(msg, this))
    return true;

  // Filter out most IPC messages if this renderer is swapped out.
  // We still want to certain ACKs to keep our state consistent.
  if (is_swapped_out_)
    if (!content::SwappedOutMessages::CanHandleWhileSwappedOut(msg))
      return true;

  ObserverListBase<RenderViewHostObserver>::Iterator it(observers_);
  RenderViewHostObserver* observer;
  while ((observer = it.GetNext()) != NULL) {
    if (observer->OnMessageReceived(msg))
      return true;
  }

  if (delegate_->OnMessageReceived(msg))
    return true;

  bool handled = true;
  bool msg_is_ok = true;
  IPC_BEGIN_MESSAGE_MAP_EX(RenderViewHost, msg, msg_is_ok)
    IPC_MESSAGE_HANDLER(ViewHostMsg_ShowView, OnMsgShowView)
    IPC_MESSAGE_HANDLER(ViewHostMsg_ShowWidget, OnMsgShowWidget)
    IPC_MESSAGE_HANDLER(ViewHostMsg_ShowFullscreenWidget,
                        OnMsgShowFullscreenWidget)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(ViewHostMsg_RunModal, OnMsgRunModal)
    IPC_MESSAGE_HANDLER(ViewHostMsg_RenderViewReady, OnMsgRenderViewReady)
    IPC_MESSAGE_HANDLER(ViewHostMsg_RenderViewGone, OnMsgRenderViewGone)
    IPC_MESSAGE_HANDLER_GENERIC(ViewHostMsg_FrameNavigate, OnMsgNavigate(msg))
    IPC_MESSAGE_HANDLER(ViewHostMsg_UpdateState, OnMsgUpdateState)
    IPC_MESSAGE_HANDLER(ViewHostMsg_UpdateTitle, OnMsgUpdateTitle)
    IPC_MESSAGE_HANDLER(ViewHostMsg_UpdateEncoding, OnMsgUpdateEncoding)
    IPC_MESSAGE_HANDLER(ViewHostMsg_UpdateTargetURL, OnMsgUpdateTargetURL)
    IPC_MESSAGE_HANDLER(ViewHostMsg_UpdateInspectorSetting,
                        OnUpdateInspectorSetting)
    IPC_MESSAGE_HANDLER(ViewHostMsg_Close, OnMsgClose)
    IPC_MESSAGE_HANDLER(ViewHostMsg_RequestMove, OnMsgRequestMove)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DidStartLoading, OnMsgDidStartLoading)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DidStopLoading, OnMsgDidStopLoading)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DidChangeLoadProgress,
                        OnMsgDidChangeLoadProgress)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DocumentAvailableInMainFrame,
                        OnMsgDocumentAvailableInMainFrame)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DocumentOnLoadCompletedInMainFrame,
                        OnMsgDocumentOnLoadCompletedInMainFrame)
    IPC_MESSAGE_HANDLER(ViewHostMsg_ContextMenu, OnMsgContextMenu)
    IPC_MESSAGE_HANDLER(ViewHostMsg_OpenURL, OnMsgOpenURL)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DidContentsPreferredSizeChange,
                        OnMsgDidContentsPreferredSizeChange)
    IPC_MESSAGE_HANDLER(ViewHostMsg_SetTooltipText, OnMsgSetTooltipText)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(ViewHostMsg_RunJavaScriptMessage,
                                    OnMsgRunJavaScriptMessage)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(ViewHostMsg_RunBeforeUnloadConfirm,
                                    OnMsgRunBeforeUnloadConfirm)
    IPC_MESSAGE_HANDLER(DragHostMsg_StartDragging, OnMsgStartDragging)
    IPC_MESSAGE_HANDLER(DragHostMsg_UpdateDragCursor, OnUpdateDragCursor)
    IPC_MESSAGE_HANDLER(ViewHostMsg_TakeFocus, OnTakeFocus)
    IPC_MESSAGE_HANDLER(ViewHostMsg_AddMessageToConsole, OnAddMessageToConsole)
    IPC_MESSAGE_HANDLER(ViewHostMsg_ShouldClose_ACK, OnMsgShouldCloseACK)
    IPC_MESSAGE_HANDLER(ViewHostMsg_ClosePage_ACK, OnMsgClosePageACK)
    IPC_MESSAGE_HANDLER(ViewHostMsg_SelectionChanged, OnMsgSelectionChanged)
    IPC_MESSAGE_HANDLER(ViewHostMsg_AccessibilityNotifications,
                        OnAccessibilityNotifications)
    IPC_MESSAGE_HANDLER(ViewHostMsg_ScriptEvalResponse, OnScriptEvalResponse)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DidZoomURL, OnDidZoomURL)
#if defined(OS_MACOSX)
    IPC_MESSAGE_HANDLER(ViewHostMsg_ShowPopup, OnMsgShowPopup)
#endif
    // Have the super handle all other messages.
    IPC_MESSAGE_UNHANDLED(handled = RenderWidgetHost::OnMessageReceived(msg))
    // NOTE: Do not add a message handler that just calls the delegate!
    // Dispatch the message directly there instead.
  IPC_END_MESSAGE_MAP_EX()

  if (!msg_is_ok) {
    // The message had a handler, but its de-serialization failed.
    // Kill the renderer.
    UserMetrics::RecordAction(UserMetricsAction("BadMessageTerminate_RVH"));
    process()->ReceivedBadMessage();
  }

  return handled;
}

void RenderViewHost::Shutdown() {
  // If we are being run modally (see RunModal), then we need to cleanup.
  if (run_modal_reply_msg_) {
    Send(run_modal_reply_msg_);
    run_modal_reply_msg_ = NULL;
  }

  RenderWidgetHost::Shutdown();
}

bool RenderViewHost::IsRenderView() const {
  return true;
}

void RenderViewHost::CreateNewWindow(
    int route_id,
    const ViewHostMsg_CreateWindow_Params& params) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (!view)
    return;

  view->CreateNewWindow(route_id, params);
}

void RenderViewHost::CreateNewWidget(int route_id,
                                     WebKit::WebPopupType popup_type) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view)
    view->CreateNewWidget(route_id, popup_type);
}

void RenderViewHost::CreateNewFullscreenWidget(int route_id) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view)
    view->CreateNewFullscreenWidget(route_id);
}

void RenderViewHost::OnMsgShowView(int route_id,
                                   WindowOpenDisposition disposition,
                                   const gfx::Rect& initial_pos,
                                   bool user_gesture) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view) {
    if (!is_swapped_out_)
      view->ShowCreatedWindow(route_id, disposition, initial_pos, user_gesture);
    Send(new ViewMsg_Move_ACK(route_id));
  }
}

void RenderViewHost::OnMsgShowWidget(int route_id,
                                     const gfx::Rect& initial_pos) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view) {
    if (!is_swapped_out_)
      view->ShowCreatedWidget(route_id, initial_pos);
    Send(new ViewMsg_Move_ACK(route_id));
  }
}

void RenderViewHost::OnMsgShowFullscreenWidget(int route_id) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view) {
    if (!is_swapped_out_)
      view->ShowCreatedFullscreenWidget(route_id);
    Send(new ViewMsg_Move_ACK(route_id));
  }
}

void RenderViewHost::OnMsgRunModal(IPC::Message* reply_msg) {
  DCHECK(!run_modal_reply_msg_);
  run_modal_reply_msg_ = reply_msg;

  // TODO(darin): Bug 1107929: Need to inform our delegate to show this view in
  // an app-modal fashion.
}

void RenderViewHost::OnMsgRenderViewReady() {
  render_view_termination_status_ = base::TERMINATION_STATUS_STILL_RUNNING;
  WasResized();
  delegate_->RenderViewReady(this);
}

void RenderViewHost::OnMsgRenderViewGone(int status, int exit_code) {
  // Keep the termination status so we can get at it later when we
  // need to know why it died.
  render_view_termination_status_ =
      static_cast<base::TerminationStatus>(status);

  // Our base class RenderWidgetHost needs to reset some stuff.
  RendererExited(render_view_termination_status_, exit_code);

  delegate_->RenderViewGone(this,
                            static_cast<base::TerminationStatus>(status),
                            exit_code);
}

// Called when the renderer navigates.  For every frame loaded, we'll get this
// notification containing parameters identifying the navigation.
//
// Subframes are identified by the page transition type.  For subframes loaded
// as part of a wider page load, the page_id will be the same as for the top
// level frame.  If the user explicitly requests a subframe navigation, we will
// get a new page_id because we need to create a new navigation entry for that
// action.
void RenderViewHost::OnMsgNavigate(const IPC::Message& msg) {
  // Read the parameters out of the IPC message directly to avoid making another
  // copy when we filter the URLs.
  void* iter = NULL;
  ViewHostMsg_FrameNavigate_Params validated_params;
  if (!IPC::ParamTraits<ViewHostMsg_FrameNavigate_Params>::
      Read(&msg, &iter, &validated_params))
    return;

  // If we're waiting for a cross-site beforeunload ack from this renderer and
  // we receive a Navigate message from the main frame, then the renderer was
  // navigating already and sent it before hearing the ViewMsg_Stop message.
  // We do not want to cancel the pending navigation in this case, since the
  // old page will soon be stopped.  Instead, treat this as a beforeunload ack
  // to allow the pending navigation to continue.
  if (is_waiting_for_beforeunload_ack_ &&
      unload_ack_is_for_cross_site_transition_ &&
      PageTransition::IsMainFrame(validated_params.transition)) {
    OnMsgShouldCloseACK(true);
    return;
  }

  // If we're waiting for an unload ack from this renderer and we receive a
  // Navigate message, then the renderer was navigating before it received the
  // unload request.  It will either respond to the unload request soon or our
  // timer will expire.  Either way, we should ignore this message, because we
  // have already committed to closing this renderer.
  if (is_waiting_for_unload_ack_)
    return;

  const int renderer_id = process()->id();
  ChildProcessSecurityPolicy* policy =
      ChildProcessSecurityPolicy::GetInstance();
  // Without this check, an evil renderer can trick the browser into creating
  // a navigation entry for a banned URL.  If the user clicks the back button
  // followed by the forward button (or clicks reload, or round-trips through
  // session restore, etc), we'll think that the browser commanded the
  // renderer to load the URL and grant the renderer the privileges to request
  // the URL.  To prevent this attack, we block the renderer from inserting
  // banned URLs into the navigation controller in the first place.
  FilterURL(policy, renderer_id, &validated_params.url);
  FilterURL(policy, renderer_id, &validated_params.referrer);
  for (std::vector<GURL>::iterator it(validated_params.redirects.begin());
      it != validated_params.redirects.end(); ++it) {
    FilterURL(policy, renderer_id, &(*it));
  }
  FilterURL(policy, renderer_id, &validated_params.searchable_form_url);
  FilterURL(policy, renderer_id, &validated_params.password_form.origin);
  FilterURL(policy, renderer_id, &validated_params.password_form.action);

  delegate_->DidNavigate(this, validated_params);
}

void RenderViewHost::OnMsgUpdateState(int32 page_id,
                                      const std::string& state) {
  delegate_->UpdateState(this, page_id, state);
}

void RenderViewHost::OnMsgUpdateTitle(int32 page_id,
                                      const std::wstring& title) {
  if (title.length() > content::kMaxTitleChars) {
    NOTREACHED() << "Renderer sent too many characters in title.";
    return;
  }
  delegate_->UpdateTitle(this, page_id, title);
}

void RenderViewHost::OnMsgUpdateEncoding(const std::string& encoding_name) {
  delegate_->UpdateEncoding(this, encoding_name);
}

void RenderViewHost::OnMsgUpdateTargetURL(int32 page_id,
                                          const GURL& url) {
  if (!is_swapped_out_)
    delegate_->UpdateTargetURL(page_id, url);

  // Send a notification back to the renderer that we are ready to
  // receive more target urls.
  Send(new ViewMsg_UpdateTargetURL_ACK(routing_id()));
}

void RenderViewHost::OnUpdateInspectorSetting(
    const std::string& key, const std::string& value) {
  delegate_->UpdateInspectorSetting(key, value);
}

void RenderViewHost::OnMsgClose() {
  // If the renderer is telling us to close, it has already run the unload
  // events, and we can take the fast path.
  ClosePageIgnoringUnloadEvents();
}

void RenderViewHost::OnMsgRequestMove(const gfx::Rect& pos) {
  if (!is_swapped_out_)
    delegate_->RequestMove(pos);
  Send(new ViewMsg_Move_ACK(routing_id()));
}

void RenderViewHost::OnMsgDidStartLoading() {
  delegate_->DidStartLoading();
}

void RenderViewHost::OnMsgDidStopLoading() {
  delegate_->DidStopLoading();
}

void RenderViewHost::OnMsgDidChangeLoadProgress(double load_progress) {
  delegate_->DidChangeLoadProgress(load_progress);
}

void RenderViewHost::OnMsgDocumentAvailableInMainFrame() {
  delegate_->DocumentAvailableInMainFrame(this);
}

void RenderViewHost::OnMsgDocumentOnLoadCompletedInMainFrame(int32 page_id) {
  delegate_->DocumentOnLoadCompletedInMainFrame(this, page_id);
}

void RenderViewHost::OnMsgContextMenu(const ContextMenuParams& params) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (!view)
    return;

  // Validate the URLs in |params|.  If the renderer can't request the URLs
  // directly, don't show them in the context menu.
  ContextMenuParams validated_params(params);
  int renderer_id = process()->id();
  ChildProcessSecurityPolicy* policy =
      ChildProcessSecurityPolicy::GetInstance();

  // We don't validate |unfiltered_link_url| so that this field can be used
  // when users want to copy the original link URL.
  FilterURL(policy, renderer_id, &validated_params.link_url);
  FilterURL(policy, renderer_id, &validated_params.src_url);
  FilterURL(policy, renderer_id, &validated_params.page_url);
  FilterURL(policy, renderer_id, &validated_params.frame_url);

  view->ShowContextMenu(validated_params);
}

void RenderViewHost::OnMsgOpenURL(const GURL& url,
                                  const GURL& referrer,
                                  WindowOpenDisposition disposition) {
  GURL validated_url(url);
  FilterURL(ChildProcessSecurityPolicy::GetInstance(),
            process()->id(), &validated_url);

  delegate_->RequestOpenURL(validated_url, referrer, disposition);
}

void RenderViewHost::OnMsgDidContentsPreferredSizeChange(
    const gfx::Size& new_size) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (!view)
    return;
  view->UpdatePreferredSize(new_size);
}

void RenderViewHost::OnMsgSetTooltipText(
    const std::wstring& tooltip_text,
    WebTextDirection text_direction_hint) {
  // First, add directionality marks around tooltip text if necessary.
  // A naive solution would be to simply always wrap the text. However, on
  // windows, Unicode directional embedding characters can't be displayed on
  // systems that lack RTL fonts and are instead displayed as empty squares.
  //
  // To get around this we only wrap the string when we deem it necessary i.e.
  // when the locale direction is different than the tooltip direction hint.
  //
  // Currently, we use element's directionality as the tooltip direction hint.
  // An alternate solution would be to set the overall directionality based on
  // trying to detect the directionality from the tooltip text rather than the
  // element direction.  One could argue that would be a preferable solution
  // but we use the current approach to match Fx & IE's behavior.
  string16 wrapped_tooltip_text = WideToUTF16(tooltip_text);
  if (!tooltip_text.empty()) {
    if (text_direction_hint == WebKit::WebTextDirectionLeftToRight) {
      // Force the tooltip to have LTR directionality.
      wrapped_tooltip_text =
          base::i18n::GetDisplayStringInLTRDirectionality(wrapped_tooltip_text);
    } else if (text_direction_hint == WebKit::WebTextDirectionRightToLeft &&
               !base::i18n::IsRTL()) {
      // Force the tooltip to have RTL directionality.
      base::i18n::WrapStringWithRTLFormatting(&wrapped_tooltip_text);
    }
  }
  if (view())
    view()->SetTooltipText(UTF16ToWide(wrapped_tooltip_text));
}

void RenderViewHost::OnMsgSelectionChanged(const std::string& text,
                                           const ui::Range& range) {
  if (view())
    view()->SelectionChanged(text, range);
}

void RenderViewHost::OnMsgRunJavaScriptMessage(
    const string16& message,
    const string16& default_prompt,
    const GURL& frame_url,
    const int flags,
    IPC::Message* reply_msg) {
  // While a JS message dialog is showing, tabs in the same process shouldn't
  // process input events.
  process()->set_ignore_input_events(true);
  StopHangMonitorTimeout();
  delegate_->RunJavaScriptMessage(this, message, default_prompt, frame_url,
                                  flags, reply_msg,
                                  &are_javascript_messages_suppressed_);
}

void RenderViewHost::OnMsgRunBeforeUnloadConfirm(const GURL& frame_url,
                                                 const string16& message,
                                                 IPC::Message* reply_msg) {
  // While a JS before unload dialog is showing, tabs in the same process
  // shouldn't process input events.
  process()->set_ignore_input_events(true);
  StopHangMonitorTimeout();
  delegate_->RunBeforeUnloadConfirm(this, message, reply_msg);
}

void RenderViewHost::OnMsgStartDragging(
    const WebDropData& drop_data,
    WebDragOperationsMask drag_operations_mask,
    const SkBitmap& image,
    const gfx::Point& image_offset) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (!view)
    return;

  GURL drag_url = drop_data.url;
  GURL html_base_url = drop_data.html_base_url;

  ChildProcessSecurityPolicy* policy =
      ChildProcessSecurityPolicy::GetInstance();
  FilterURL(policy, process()->id(), &drag_url);
  FilterURL(policy, process()->id(), &html_base_url);

  if (drag_url != drop_data.url || html_base_url != drop_data.html_base_url) {
    WebDropData drop_data_copy = drop_data;
    drop_data_copy.url = drag_url;
    drop_data_copy.html_base_url = html_base_url;
    view->StartDragging(drop_data_copy, drag_operations_mask, image,
                        image_offset);
  } else {
    view->StartDragging(drop_data, drag_operations_mask, image, image_offset);
  }
}

void RenderViewHost::OnUpdateDragCursor(WebDragOperation current_op) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view)
    view->UpdateDragCursor(current_op);
}

void RenderViewHost::OnTakeFocus(bool reverse) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view)
    view->TakeFocus(reverse);
}

void RenderViewHost::OnAddMessageToConsole(int32 level,
                                           const std::wstring& message,
                                           int32 line_no,
                                           const std::wstring& source_id) {
  // Pass through log level only on WebUI pages to limit console spew.
  int32 resolved_level =
      BindingsPolicy::is_web_ui_enabled(enabled_bindings_) ? level : 0;

  logging::LogMessage("CONSOLE", line_no, resolved_level).stream() << "\"" <<
      message << "\", source: " << source_id << " (" << line_no << ")";
}

void RenderViewHost::AddObserver(RenderViewHostObserver* observer) {
  observers_.AddObserver(observer);
}

void RenderViewHost::RemoveObserver(RenderViewHostObserver* observer) {
  observers_.RemoveObserver(observer);
}

bool RenderViewHost::PreHandleKeyboardEvent(
    const NativeWebKeyboardEvent& event, bool* is_keyboard_shortcut) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  return view && view->PreHandleKeyboardEvent(event, is_keyboard_shortcut);
}

void RenderViewHost::UnhandledKeyboardEvent(
    const NativeWebKeyboardEvent& event) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view)
    view->HandleKeyboardEvent(event);
}

void RenderViewHost::OnUserGesture() {
  delegate_->OnUserGesture();
}

void RenderViewHost::OnMsgShouldCloseACK(bool proceed) {
  StopHangMonitorTimeout();
  // If this renderer navigated while the beforeunload request was in flight, we
  // may have cleared this state in OnMsgNavigate, in which case we can ignore
  // this message.
  if (!is_waiting_for_beforeunload_ack_ || is_swapped_out_)
    return;

  is_waiting_for_beforeunload_ack_ = false;

  RenderViewHostDelegate::RendererManagement* management_delegate =
      delegate_->GetRendererManagementDelegate();
  if (management_delegate) {
    management_delegate->ShouldClosePage(
        unload_ack_is_for_cross_site_transition_, proceed);
  }

  // If canceled, notify the delegate to cancel its pending navigation entry.
  if (!proceed)
    delegate_->DidCancelLoading();
}

void RenderViewHost::OnMsgClosePageACK() {
  ClosePageIgnoringUnloadEvents();
}

void RenderViewHost::NotifyRendererUnresponsive() {
  delegate_->RendererUnresponsive(
      this, is_waiting_for_beforeunload_ack_ || is_waiting_for_unload_ack_);
}

void RenderViewHost::NotifyRendererResponsive() {
  delegate_->RendererResponsive(this);
}

void RenderViewHost::OnMsgFocus() {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view)
    view->Activate();
}

void RenderViewHost::OnMsgBlur() {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view)
    view->Deactivate();
}

void RenderViewHost::ForwardMouseEvent(
    const WebKit::WebMouseEvent& mouse_event) {

  // We make a copy of the mouse event because
  // RenderWidgetHost::ForwardMouseEvent will delete |mouse_event|.
  WebKit::WebMouseEvent event_copy(mouse_event);
  RenderWidgetHost::ForwardMouseEvent(event_copy);

  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view) {
    switch (event_copy.type) {
      case WebInputEvent::MouseMove:
        view->HandleMouseMove();
        break;
      case WebInputEvent::MouseLeave:
        view->HandleMouseLeave();
        break;
      case WebInputEvent::MouseDown:
        view->HandleMouseDown();
        break;
      case WebInputEvent::MouseWheel:
        if (ignore_input_events())
          delegate_->OnIgnoredUIEvent();
        break;
      case WebInputEvent::MouseUp:
        view->HandleMouseUp();
      default:
        // For now, we don't care about the rest.
        break;
    }
  }
}

void RenderViewHost::OnMouseActivate() {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view)
    view->HandleMouseActivate();
}

void RenderViewHost::ForwardKeyboardEvent(
    const NativeWebKeyboardEvent& key_event) {
  if (ignore_input_events()) {
    if (key_event.type == WebInputEvent::RawKeyDown)
      delegate_->OnIgnoredUIEvent();
    return;
  }
  RenderWidgetHost::ForwardKeyboardEvent(key_event);
}

#if defined(OS_MACOSX)
void RenderViewHost::DidSelectPopupMenuItem(int selected_index) {
  Send(new ViewMsg_SelectPopupMenuItem(routing_id(), selected_index));
}

void RenderViewHost::DidCancelPopupMenu() {
  Send(new ViewMsg_SelectPopupMenuItem(routing_id(), -1));
}
#endif

void RenderViewHost::FilterURL(ChildProcessSecurityPolicy* policy,
                               int renderer_id,
                               GURL* url) {
  if (!url->is_valid())
    return;  // We don't need to block invalid URLs.

  if (url->SchemeIs(chrome::kAboutScheme)) {
    // The renderer treats all URLs in the about: scheme as being about:blank.
    // Canonicalize about: URLs to about:blank.
    *url = GURL(chrome::kAboutBlankURL);
  }

  if (!policy->CanRequestURL(renderer_id, *url)) {
    // If this renderer is not permitted to request this URL, we invalidate the
    // URL.  This prevents us from storing the blocked URL and becoming confused
    // later.
    VLOG(1) << "Blocked URL " << url->spec();
    *url = GURL();
  }
}

void RenderViewHost::OnAccessibilityNotifications(
    const std::vector<ViewHostMsg_AccessibilityNotification_Params>& params) {
  if (view() && !is_swapped_out_)
    view()->OnAccessibilityNotifications(params);

  if (!params.empty()) {
    for (unsigned i = 0; i < params.size(); i++) {
      const ViewHostMsg_AccessibilityNotification_Params& param = params[i];

      if (param.notification_type ==
              ViewHostMsg_AccessibilityNotification_Type::
                  NOTIFICATION_TYPE_LOAD_COMPLETE) {
        // TODO(ctguil): Remove when mac processes OnAccessibilityNotifications.
        if (view())
          view()->UpdateAccessibilityTree(param.acc_obj);

        if (save_accessibility_tree_for_testing_)
          accessibility_tree_ = param.acc_obj;
      }
    }

    NotificationService::current()->Notify(
        NotificationType::RENDER_VIEW_HOST_ACCESSIBILITY_TREE_UPDATED,
        Source<RenderViewHost>(this),
        NotificationService::NoDetails());
  }

  Send(new ViewMsg_AccessibilityNotifications_ACK(routing_id()));
}

void RenderViewHost::OnScriptEvalResponse(int id, const ListValue& result) {
  Value* result_value;
  if (!result.Get(0, &result_value)) {
    // Programming error or rogue renderer.
    NOTREACHED() << "Got bad arguments for OnScriptEvalResponse";
    return;
  }
  std::pair<int, Value*> details(id, result_value);
  NotificationService::current()->Notify(
      NotificationType::EXECUTE_JAVASCRIPT_RESULT,
      Source<RenderViewHost>(this),
      Details<std::pair<int, Value*> >(&details));
}

void RenderViewHost::OnDidZoomURL(double zoom_level,
                                  bool remember,
                                  const GURL& url) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  HostZoomMap* host_zoom_map = process()->profile()->GetHostZoomMap();
  if (remember) {
    host_zoom_map->SetZoomLevel(net::GetHostOrSpecFromURL(url), zoom_level);
    // Notify renderers from this profile.
    for (RenderProcessHost::iterator i(RenderProcessHost::AllHostsIterator());
         !i.IsAtEnd(); i.Advance()) {
      RenderProcessHost* render_process_host = i.GetCurrentValue();
      if (render_process_host->profile() == process()->profile()) {
        render_process_host->Send(
            new ViewMsg_SetZoomLevelForCurrentURL(url, zoom_level));
      }
    }
  } else {
    host_zoom_map->SetTemporaryZoomLevel(
        process()->id(), routing_id(), zoom_level);
  }
}

#if defined(OS_MACOSX)
void RenderViewHost::OnMsgShowPopup(
    const ViewHostMsg_ShowPopup_Params& params) {
  RenderViewHostDelegate::View* view = delegate_->GetViewDelegate();
  if (view) {
    view->ShowPopupMenu(params.bounds,
                        params.item_height,
                        params.item_font_size,
                        params.selected_item,
                        params.popup_items,
                        params.right_aligned);
  }
}
#endif
