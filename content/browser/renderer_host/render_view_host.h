// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_RENDER_VIEW_HOST_H_
#define CONTENT_BROWSER_RENDERER_HOST_RENDER_VIEW_HOST_H_
#pragma once

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/process_util.h"
#include "content/browser/renderer_host/render_widget_host.h"
#include "content/common/window_container_type.h"
#include "net/base/load_states.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebConsoleMessage.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDragOperation.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPopupType.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebTextDirection.h"
#include "webkit/glue/webaccessibility.h"
#include "webkit/glue/window_open_disposition.h"

class ChildProcessSecurityPolicy;
class FilePath;
class GURL;
class ListValue;
class RenderViewHostDelegate;
class RenderViewHostObserver;
class SessionStorageNamespace;
class SiteInstance;
class SkBitmap;
class ViewMsg_Navigate;
struct ContextMenuParams;
struct MediaPlayerAction;
struct ViewHostMsg_AccessibilityNotification_Params;
struct ViewHostMsg_CreateWindow_Params;
struct ViewHostMsg_ShowPopup_Params;
struct ViewMsg_Navigate_Params;
struct WebDropData;
struct UserMetricsAction;

namespace gfx {
class Point;
}  // namespace gfx

namespace ui {
class Range;
}  // namespace ui

namespace webkit_glue {
struct WebAccessibility;
}  // namespace webkit_glue

namespace WebKit {
struct WebMediaPlayerAction;
}  // namespace WebKit

namespace net {
class URLRequestContextGetter;
}

//
// RenderViewHost
//
//  A RenderViewHost is responsible for creating and talking to a RenderView
//  object in a child process. It exposes a high level API to users, for things
//  like loading pages, adjusting the display and other browser functionality,
//  which it translates into IPC messages sent over the IPC channel with the
//  RenderView. It responds to all IPC messages sent by that RenderView and
//  cracks them, calling a delegate object back with higher level types where
//  possible.
//
//  The intent of this class is to provide a view-agnostic communication
//  conduit with a renderer. This is so we can build HTML views not only as
//  TabContents (see TabContents for an example) but also as views, etc.
//
//  The exact API of this object needs to be more thoroughly designed. Right
//  now it mimics what TabContents exposed, which is a fairly large API and may
//  contain things that are not relevant to a common subset of views. See also
//  the comment in render_view_host_delegate.h about the size and scope of the
//  delegate API.
//
//  Right now, the concept of page navigation (both top level and frame) exists
//  in the TabContents still, so if you instantiate one of these elsewhere, you
//  will not be able to traverse pages back and forward. We need to determine
//  if we want to bring that and other functionality down into this object so
//  it can be shared by others.
//
class RenderViewHost : public RenderWidgetHost {
 public:
  // Returns the RenderViewHost given its ID and the ID of its render process.
  // Returns NULL if the IDs do not correspond to a live RenderViewHost.
  static RenderViewHost* FromID(int render_process_id, int render_view_id);

  // routing_id could be a valid route id, or it could be MSG_ROUTING_NONE, in
  // which case RenderWidgetHost will create a new one.
  //
  // The session storage namespace parameter allows multiple render views and
  // tab contentses to share the same session storage (part of the WebStorage
  // spec) space. This is useful when restoring tabs, but most callers should
  // pass in NULL which will cause a new SessionStorageNamespace to be created.
  RenderViewHost(SiteInstance* instance,
                 RenderViewHostDelegate* delegate,
                 int routing_id,
                 SessionStorageNamespace* session_storage_namespace);
  virtual ~RenderViewHost();

  SiteInstance* site_instance() const { return instance_; }
  RenderViewHostDelegate* delegate() const { return delegate_; }
  void set_delegate(RenderViewHostDelegate* d) {
    CHECK(d);  // http://crbug.com/82827
    delegate_ = d;
  }

  // Set up the RenderView child process. Virtual because it is overridden by
  // TestRenderViewHost. If the |frame_name| parameter is non-empty, it is used
  // as the name of the new top-level frame.
  virtual bool CreateRenderView(const string16& frame_name);

  // Returns true if the RenderView is active and has not crashed. Virtual
  // because it is overridden by TestRenderViewHost.
  virtual bool IsRenderViewLive() const;

  base::TerminationStatus render_view_termination_status() const {
    return render_view_termination_status_;
  }

