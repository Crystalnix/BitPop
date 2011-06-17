// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_RENDER_VIEW_HOST_H_
#define CONTENT_BROWSER_RENDERER_HOST_RENDER_VIEW_HOST_H_
#pragma once

#include <string>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/process_util.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "chrome/common/content_settings_types.h"
#include "chrome/common/render_view_commands.h"
#include "chrome/common/translate_errors.h"
#include "chrome/common/view_types.h"
#include "content/browser/renderer_host/render_widget_host.h"
#include "content/common/page_zoom.h"
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
struct ContentSettings;
struct ContextMenuParams;
struct MediaPlayerAction;
struct ViewHostMsg_AccessibilityNotification_Params;
struct ViewHostMsg_CreateWindow_Params;
struct ViewHostMsg_ShowPopup_Params;
struct ViewMsg_Navigate_Params;
struct WebDropData;
struct WebPreferences;
struct UserMetricsAction;

namespace gfx {
class Point;
}  // namespace gfx

namespace webkit_glue {
struct CustomContextMenuContext;
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
  void set_delegate(RenderViewHostDelegate* d) { delegate_ = d; }

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

  // Causes the renderer to invoke the onbeforeunload event handler.  The
  // result will be returned via ViewMsg_ShouldClose. See also ClosePage which
  // will fire the PageUnload event.
  //
  // Set bool for_cross_site_transition when this close is just for the current
  // RenderView in the case of a cross-site transition. False means we're
  // closing the entire tab.
  void FirePageBeforeUnload(bool for_cross_site_transition);

  // Causes the renderer to close the current page, including running its
  // onunload event handler.  A ClosePage_ACK message will be sent to the
  // ResourceDispatcherHost when it is finished.
  //
  // Please see ViewMsg_ClosePage in resource_messages_internal.h for a
  // description of the parameters.
  void ClosePage(bool for_cross_site_transition,
                 int new_render_process_host_id,
                 int new_request_id);

  // Called by ResourceDispatcherHost after the ClosePageACK is received.
  void OnClosePageACK(bool for_cross_site_transition);

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

  struct CommandState {
    bool is_enabled;
    RenderViewCommandCheckedState checked_state;
  };
  CommandState GetStateForCommand(RenderViewCommand command) const;

  // Stops the current load.
  void Stop();

  // Reloads the current frame.
  void ReloadFrame();

  // Start looking for a string within the content of the page, with the
  // specified options.
  void StartFinding(int request_id,
                    const string16& search_string,
                    bool forward,
                    bool match_case,
                    bool find_next);

  // Cancel a pending find operation.
  void StopFinding(FindBarController::SelectionAction selection_action);

  // Increment, decrement, or reset the zoom level of a page.
  void Zoom(PageZoom::Function function);

  // Change the zoom level of a page to a specific value.
  void SetZoomLevel(double zoom_level);

  // Change the encoding of the page.
  void SetPageEncoding(const std::string& encoding);

  // Reset any override encoding on the page and change back to default.
  void ResetPageEncodingToDefault();

  // Change the alternate error page URL.  An empty GURL disables the use of
  // alternate error pages.
  void SetAlternateErrorPageURL(const GURL& url);

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

  // Tell the RenderView to reserve a range of page ids of the given size.
  void ReservePageIDRange(int size);

  // Runs some javascript within the context of a frame in the page.
  void ExecuteJavascriptInWebFrame(const string16& frame_xpath,
                                   const string16& jscript);

  // Runs some javascript within the context of a frame in the page. The result
  // is sent back via the notification EXECUTE_JAVASCRIPT_RESULT.
  int ExecuteJavascriptInWebFrameNotifyResult(const string16& frame_xpath,
                                              const string16& jscript);

  // Insert some css into a frame in the page. |id| is optional, and specifies
  // the element id given when inserting/replacing the style element.
  void InsertCSSInWebFrame(const std::wstring& frame_xpath,
                           const std::string& css,
                           const std::string& id);

  // Edit operations.
  void Undo();
  void Redo();
  void Cut();
  void Copy();
  void CopyToFindPboard();
  void Paste();
  void ToggleSpellCheck();
  void Delete();
  void SelectAll();
  void ToggleSpellPanel(bool is_currently_visible);

  // Downloads an image notifying the favicon delegate appropriately. The
  // returned integer uniquely identifies the download for the lifetime of the
  // browser.
  int DownloadFavicon(const GURL& url, int image_size);