  // Send the renderer process the current preferences supplied by the
  // RenderViewHostDelegate.
  void SyncRendererPrefs();

  // Sends the given navigation message. Use this rather than sending it
  // yourself since this does the internal bookkeeping described below. This
  // function takes ownership of the provided message pointer.
  //
  // If a cross-site request is in progress, we may be suspended while waiting
  // for the onbeforeunload handler, so this function might buffer the message
  // rather than sending it.
  void Navigate(const ViewMsg_Navigate_Params& message);

  // Load the specified URL, this is a shortcut for Navigate().
  void NavigateToURL(const GURL& url);

  // Returns whether navigation messages are currently suspended for this
  // RenderViewHost.  Only true during a cross-site navigation, while waiting
  // for the onbeforeunload handler.
  bool are_navigations_suspended() const { return navigations_suspended_; }

  // Suspends (or unsuspends) any navigation messages from being sent from this
  // RenderViewHost.  This is called when a pending RenderViewHost is created
  // for a cross-site navigation, because we must suspend any navigations until
  // we hear back from the old renderer's onbeforeunload handler.  Note that it
  // is important that only one navigation event happen after calling this
  // method with |suspend| equal to true.  If |suspend| is false and there is
  // a suspended_nav_message_, this will send the message.  This function
  // should only be called to toggle the state; callers should check
  // are_navigations_suspended() first.
  void SetNavigationsSuspended(bool suspend);

  // Clears any suspended navigation state after a cross-site navigation is
  // canceled or suspended.  This is important if we later return to this
  // RenderViewHost.
  void CancelSuspendedNavigations();

  // Whether this RenderViewHost has been swapped out to be displayed by a
  // different process.
  bool is_swapped_out() const { return is_swapped_out_; }

  // Causes the renderer to invoke the onbeforeunload event handler.  The
  // result will be returned via ViewMsg_ShouldClose. See also ClosePage and
  // SwapOut, which fire the PageUnload event.
  //
  // Set bool for_cross_site_transition when this close is just for the current
  // RenderView in the case of a cross-site transition. False means we're
  // closing the entire tab.
  void FirePageBeforeUnload(bool for_cross_site_transition);

  // Tells the renderer that this RenderView is being swapped out for one in a
  // different renderer process.  It should run its unload handler and move to
  // a blank document.  The renderer should preserve the Frame object until it
  // exits, in case we come back.  The renderer can exit if it has no other
  // active RenderViews, but not until WasSwappedOut is called (when it is no
  // longer visible).
  //
  // Please see ViewMsg_SwapOut_Params in view_messages.h for a description
  // of the parameters.
  void SwapOut(int new_render_process_host_id, int new_request_id);

  // Called by ResourceDispatcherHost after the SwapOutACK is received.
  void OnSwapOutACK();

  // Called to notify the renderer that it has been visibly swapped out and
  // replaced by another RenderViewHost, after an earlier call to SwapOut.
  // It is now safe for the process to exit if there are no other active
  // RenderViews.
  void WasSwappedOut();

  // Causes the renderer to close the current page, including running its
  // onunload event handler.  A ClosePage_ACK message will be sent to the
  // ResourceDispatcherHost when it is finished.
  void ClosePage();

  // Close the page ignoring whether it has unload events registers.
  // This is called after the beforeunload and unload events have fired
  // and the user has agreed to continue with closing the page.
  void ClosePageIgnoringUnloadEvents();

  // Sets whether this RenderViewHost has an outstanding cross-site request,
  // for which another renderer will need to run an onunload event handler.
  // This is called before the first navigation event for this RenderViewHost,
  // and again after the corresponding OnCrossSiteResponse.
  void SetHasPendingCrossSiteRequest(bool has_pending_request, int request_id);

  // Returns the request_id for the pending cross-site request.
  // This is just needed in case the unload of the current page
  // hangs, in which case we need to swap to the pending RenderViewHost.
  int GetPendingRequestId();

  // D&d drop target messages that get sent to WebKit.
  void DragTargetDragEnter(const WebDropData& drop_data,
                           const gfx::Point& client_pt,
                           const gfx::Point& screen_pt,
                           WebKit::WebDragOperationsMask operations_allowed);
  void DragTargetDragOver(const gfx::Point& client_pt,
                          const gfx::Point& screen_pt,
                          WebKit::WebDragOperationsMask operations_allowed);
  void DragTargetDragLeave();
  void DragTargetDrop(const gfx::Point& client_pt,
                      const gfx::Point& screen_pt);