  // Captures a snapshot of the page.
  void CaptureSnapshot();

  // Notifies the RenderView that the JavaScript message that was shown was
  // closed by the user.
  void JavaScriptMessageBoxClosed(IPC::Message* reply_msg,
                                  bool success,
                                  const std::wstring& prompt);

  // Send an action to the media player element located at |location|.
  void MediaPlayerActionAt(const gfx::Point& location,
                           const WebKit::WebMediaPlayerAction& action);

  // Notifies the renderer that the context menu has closed.
  void ContextMenuClosed(
      const webkit_glue::CustomContextMenuContext& custom_context);

  // Copies the image at the specified point.
  void CopyImageAt(int x, int y);

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

  // See variable comment.
  bool is_extension_process() const { return is_extension_process_; }
  void set_is_extension_process(bool is_extension_process) {
    is_extension_process_ = is_extension_process;
  }

  // Sets a property with the given name and value on the Web UI binding object.
  // Must call AllowWebUIBindings() on this renderer first.
  void SetWebUIProperty(const std::string& name, const std::string& value);

  // Tells the renderer view to focus the first (last if reverse is true) node.
  void SetInitialFocus(bool reverse);

  // Clears the node that is currently focused (if any).
  void ClearFocusedNode();

  // Tells the renderer view to scroll to the focused node.
  void ScrollFocusedEditableNodeIntoView();

  // Update render view specific (WebKit) preferences.
  void UpdateWebPreferences(const WebPreferences& prefs);

  // Request the Renderer to ask the default plugin to start installation of
  // missing plugin. Called by PluginInstallerInfoBarDelegate.
  void InstallMissingPlugin();

  // Load all blocked plugins in the RenderView.
  void LoadBlockedPlugins();

  // Get all script and frame urls from all frames in the current document.
  // Called when a malware interstitial page is shown.
  void GetMalwareDOMDetails();

  // Get all savable resource links from current webpage, include main
  // frame and sub-frame.
  void GetAllSavableResourceLinksForCurrentPage(const GURL& page_url);

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

  // Forward a message from external host to chrome renderer.
  void ForwardMessageFromExternalHost(const std::string& message,
                                      const std::string& origin,
                                      const std::string& target);

  // Message the renderer that we should be counted as a new document and not
  // as a popup.
  void DisassociateFromPopupCount();

  // Notifies the Renderer that a move or resize of its containing window has
  // started (this is used to hide the autocomplete popups if any).
  void WindowMoveOrResizeStarted();

  // RenderWidgetHost public overrides.
  virtual void Shutdown();
  virtual bool IsRenderView() const;
  virtual bool OnMessageReceived(const IPC::Message& msg);
  virtual void GotFocus();
  virtual void LostCapture();
  virtual void ForwardMouseEvent(const WebKit::WebMouseEvent& mouse_event);
  virtual void OnMouseActivate();
  virtual void ForwardKeyboardEvent(const NativeWebKeyboardEvent& key_event);
  virtual void ForwardEditCommand(const std::string& name,
                                  const std::string& value);
  virtual void ForwardEditCommandsForNextKeyEvent(
      const EditCommands& edit_commands);

  // Creates a new RenderView with the given route id.
  void CreateNewWindow(int route_id,
                       const ViewHostMsg_CreateWindow_Params& params);

  // Creates a new RenderWidget with the given route id.  |popup_type| indicates
  // if this widget is a popup and what kind of popup it is (select, autofill).
  void CreateNewWidget(int route_id, WebKit::WebPopupType popup_type);

  // Creates a full screen RenderWidget.
  void CreateNewFullscreenWidget(int route_id);

  // Tells the renderer which browser window it is being attached to.
  void UpdateBrowserWindowId(int window_id);

  // Tells the render view that a custom context action has been selected.
  void PerformCustomContextMenuAction(
      const webkit_glue::CustomContextMenuContext& custom_context,
      unsigned action);

  // Informs renderer of updated content settings.
  void SendContentSettings(const GURL& url,
                           const ContentSettings& settings);

  // Tells the renderer to notify us when the page contents preferred size
  // changed. |flags| is a combination of
  // |ViewHostMsg_EnablePreferredSizeChangedMode_Flags| values, which is defined
  // in render_messages.h.
  void EnablePreferredSizeChangedMode(int flags);

#if defined(OS_MACOSX)
  // Select popup menu related methods (for external popup menus).
  void DidSelectPopupMenuItem(int selected_index);
  void DidCancelPopupMenu();
#endif