  // Runs some javascript within the context of a frame in the page.
  void ExecuteJavascriptInWebFrame(const string16& frame_xpath,
                                   const string16& jscript);

  // Runs some javascript within the context of a frame in the page. The result
  // is sent back via the notification EXECUTE_JAVASCRIPT_RESULT.
  int ExecuteJavascriptInWebFrameNotifyResult(const string16& frame_xpath,
                                              const string16& jscript);

  // Edit operations.
  void Undo();
  void Redo();
  void Cut();
  void Copy();
  void CopyToFindPboard();
  void Paste();
  void Delete();
  void SelectAll();

  // Notifies the RenderView that the JavaScript message that was shown was
  // closed by the user.
  void JavaScriptDialogClosed(IPC::Message* reply_msg,
                              bool success,
                              const string16& user_input);

  // Notifies the renderer that a a drag operation that it started has ended,
  // either in a drop or by being cancelled.
  void DragSourceEndedAt(
      int client_x, int client_y, int screen_x, int screen_y,
      WebKit::WebDragOperation operation);

  // Notifies the renderer that a drag and drop operation is in progress, with
  // droppable items positioned over the renderer's view.
  void DragSourceMovedTo(
      int client_x, int client_y, int screen_x, int screen_y);

  // Notifies the renderer that we're done with the drag and drop operation.
  // This allows the renderer to reset some state.
  void DragSourceSystemDragEnded();

  // Tell the render view to enable a set of javascript bindings. The argument
  // should be a combination of values from BindingsPolicy.
  void AllowBindings(int binding_flags);

  // Returns a bitwise OR of bindings types that have been enabled for this
  // RenderView. See BindingsPolicy for details.
  int enabled_bindings() const { return enabled_bindings_; }

  // Sets a property with the given name and value on the Web UI binding object.
  // Must call AllowWebUIBindings() on this renderer first.
  void SetWebUIProperty(const std::string& name, const std::string& value);

  // Tells the renderer view to focus the first (last if reverse is true) node.
  void SetInitialFocus(bool reverse);

  // Get html data by serializing all frames of current page with lists
  // which contain all resource links that have local copy.
  // The parameter links contain original URLs of all saved links.
  // The parameter local_paths contain corresponding local file paths of
  // all saved links, which matched with vector:links one by one.
  // The parameter local_directory_name is relative path of directory which
  // contain all saved auxiliary files included all sub frames and resouces.
  void GetSerializedHtmlDataForCurrentPageWithLocalLinks(
      const std::vector<GURL>& links,
      const std::vector<FilePath>& local_paths,
      const FilePath& local_directory_name);

  // Notifies the Listener that one or more files have been chosen by the user
  // from an Open File dialog for the form.
  void FilesSelectedInChooser(const std::vector<FilePath>& files);

  // Notifies the listener that a directory enumeration is complete.
  void DirectoryEnumerationFinished(int request_id,
                                    const std::vector<FilePath>& files);

  // Notifies the RenderViewHost that its load state changed.
  void LoadStateChanged(const GURL& url, net::LoadState load_state,
                        uint64 upload_position, uint64 upload_size);

  bool SuddenTerminationAllowed() const;
  void set_sudden_termination_allowed(bool enabled) {
    sudden_termination_allowed_ = enabled;
  }

  // Message the renderer that we should be counted as a new document and not
  // as a popup.
  void DisassociateFromPopupCount();

  // RenderWidgetHost public overrides.
  virtual void Shutdown();
  virtual bool IsRenderView() const;
  virtual bool OnMessageReceived(const IPC::Message& msg);
  virtual void GotFocus();
  virtual void LostCapture();
  virtual void ForwardMouseEvent(const WebKit::WebMouseEvent& mouse_event);
  virtual void OnMouseActivate();
  virtual void ForwardKeyboardEvent(const NativeWebKeyboardEvent& key_event);

  // Creates a new RenderView with the given route id.
  void CreateNewWindow(int route_id,
                       const ViewHostMsg_CreateWindow_Params& params);