  // SearchBox notifications.
  void SearchBoxChange(const string16& value,
                       bool verbatim,
                       int selection_start,
                       int selection_end);
  void SearchBoxSubmit(const string16& value,
                       bool verbatim);
  void SearchBoxCancel();
  void SearchBoxResize(const gfx::Rect& search_box_bounds);
  void DetermineIfPageSupportsInstant(const string16& value,
                                      bool verbatim,
                                      int selection_start,
                                      int selection_end);

  // Send a notification to the V8 JavaScript engine to change its parameters
  // while performing stress testing. |cmd| is one of the values defined by
  // |ViewHostMsg_JavaScriptStressTestControl_Commands|, which is defined
  // in render_messages.h.
  void JavaScriptStressTestControl(int cmd, int param);

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
  virtual void OnMsgFocusedNodeChanged(bool is_editable_node);
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
  void OnMsgScreenshot(const SkBitmap& bitmap);
  void OnMsgClose();
  void OnMsgRequestMove(const gfx::Rect& pos);
  void OnMsgDidStartLoading();
  void OnMsgDidStopLoading();
  void OnMsgDidChangeLoadProgress(double load_progress);
  void OnMsgDocumentAvailableInMainFrame();
  void OnMsgDocumentOnLoadCompletedInMainFrame(int32 page_id);
  void OnMsgUpdateFaviconURL(int32 page_id, const GURL& icon_url);
  void OnMsgDidDownloadFavicon(int id,
                               const GURL& image_url,
                               bool errored,
                               const SkBitmap& image_data);
  void OnMsgContextMenu(const ContextMenuParams& params);
  void OnMsgOpenURL(const GURL& url, const GURL& referrer,
                    WindowOpenDisposition disposition);
  void OnMsgDidContentsPreferredSizeChange(const gfx::Size& new_size);
  void OnMsgDomOperationResponse(const std::string& json_string,
                                 int automation_id);
  void OnMsgWebUISend(const GURL& source_url,
                      const std::string& message,
                      const std::string& content);
  void OnMsgForwardMessageToExternalHost(const std::string& message,
                                         const std::string& origin,
                                         const std::string& target);
  void OnMsgSetTooltipText(const std::wstring& tooltip_text,
                           WebKit::WebTextDirection text_direction_hint);
  void OnMsgSelectionChanged(const std::string& text);
  void OnMsgPasteFromSelectionClipboard();
  void OnMsgRunJavaScriptMessage(const std::wstring& message,
                                 const std::wstring& default_prompt,
                                 const GURL& frame_url,
                                 const int flags,
                                 IPC::Message* reply_msg);
  void OnMsgRunBeforeUnloadConfirm(const GURL& frame_url,
                                   const std::wstring& message,
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

  void OnAccessibilityNotifications(
      const std::vector<ViewHostMsg_AccessibilityNotification_Params>& params);
  void OnCSSInserted();
  void OnContentBlocked(ContentSettingsType type,
                        const std::string& resource_identifier);
  void OnAppCacheAccessed(const GURL& manifest_url, bool blocked_by_policy);
  void OnWebDatabaseAccessed(const GURL& url,
                             const string16& name,
                             const string16& display_name,
                             unsigned long estimated_size,
                             bool blocked_by_policy);
  void OnUpdateZoomLimits(int minimum_percent,
                          int maximum_percent,
                          bool remember);
  void OnScriptEvalResponse(int id, const ListValue& result);
  void OnCommandStateChanged(int command,
                             bool is_enabled,
                             int checked_state);

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

  // Whether this render view will get extension api bindings. This controls
  // what process type we use.
  bool is_extension_process_;

  // Whether the accessibility tree should be saved, for unit testing.
  bool save_accessibility_tree_for_testing_;

  // The most recently received accessibility tree - for unit testing only.
  webkit_glue::WebAccessibility accessibility_tree_;

  // The termination status of the last render view that terminated.
  base::TerminationStatus render_view_termination_status_;

  // The enabled/disabled states of various commands.
  std::map<RenderViewCommand, CommandState> command_states_;

  // A list of observers that filter messages.  Weak references.
  ObserverList<RenderViewHostObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(RenderViewHost);
};

#endif  // CONTENT_BROWSER_RENDERER_HOST_RENDER_VIEW_HOST_H_