  // Creates a new RenderWidget with the given route id.  |popup_type| indicates
  // if this widget is a popup and what kind of popup it is (select, autofill).
  void CreateNewWidget(int route_id, WebKit::WebPopupType popup_type);

  // Creates a full screen RenderWidget.
  void CreateNewFullscreenWidget(int route_id);

#if defined(OS_MACOSX)
  // Select popup menu related methods (for external popup menus).
  void DidSelectPopupMenuItem(int selected_index);
  void DidCancelPopupMenu();
#endif

#if defined(UNIT_TEST)
  // These functions shouldn't be necessary outside of testing.

  void set_save_accessibility_tree_for_testing(bool save) {
    save_accessibility_tree_for_testing_ = save;
  }

  const webkit_glue::WebAccessibility& accessibility_tree() {
    return accessibility_tree_;
  }

  bool is_waiting_for_unload_ack() { return is_waiting_for_unload_ack_; }
#endif

  // Checks that the given renderer can request |url|, if not it sets it to an
  // empty url.
  static void FilterURL(ChildProcessSecurityPolicy* policy,
                        int renderer_id,
                        GURL* url);

  // NOTE: Do not add functions that just send an IPC message that are called in
  // one or two places.  Have the caller send the IPC message directly.

 protected:
  friend class RenderViewHostObserver;

  // Add and remove observers for filtering IPC messages.  Clients must be sure
  // to remove the observer before they go away.
  void AddObserver(RenderViewHostObserver* observer);
  void RemoveObserver(RenderViewHostObserver* observer);

  // RenderWidgetHost protected overrides.
  virtual bool PreHandleKeyboardEvent(const NativeWebKeyboardEvent& event,
                                      bool* is_keyboard_shortcut);
  virtual void UnhandledKeyboardEvent(const NativeWebKeyboardEvent& event);
  virtual void OnUserGesture();
  virtual void NotifyRendererUnresponsive();
  virtual void NotifyRendererResponsive();
  virtual void OnMsgFocus();
  virtual void OnMsgBlur();

  // IPC message handlers.
  void OnMsgShowView(int route_id,
                     WindowOpenDisposition disposition,
                     const gfx::Rect& initial_pos,
                     bool user_gesture);
  void OnMsgShowWidget(int route_id, const gfx::Rect& initial_pos);
  void OnMsgShowFullscreenWidget(int route_id);
  void OnMsgRunModal(IPC::Message* reply_msg);
  void OnMsgRenderViewReady();
  void OnMsgRenderViewGone(int status, int error_code);
  void OnMsgNavigate(const IPC::Message& msg);
  void OnMsgUpdateState(int32 page_id,
                        const std::string& state);
  void OnMsgUpdateTitle(int32 page_id, const std::wstring& title);
  void OnMsgUpdateEncoding(const std::string& encoding);
  void OnMsgUpdateTargetURL(int32 page_id, const GURL& url);
  void OnMsgClose();
  void OnMsgRequestMove(const gfx::Rect& pos);
  void OnMsgDidStartLoading();
  void OnMsgDidStopLoading();
  void OnMsgDidChangeLoadProgress(double load_progress);
  void OnMsgDocumentAvailableInMainFrame();
  void OnMsgDocumentOnLoadCompletedInMainFrame(int32 page_id);
  void OnMsgContextMenu(const ContextMenuParams& params);
  void OnMsgOpenURL(const GURL& url, const GURL& referrer,
                    WindowOpenDisposition disposition);
  void OnMsgDidContentsPreferredSizeChange(const gfx::Size& new_size);
  void OnMsgSetTooltipText(const std::wstring& tooltip_text,
                           WebKit::WebTextDirection text_direction_hint);
  void OnMsgSelectionChanged(const std::string& text, const ui::Range& range);
  void OnMsgPasteFromSelectionClipboard();
  void OnMsgRunJavaScriptMessage(const string16& message,
                                 const string16& default_prompt,
                                 const GURL& frame_url,
                                 const int flags,
                                 IPC::Message* reply_msg);
  void OnMsgRunBeforeUnloadConfirm(const GURL& frame_url,
                                   const string16& message,
                                   IPC::Message* reply_msg);
  void OnMsgStartDragging(const WebDropData& drop_data,
                          WebKit::WebDragOperationsMask operations_allowed,
                          const SkBitmap& image,
                          const gfx::Point& image_offset);
  void OnUpdateDragCursor(WebKit::WebDragOperation drag_operation);
  void OnTakeFocus(bool reverse);
  void OnAddMessageToConsole(int32 level,
                             const std::wstring& message,
                             int32 line_no,
                             const std::wstring& source_id);
  void OnUpdateInspectorSetting(const std::string& key,
                                const std::string& value);
  void OnMsgShouldCloseACK(bool proceed);
  void OnMsgClosePageACK();

  void OnAccessibilityNotifications(
      const std::vector<ViewHostMsg_AccessibilityNotification_Params>& params);
  void OnScriptEvalResponse(int id, const ListValue& result);
  void OnDidZoomURL(double zoom_level, bool remember, const GURL& url);

#if defined(OS_MACOSX)
  void OnMsgShowPopup(const ViewHostMsg_ShowPopup_Params& params);
#endif

 private:
  friend class TestRenderViewHost;

  // The SiteInstance associated with this RenderViewHost.  All pages drawn
  // in this RenderViewHost are part of this SiteInstance.  Should not change
  // over time.
  scoped_refptr<SiteInstance> instance_;

  // Our delegate, which wants to know about changes in the RenderView.
  RenderViewHostDelegate* delegate_;

  // true if we are currently waiting for a response for drag context
  // information.
  bool waiting_for_drag_context_response_;

  // A bitwise OR of bindings types that have been enabled for this RenderView.
  // See BindingsPolicy for details.
  int enabled_bindings_;

  // The request_id for the pending cross-site request. Set to -1 if
  // there is a pending request, but we have not yet started the unload
  // for the current page. Set to the request_id value of the pending
  // request once we have gotten the some data for the pending page
  // and thus started the unload process.
  int pending_request_id_;

  // Whether we should buffer outgoing Navigate messages rather than sending
  // them.  This will be true when a RenderViewHost is created for a cross-site
  // request, until we hear back from the onbeforeunload handler of the old
  // RenderViewHost.
  bool navigations_suspended_;

  // We only buffer a suspended navigation message while we a pending RVH for a
  // TabContents.  There will only ever be one suspended navigation, because
  // TabContents will destroy the pending RVH and create a new one if a second
  // navigation occurs.
  scoped_ptr<ViewMsg_Navigate> suspended_nav_message_;

  // Whether this RenderViewHost is currently swapped out, such that the view is
  // being rendered by another process.
  bool is_swapped_out_;

  // If we were asked to RunModal, then this will hold the reply_msg that we
  // must return to the renderer to unblock it.
  IPC::Message* run_modal_reply_msg_;

  // Set to true when there is a pending ViewMsg_ShouldClose message.  This
  // ensures we don't spam the renderer with multiple beforeunload requests.
  // When either this value or is_waiting_for_unload_ack_ is true, the value of
  // unload_ack_is_for_cross_site_transition_ indicates whether this is for a
  // cross-site transition or a tab close attempt.
  bool is_waiting_for_beforeunload_ack_;

  // Set to true when there is a pending ViewMsg_Close message.  Also see
  // is_waiting_for_beforeunload_ack_, unload_ack_is_for_cross_site_transition_.
  bool is_waiting_for_unload_ack_;

  // Valid only when is_waiting_for_beforeunload_ack_ or
  // is_waiting_for_unload_ack_ is true.  This tells us if the unload request
  // is for closing the entire tab ( = false), or only this RenderViewHost in
  // the case of a cross-site transition ( = true).
  bool unload_ack_is_for_cross_site_transition_;

  bool are_javascript_messages_suppressed_;

  // True if the render view can be shut down suddenly.
  bool sudden_termination_allowed_;

  // The session storage namespace to be used by the associated render view.
  scoped_refptr<SessionStorageNamespace> session_storage_namespace_;

  // Whether the accessibility tree should be saved, for unit testing.
  bool save_accessibility_tree_for_testing_;

  // The most recently received accessibility tree - for unit testing only.
  webkit_glue::WebAccessibility accessibility_tree_;

  // The termination status of the last render view that terminated.
  base::TerminationStatus render_view_termination_status_;

  // A list of observers that filter messages.  Weak references.
  ObserverList<RenderViewHostObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(RenderViewHost);
};

#endif  // CONTENT_BROWSER_RENDERER_HOST_RENDER_VIEW_HOST_H_
