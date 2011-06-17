// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_RENDER_VIEW_H_
#define CONTENT_RENDERER_RENDER_VIEW_H_
#pragma once

#include <deque>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/id_map.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/timer.h"
#include "build/build_config.h"
#include "content/renderer/renderer_webcookiejar_impl.h"
#include "content/common/edit_command.h"
#include "content/common/navigation_gesture.h"
#include "content/common/page_zoom.h"
#include "content/common/renderer_preferences.h"
#include "content/renderer/pepper_plugin_delegate_impl.h"
#include "content/renderer/render_widget.h"
#include "ipc/ipc_platform_file.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebAccessibilityNotification.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebConsoleMessage.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFileSystem.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrameClient.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebNode.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebTextDirection.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebViewClient.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebNavigationType.h"
#include "ui/gfx/surface/transport_dib.h"
#include "webkit/glue/webpreferences.h"
#include "webkit/plugins/npapi/webplugin_page_delegate.h"

#if defined(OS_WIN)
// RenderView is a diamond-shaped hierarchy, with WebWidgetClient at the root.
// VS warns when we inherit the WebWidgetClient method implementations from
// RenderWidget.  It's safe to ignore that warning.
#pragma warning(disable: 4250)
#endif

class AudioMessageFilter;
class DeviceOrientationDispatcher;
class ExternalPopupMenu;
class FilePath;
class GeolocationDispatcher;
class GURL;
class LoadProgressTracker;
class NavigationState;
class NotificationProvider;
class P2PSocketDispatcher;
class PepperDeviceTest;
class PrintWebViewHelper;
class RenderViewObserver;
class RenderViewVisitor;
class RenderWidgetFullscreenPepper;
class SkBitmap;
class SpeechInputDispatcher;
class WebPluginDelegatePepper;
class WebPluginDelegateProxy;
class WebUIBindings;
struct ContextMenuMediaParams;
struct PP_Flash_NetAddress;
struct ViewHostMsg_RunFileChooser_Params;
struct ViewMsg_ClosePage_Params;
struct ViewMsg_Navigate_Params;
struct ViewMsg_StopFinding_Params;
struct WebDropData;

namespace base {
class WaitableEvent;
}

namespace chrome {
class ChromeContentRendererClient;
}

namespace gfx {
class Point;
class Rect;
}

namespace webkit {

namespace npapi {
class PluginGroup;
}  // namespace npapi

namespace ppapi {
class PluginInstance;
class PluginModule;
}  // namespace ppapi

}  // namespace webkit

namespace webkit_glue {
struct CustomContextMenuContext;
class ImageResourceFetcher;
struct FileUploadData;
struct FormData;
struct PasswordFormFillData;
class ResourceFetcher;
}

namespace WebKit {
class WebAccessibilityCache;
class WebAccessibilityObject;
class WebApplicationCacheHost;
class WebApplicationCacheHostClient;
class WebDataSource;
class WebDocument;
class WebDragData;
class WebFrame;
class WebGeolocationClient;
class WebGeolocationServiceInterface;
class WebImage;
class WebInputElement;
class WebKeyboardEvent;
class WebMediaPlayer;
class WebMediaPlayerClient;
class WebMouseEvent;
class WebPlugin;
class WebSpeechInputController;
class WebSpeechInputListener;
class WebStorageNamespace;
class WebURLRequest;
class WebView;
struct WebContextMenuData;
struct WebFileChooserParams;
struct WebFindOptions;
struct WebMediaPlayerAction;
struct WebPluginParams;
struct WebPoint;
struct WebWindowFeatures;
}

// We need to prevent a page from trying to create infinite popups. It is not
// as simple as keeping a count of the number of immediate children
// popups. Having an html file that window.open()s itself would create
// an unlimited chain of RenderViews who only have one RenderView child.
//
// Therefore, each new top level RenderView creates a new counter and shares it
// with all its children and grandchildren popup RenderViews created with
// createView() to have a sort of global limit for the page so no more than
// kMaximumNumberOfPopups popups are created.
//
// This is a RefCounted holder of an int because I can't say
// scoped_refptr<int>.
typedef base::RefCountedData<int> SharedRenderViewCounter;

//
// RenderView is an object that manages a WebView object, and provides a
// communication interface with an embedding application process
//
class RenderView : public RenderWidget,
                   public WebKit::WebViewClient,
                   public WebKit::WebFrameClient,
                   public webkit::npapi::WebPluginPageDelegate,
                   public base::SupportsWeakPtr<RenderView> {
 public:
  // Creates a new RenderView.  The parent_hwnd specifies a HWND to use as the
  // parent of the WebView HWND that will be created.  If this is a constrained
  // popup or as a new tab, opener_id is the routing ID of the RenderView
  // responsible for creating this RenderView (corresponding to parent_hwnd).
  // |counter| is either a currently initialized counter, or NULL (in which case
  // we treat this RenderView as a top level window).
  static RenderView* Create(
      RenderThreadBase* render_thread,
      gfx::NativeViewId parent_hwnd,
      gfx::PluginWindowHandle compositing_surface,
      int32 opener_id,
      const RendererPreferences& renderer_prefs,
      const WebPreferences& webkit_prefs,
      SharedRenderViewCounter* counter,
      int32 routing_id,
      int64 session_storage_namespace_id,
      const string16& frame_name);

  // Visit all RenderViews with a live WebView (i.e., RenderViews that have
  // been closed but not yet destroyed are excluded).
  static void ForEach(RenderViewVisitor* visitor);

  // Returns the RenderView containing the given WebView.
  static RenderView* FromWebView(WebKit::WebView* webview);

  // Sets the "next page id" counter.
  static void SetNextPageID(int32 next_page_id);

  // May return NULL when the view is closing.
  WebKit::WebView* webview() const;

  bool is_prerendering() const { return is_prerendering_; }
  int page_id() const { return page_id_; }
  PepperPluginDelegateImpl* pepper_delegate() { return &pepper_delegate_; }

  AudioMessageFilter* audio_message_filter() {
    return audio_message_filter_;
  }

  const WebPreferences& webkit_preferences() const {
    return webkit_preferences_;
  }

  bool content_state_immediately() { return send_content_state_immediately_; }
  int enabled_bindings() const { return enabled_bindings_; }
  void set_enabled_bindings(int b) { enabled_bindings_ = b; }
  void set_send_content_state_immediately(bool value) {
    send_content_state_immediately_ = value;
  }

  // Returns true if we should display scrollbars for the given view size and
  // false if the scrollbars should be hidden.
  bool should_display_scrollbars(int width, int height) const {
    return (!send_preferred_size_changes_ ||
            (disable_scrollbars_size_limit_.width() <= width ||
             disable_scrollbars_size_limit_.height() <= height));
  }

  const WebKit::WebNode& context_menu_node() { return context_menu_node_; }

  // Current P2PSocketDispatcher. Set to NULL if P2P API is disabled.
  P2PSocketDispatcher* p2p_socket_dispatcher() {
    return p2p_socket_dispatcher_;
  }

  // Functions to add and remove observers for this object.
  void AddObserver(RenderViewObserver* observer);
  void RemoveObserver(RenderViewObserver* observer);

  // Evaluates a string of JavaScript in a particular frame.
  void EvaluateScript(const string16& frame_xpath,
                      const string16& jscript,
                      int id,
                      bool notify_result);

  // Adds the given file chooser request to the file_chooser_completion_ queue
  // (see that var for more) and requests the chooser be displayed if there are
  // no other waiting items in the queue.
  //
  // Returns true if the chooser was successfully scheduled. False means we
  // didn't schedule anything.
  bool ScheduleFileChooser(const ViewHostMsg_RunFileChooser_Params& params,
                           WebKit::WebFileChooserCompletion* completion);

  // Sets whether  the renderer should report load progress to the browser.
  void SetReportLoadProgressEnabled(bool enabled);

  // Gets the focused node. If no such node exists then the node will be isNull.
  WebKit::WebNode GetFocusedNode() const;

  // Returns true if the parameter node is a textfield, text area or a content
  // editable div.
  bool IsEditableNode(const WebKit::WebNode& node);

  void LoadNavigationErrorPage(WebKit::WebFrame* frame,
                               const WebKit::WebURLRequest& failed_request,
                               const WebKit::WebURLError& error,
                               const std::string& html,
                               bool replace);

  // Plugin-related functions --------------------------------------------------
  // (See also WebPluginPageDelegate implementation.)

  // Notification that the given plugin has crashed.
  void PluginCrashed(const FilePath& plugin_path);

  // Notification that the default plugin has done something about a missing
  // plugin. See default_plugin_shared.h for possible values of |status|.
  void OnMissingPluginStatus(WebPluginDelegateProxy* delegate,
                             int status);

  // Create a new NPAPI plugin.
  WebKit::WebPlugin* CreateNPAPIPlugin(WebKit::WebFrame* frame,
                                       const WebKit::WebPluginParams& params,
                                       const FilePath& path,
                                       const std::string& mime_type);

  // Create a new Pepper plugin.
  WebKit::WebPlugin* CreatePepperPlugin(
      WebKit::WebFrame* frame,
      const WebKit::WebPluginParams& params,
      const FilePath& path,
      webkit::ppapi::PluginModule* pepper_module);

  // Creates a fullscreen container for a pepper plugin instance.
  RenderWidgetFullscreenPepper* CreatePepperFullscreenContainer(
      webkit::ppapi::PluginInstance* plugin);

  // Create a new plugin without checking the content settings.
  WebKit::WebPlugin* CreatePluginNoCheck(WebKit::WebFrame* frame,
                                         const WebKit::WebPluginParams& params);

#if defined(OS_MACOSX)
  // Informs the render view that the given plugin has gained or lost focus.
  void PluginFocusChanged(bool focused, int plugin_id);

  // Starts plugin IME.
  void StartPluginIme();

  // Helper routines for accelerated plugin support. Used by the
  // WebPluginDelegateProxy, which has a pointer to the RenderView.
  gfx::PluginWindowHandle AllocateFakePluginWindowHandle(bool opaque,
                                                         bool root);
  void DestroyFakePluginWindowHandle(gfx::PluginWindowHandle window);
  void AcceleratedSurfaceSetIOSurface(gfx::PluginWindowHandle window,
                                      int32 width,
                                      int32 height,
                                      uint64 io_surface_identifier);
  TransportDIB::Handle AcceleratedSurfaceAllocTransportDIB(size_t size);
  void AcceleratedSurfaceFreeTransportDIB(TransportDIB::Id dib_id);
  void AcceleratedSurfaceSetTransportDIB(gfx::PluginWindowHandle window,
                                         int32 width,
                                         int32 height,
                                         TransportDIB::Handle transport_dib);
  void AcceleratedSurfaceBuffersSwapped(gfx::PluginWindowHandle window,
                                        uint64 surface_id);
#endif

  void RegisterPluginDelegate(WebPluginDelegateProxy* delegate);
  void UnregisterPluginDelegate(WebPluginDelegateProxy* delegate);

  // IPC::Channel::Listener implementation -------------------------------------

  virtual bool OnMessageReceived(const IPC::Message& msg);

  // WebKit::WebWidgetClient implementation ------------------------------------

  // Most methods are handled by RenderWidget.
  virtual void didFocus();
  virtual void didBlur();
  virtual void show(WebKit::WebNavigationPolicy policy);
  virtual void runModal();

  // WebKit::WebViewClient implementation --------------------------------------

  virtual WebKit::WebView* createView(
      WebKit::WebFrame* creator,
      const WebKit::WebURLRequest& request,
      const WebKit::WebWindowFeatures& features,
      const WebKit::WebString& frame_name);
  virtual WebKit::WebWidget* createPopupMenu(WebKit::WebPopupType popup_type);
  virtual WebKit::WebWidget* createPopupMenu(
      const WebKit::WebPopupMenuInfo& info);
  virtual WebKit::WebExternalPopupMenu* createExternalPopupMenu(
      const WebKit::WebPopupMenuInfo& popup_menu_info,
      WebKit::WebExternalPopupMenuClient* popup_menu_client);
  virtual WebKit::WebStorageNamespace* createSessionStorageNamespace(
      unsigned quota);
  virtual void didAddMessageToConsole(
      const WebKit::WebConsoleMessage& message,
      const WebKit::WebString& source_name,
      unsigned source_line);
  virtual void printPage(WebKit::WebFrame* frame);
  virtual WebKit::WebNotificationPresenter* notificationPresenter();
  virtual bool enumerateChosenDirectory(
      const WebKit::WebString& path,
      WebKit::WebFileChooserCompletion* chooser_completion);
  virtual void didStartLoading();
  virtual void didStopLoading();
  virtual void didChangeLoadProgress(WebKit::WebFrame* frame,
                                     double load_progress);
  virtual bool isSmartInsertDeleteEnabled();
  virtual bool isSelectTrailingWhitespaceEnabled();
  virtual void didChangeSelection(bool is_selection_empty);
  virtual void didExecuteCommand(const WebKit::WebString& command_name);
  virtual bool handleCurrentKeyboardEvent();
  virtual bool runFileChooser(
      const WebKit::WebFileChooserParams& params,
      WebKit::WebFileChooserCompletion* chooser_completion);
  virtual void runModalAlertDialog(WebKit::WebFrame* frame,
                                   const WebKit::WebString& message);
  virtual bool runModalConfirmDialog(WebKit::WebFrame* frame,
                                     const WebKit::WebString& message);
  virtual bool runModalPromptDialog(WebKit::WebFrame* frame,
                                    const WebKit::WebString& message,
                                    const WebKit::WebString& default_value,
                                    WebKit::WebString* actual_value);
  virtual bool runModalBeforeUnloadDialog(WebKit::WebFrame* frame,
                                          const WebKit::WebString& message);
  virtual void showContextMenu(WebKit::WebFrame* frame,
                               const WebKit::WebContextMenuData& data);
  virtual bool supportsFullscreen();
  virtual void enterFullscreenForNode(const WebKit::WebNode&);
  virtual void exitFullscreenForNode(const WebKit::WebNode&);
  virtual void setStatusText(const WebKit::WebString& text);
  virtual void setMouseOverURL(const WebKit::WebURL& url);
  virtual void setKeyboardFocusURL(const WebKit::WebURL& url);
  virtual void setToolTipText(const WebKit::WebString& text,
                              WebKit::WebTextDirection hint);
  virtual void startDragging(const WebKit::WebDragData& data,
                             WebKit::WebDragOperationsMask mask,
                             const WebKit::WebImage& image,
                             const WebKit::WebPoint& imageOffset);
  virtual bool acceptsLoadDrops();
  virtual void focusNext();
  virtual void focusPrevious();
  virtual void focusedNodeChanged(const WebKit::WebNode& node);
  virtual void navigateBackForwardSoon(int offset);
  virtual int historyBackListCount();
  virtual int historyForwardListCount();
  virtual void postAccessibilityNotification(
      const WebKit::WebAccessibilityObject& obj,
      WebKit::WebAccessibilityNotification notification);
  virtual void didUpdateInspectorSetting(const WebKit::WebString& key,
                                         const WebKit::WebString& value);
  virtual WebKit::WebGeolocationClient* geolocationClient();
  virtual WebKit::WebSpeechInputController* speechInputController(
      WebKit::WebSpeechInputListener* listener);
  virtual WebKit::WebDeviceOrientationClient* deviceOrientationClient();
  virtual void zoomLimitsChanged(double minimum_level, double maximum_level);
  virtual void zoomLevelChanged();
  virtual void registerProtocolHandler(const WebKit::WebString& scheme,
                                       const WebKit::WebString& base_url,
                                       const WebKit::WebString& url,
                                       const WebKit::WebString& title);

  // WebKit::WebFrameClient implementation -------------------------------------

  virtual WebKit::WebPlugin* createPlugin(
      WebKit::WebFrame* frame,
      const WebKit::WebPluginParams& params);
  virtual WebKit::WebWorker* createWorker(WebKit::WebFrame* frame,
                                          WebKit::WebWorkerClient* client);
  virtual WebKit::WebSharedWorker* createSharedWorker(
      WebKit::WebFrame* frame, const WebKit::WebURL& url,
      const WebKit::WebString& name, unsigned long long documentId);
  virtual WebKit::WebMediaPlayer* createMediaPlayer(
      WebKit::WebFrame* frame,
      WebKit::WebMediaPlayerClient* client);
  virtual WebKit::WebApplicationCacheHost* createApplicationCacheHost(
      WebKit::WebFrame* frame,
      WebKit::WebApplicationCacheHostClient* client);
  virtual WebKit::WebCookieJar* cookieJar(WebKit::WebFrame* frame);
  virtual void frameDetached(WebKit::WebFrame* frame);
  virtual void willClose(WebKit::WebFrame* frame);
  virtual void loadURLExternally(WebKit::WebFrame* frame,
                                 const WebKit::WebURLRequest& request,
                                 WebKit::WebNavigationPolicy policy);
  virtual WebKit::WebNavigationPolicy decidePolicyForNavigation(
      WebKit::WebFrame* frame,
      const WebKit::WebURLRequest& request,
      WebKit::WebNavigationType type,
      const WebKit::WebNode&,
      WebKit::WebNavigationPolicy default_policy,
      bool is_redirect);
  virtual bool canHandleRequest(WebKit::WebFrame* frame,
                                const WebKit::WebURLRequest& request);
  virtual WebKit::WebURLError cannotHandleRequestError(
      WebKit::WebFrame* frame,
      const WebKit::WebURLRequest& request);
  virtual WebKit::WebURLError cancelledError(
      WebKit::WebFrame* frame,
      const WebKit::WebURLRequest& request);
  virtual void unableToImplementPolicyWithError(
      WebKit::WebFrame* frame,
      const WebKit::WebURLError& error);
  virtual void willSendSubmitEvent(WebKit::WebFrame* frame,
                                   const WebKit::WebFormElement& form);
  virtual void willSubmitForm(WebKit::WebFrame* frame,
                              const WebKit::WebFormElement& form);
  virtual void willPerformClientRedirect(WebKit::WebFrame* frame,
                                         const WebKit::WebURL& from,
                                         const WebKit::WebURL& to,
                                         double interval,
                                         double fire_time);
  virtual void didCancelClientRedirect(WebKit::WebFrame* frame);
  virtual void didCompleteClientRedirect(WebKit::WebFrame* frame,
                                         const WebKit::WebURL& from);
  virtual void didCreateDataSource(WebKit::WebFrame* frame,
                                   WebKit::WebDataSource* datasource);
  virtual void didStartProvisionalLoad(WebKit::WebFrame* frame);
  virtual void didReceiveServerRedirectForProvisionalLoad(
      WebKit::WebFrame* frame);
  virtual void didFailProvisionalLoad(WebKit::WebFrame* frame,
                                      const WebKit::WebURLError& error);
  virtual void didReceiveDocumentData(WebKit::WebFrame* frame,
                                      const char* data, size_t length,
                                      bool& prevent_default);
  virtual void didCommitProvisionalLoad(WebKit::WebFrame* frame,
                                        bool is_new_navigation);
  virtual void didClearWindowObject(WebKit::WebFrame* frame);
  virtual void didCreateDocumentElement(WebKit::WebFrame* frame);
  virtual void didReceiveTitle(WebKit::WebFrame* frame,
                               const WebKit::WebString& title);
  virtual void didChangeIcons(WebKit::WebFrame*);
  virtual void didFinishDocumentLoad(WebKit::WebFrame* frame);
  virtual void didHandleOnloadEvents(WebKit::WebFrame* frame);
  virtual void didFailLoad(WebKit::WebFrame* frame,
                           const WebKit::WebURLError& error);
  virtual void didFinishLoad(WebKit::WebFrame* frame);
  virtual void didNavigateWithinPage(WebKit::WebFrame* frame,
                                     bool is_new_navigation);
  virtual void didUpdateCurrentHistoryItem(WebKit::WebFrame* frame);
  virtual void assignIdentifierToRequest(WebKit::WebFrame* frame,
                                         unsigned identifier,
                                         const WebKit::WebURLRequest& request);
  virtual void willSendRequest(WebKit::WebFrame* frame,
                               unsigned identifier,
                               WebKit::WebURLRequest& request,
                               const WebKit::WebURLResponse& redirect_response);
  virtual void didReceiveResponse(WebKit::WebFrame* frame,
                                  unsigned identifier,
                                  const WebKit::WebURLResponse& response);
  virtual void didFinishResourceLoad(WebKit::WebFrame* frame,
                                     unsigned identifier);
  virtual void didFailResourceLoad(WebKit::WebFrame* frame,
                                   unsigned identifier,
                                   const WebKit::WebURLError& error);
  virtual void didLoadResourceFromMemoryCache(
      WebKit::WebFrame* frame,
      const WebKit::WebURLRequest& request,
      const WebKit::WebURLResponse&);
  virtual void didDisplayInsecureContent(WebKit::WebFrame* frame);
  virtual void didRunInsecureContent(
      WebKit::WebFrame* frame,
      const WebKit::WebSecurityOrigin& origin,
      const WebKit::WebURL& target);

  virtual bool allowImages(WebKit::WebFrame* frame, bool enabled_per_settings);
  virtual bool allowPlugins(WebKit::WebFrame* frame, bool enabled_per_settings);
  virtual bool allowScript(WebKit::WebFrame* frame, bool enabled_per_settings);
  virtual bool allowDatabase(WebKit::WebFrame* frame,
                             const WebKit::WebString& name,
                             const WebKit::WebString& display_name,
                             unsigned long estimated_size);
  virtual void didNotAllowScript(WebKit::WebFrame* frame);
  virtual void didNotAllowPlugins(WebKit::WebFrame* frame);
  virtual void didExhaustMemoryAvailableForScript(WebKit::WebFrame* frame);
  virtual void didCreateScriptContext(WebKit::WebFrame* frame);
  virtual void didDestroyScriptContext(WebKit::WebFrame* frame);
  virtual void didCreateIsolatedScriptContext(WebKit::WebFrame* frame);
  virtual bool allowScriptExtension(WebKit::WebFrame*,
                                    const WebKit::WebString& extension_name,
                                    int extensionGroup);
  virtual void logCrossFramePropertyAccess(
      WebKit::WebFrame* frame,
      WebKit::WebFrame* target,
      bool cross_origin,
      const WebKit::WebString& property_name,
      unsigned long long event_id);
  virtual void didChangeContentsSize(WebKit::WebFrame* frame,
                                     const WebKit::WebSize& size);
  virtual void didChangeScrollOffset(WebKit::WebFrame* frame);
  virtual void reportFindInPageMatchCount(int request_id,
                                          int count,
                                          bool final_update);
  virtual void reportFindInPageSelection(int request_id,
                                         int active_match_ordinal,
                                         const WebKit::WebRect& sel);

  virtual void openFileSystem(WebKit::WebFrame* frame,
                              WebKit::WebFileSystem::Type type,
                              long long size,
                              bool create,
                              WebKit::WebFileSystemCallbacks* callbacks);

  virtual void queryStorageUsageAndQuota(
      WebKit::WebFrame* frame,
      WebKit::WebStorageQuotaType type,
      WebKit::WebStorageQuotaCallbacks* callbacks);

  virtual void requestStorageQuota(
      WebKit::WebFrame* frame,
      WebKit::WebStorageQuotaType type,
      unsigned long long requested_size,
      WebKit::WebStorageQuotaCallbacks* callbacks);

  // webkit_glue::WebPluginPageDelegate implementation -------------------------

  virtual webkit::npapi::WebPluginDelegate* CreatePluginDelegate(
      const FilePath& file_path,
      const std::string& mime_type);
  virtual void CreatedPluginWindow(gfx::PluginWindowHandle handle);
  virtual void WillDestroyPluginWindow(gfx::PluginWindowHandle handle);
  virtual void DidMovePlugin(const webkit::npapi::WebPluginGeometry& move);
  virtual void DidStartLoadingForPlugin();
  virtual void DidStopLoadingForPlugin();
  virtual WebKit::WebCookieJar* GetCookieJar();

  // Please do not add your stuff randomly to the end here. If there is an
  // appropriate section, add it there. If not, there are some random functions
  // nearer to the top you can add it to.

  virtual void DidFlushPaint();

  // Cannot use std::set unfortunately since linked_ptr<> does not support
  // operator<.
  typedef std::vector<linked_ptr<webkit_glue::ImageResourceFetcher> >
      ImageResourceFetcherList;

 protected:
  // RenderWidget overrides:
  virtual void Close();
  virtual void OnResize(const gfx::Size& new_size,
                        const gfx::Rect& resizer_rect);
  virtual void DidInitiatePaint();
  virtual webkit::ppapi::PluginInstance* GetBitmapForOptimizedPluginPaint(
      const gfx::Rect& paint_bounds,
      TransportDIB** dib,
      gfx::Rect* location,
      gfx::Rect* clip);
  virtual gfx::Point GetScrollOffset();
  virtual void DidHandleKeyEvent();
  virtual void DidHandleMouseEvent(const WebKit::WebMouseEvent& event);
  virtual void OnSetFocus(bool enable);
  virtual void OnWasHidden();
  virtual void OnWasRestored(bool needs_repainting);

 private:
  // For unit tests.
  friend class ExternalPopupMenuTest;
  friend class PepperDeviceTest;
  friend class RenderViewTest;

  FRIEND_TEST_ALL_PREFIXES(ExternalPopupMenuRemoveTest, RemoveOnChange);
  FRIEND_TEST_ALL_PREFIXES(ExternalPopupMenuTest, NormalCase);
  FRIEND_TEST_ALL_PREFIXES(ExternalPopupMenuTest, ShowPopupThenNavigate);
  FRIEND_TEST_ALL_PREFIXES(RenderViewTest, ImeComposition);
  FRIEND_TEST_ALL_PREFIXES(RenderViewTest, InsertCharacters);
  FRIEND_TEST_ALL_PREFIXES(RenderViewTest, JSBlockSentAfterPageLoad);
  FRIEND_TEST_ALL_PREFIXES(RenderViewTest, LastCommittedUpdateState);
  FRIEND_TEST_ALL_PREFIXES(RenderViewTest, OnHandleKeyboardEvent);
  FRIEND_TEST_ALL_PREFIXES(RenderViewTest, OnImeStateChanged);
  FRIEND_TEST_ALL_PREFIXES(RenderViewTest, OnNavStateChanged);
  FRIEND_TEST_ALL_PREFIXES(RenderViewTest, OnSetTextDirection);
  FRIEND_TEST_ALL_PREFIXES(RenderViewTest, UpdateTargetURLWithInvalidURL);
#if defined(OS_MACOSX)
  FRIEND_TEST_ALL_PREFIXES(RenderViewTest, MacTestCmdUp);
#endif

  typedef std::map<GURL, double> HostZoomLevels;

  // Identifies an accessibility notification from webkit.
  struct RendererAccessibilityNotification {
   public:
    bool ShouldIncludeChildren();

    // The webkit glue id of the accessibility object.
    int32 id;

    // The accessibility notification type.
    WebKit::WebAccessibilityNotification type;
  };

  enum ErrorPageType {
    DNS_ERROR,
    HTTP_404,
    CONNECTION_ERROR,
  };

  RenderView(RenderThreadBase* render_thread,
             gfx::NativeViewId parent_hwnd,
             gfx::PluginWindowHandle compositing_surface,
             int32 opener_id,
             const RendererPreferences& renderer_prefs,
             const WebPreferences& webkit_prefs,
             SharedRenderViewCounter* counter,
             int32 routing_id,
             int64 session_storage_namespace_id,
             const string16& frame_name);

  // Do not delete directly.  This class is reference counted.
  virtual ~RenderView();

  void UpdateURL(WebKit::WebFrame* frame);
  void UpdateTitle(WebKit::WebFrame* frame, const string16& title);
  void UpdateSessionHistory(WebKit::WebFrame* frame);

  // Update current main frame's encoding and send it to browser window.
  // Since we want to let users see the right encoding info from menu
  // before finishing loading, we call the UpdateEncoding in
  // a) function:DidCommitLoadForFrame. When this function is called,
  // that means we have got first data. In here we try to get encoding
  // of page if it has been specified in http header.
  // b) function:DidReceiveTitle. When this function is called,
  // that means we have got specified title. Because in most of webpages,
  // title tags will follow meta tags. In here we try to get encoding of
  // page if it has been specified in meta tag.
  // c) function:DidFinishDocumentLoadForFrame. When this function is
  // called, that means we have got whole html page. In here we should
  // finally get right encoding of page.
  void UpdateEncoding(WebKit::WebFrame* frame,
                      const std::string& encoding_name);

  void OpenURL(const GURL& url, const GURL& referrer,
               WebKit::WebNavigationPolicy policy);

  bool RunJavaScriptMessage(int type,
                            const std::wstring& message,
                            const std::wstring& default_value,
                            const GURL& frame_url,
                            std::wstring* result);

  // Sends a message and runs a nested message loop.
  bool SendAndRunNestedMessageLoop(IPC::SyncMessage* message);

  // Send queued accessibility notifications from the renderer to the browser.
  void SendPendingAccessibilityNotifications();

  // IPC message handlers ------------------------------------------------------
  //
  // The documentation for these functions should be in
  // render_messages_internal.h for the message that the function is handling.

  void OnAccessibilityDoDefaultAction(int acc_obj_id);
  void OnAccessibilityNotificationsAck();
  void OnAllowBindings(int enabled_bindings_flags);
  void OnAllowScriptToClose(bool script_can_close);
  void OnAsyncFileOpened(base::PlatformFileError error_code,
                         IPC::PlatformFileForTransit file_for_transit,
                         int message_id);
  void OnPpapiBrokerChannelCreated(int request_id,
                                   base::ProcessHandle broker_process_handle,
                                   IPC::ChannelHandle handle);
  void OnCancelDownload(int32 download_id);
  void OnClearFocusedNode();
  void OnClosePage(const ViewMsg_ClosePage_Params& params);
#if defined(ENABLE_FLAPPER_HACKS)
  void OnConnectTcpACK(int request_id,
                       IPC::PlatformFileForTransit socket_for_transit,
                       const PP_Flash_NetAddress& local_addr,
                       const PP_Flash_NetAddress& remote_addr);
#endif
  void OnContextMenuClosed(
      const webkit_glue::CustomContextMenuContext& custom_context);
  void OnCopy();
  void OnCopyImageAt(int x, int y);
#if defined(OS_MACOSX)
  void OnCopyToFindPboard();
#endif
  void OnCut();
  void OnCSSInsertRequest(const std::wstring& frame_xpath,
                          const std::string& css,
                          const std::string& id);
  void OnCustomContextMenuAction(
      const webkit_glue::CustomContextMenuContext& custom_context,
      unsigned action);
  void OnDelete();
  void OnDeterminePageLanguage();
  void OnDisableScrollbarsForSmallWindows(
      const gfx::Size& disable_scrollbars_size_limit);
  void OnDisassociateFromPopupCount();
  void OnDragSourceEndedOrMoved(const gfx::Point& client_point,
                                const gfx::Point& screen_point,
                                bool ended,
                                WebKit::WebDragOperation drag_operation);
  void OnDragSourceSystemDragEnded();
  void OnDragTargetDrop(const gfx::Point& client_pt,
                        const gfx::Point& screen_pt);
  void OnDragTargetDragEnter(const WebDropData& drop_data,
                             const gfx::Point& client_pt,
                             const gfx::Point& screen_pt,
                             WebKit::WebDragOperationsMask operations_allowed);
  void OnDragTargetDragLeave();
  void OnDragTargetDragOver(const gfx::Point& client_pt,
                            const gfx::Point& screen_pt,
                            WebKit::WebDragOperationsMask operations_allowed);
  void OnEnablePreferredSizeChangedMode(int flags);
  void OnEnumerateDirectoryResponse(int id, const std::vector<FilePath>& paths);
  void OnExecuteEditCommand(const std::string& name, const std::string& value);
  void OnFileChooserResponse(const std::vector<FilePath>& paths);
  void OnFind(int request_id, const string16&, const WebKit::WebFindOptions&);
  void OnFindReplyAck();
  void OnEnableAccessibility();
  void OnInstallMissingPlugin();
  void OnDisplayPrerenderedPage();
  void OnMediaPlayerActionAt(const gfx::Point& location,
                             const WebKit::WebMediaPlayerAction& action);
  void OnMoveOrResizeStarted();
  void OnNavigate(const ViewMsg_Navigate_Params& params);
  void OnNetworkStateChanged(bool online);
  void OnPaste();
#if defined(OS_MACOSX)
  void OnPluginImeCompositionCompleted(const string16& text, int plugin_id);
#endif
  void OnRedo();
  void OnReloadFrame();
  void OnReplace(const string16& text);
  void OnReservePageIDRange(int size_of_range);
  void OnResetPageEncodingToDefault();
  void OnScriptEvalRequest(const string16& frame_xpath,
                           const string16& jscript,
                           int id,
                           bool notify_result);
  void OnSelectAll();
  void OnSetAccessibilityFocus(int acc_obj_id);
  void OnSetActive(bool active);
  void OnSetAltErrorPageURL(const GURL& gurl);
  void OnSetBackground(const SkBitmap& background);
  void OnSetWebUIProperty(const std::string& name, const std::string& value);
  void OnSetEditCommandsForNextKeyEvent(const EditCommands& edit_commands);
  void OnSetInitialFocus(bool reverse);
  void OnScrollFocusedEditableNodeIntoView();
  void OnSetPageEncoding(const std::string& encoding_name);
  void OnSetRendererPrefs(const RendererPreferences& renderer_prefs);
#if defined(OS_MACOSX)
  void OnSetWindowVisibility(bool visible);
#endif
  void OnSetZoomLevel(double zoom_level);
  void OnSetZoomLevelForLoadingURL(const GURL& url, double zoom_level);
  void OnShouldClose();
  void OnStop();
  void OnStopFinding(const ViewMsg_StopFinding_Params& params);
  void OnThemeChanged();
  void OnUndo();
  void OnUpdateTargetURLAck();
  void OnUpdateWebPreferences(const WebPreferences& prefs);
#if defined(OS_MACOSX)
  void OnWindowFrameChanged(const gfx::Rect& window_frame,
                            const gfx::Rect& view_frame);
  void OnSelectPopupMenuItem(int selected_index);
#endif
  void OnZoom(PageZoom::Function function);

  // Adding a new message handler? Please add it in alphabetical order above
  // and put it in the same position in the .cc file.

  // Misc private functions ----------------------------------------------------

  void AltErrorPageFinished(WebKit::WebFrame* frame,
                            const WebKit::WebURLError& original_error,
                            const std::string& html);

  // Check whether the preferred size has changed. This is called periodically
  // by preferred_size_change_timer_.
  void CheckPreferredSize();

  // This callback is triggered when DownloadFavicon completes, either
  // succesfully or with a failure. See DownloadFavicon for more
  // details.
  void DidDownloadFavicon(webkit_glue::ImageResourceFetcher* fetcher,
                          const SkBitmap& image);

  // Requests to download a favicon image. When done, the RenderView
  // is notified by way of DidDownloadFavicon. Returns true if the
  // request was successfully started, false otherwise. id is used to
  // uniquely identify the request and passed back to the
  // DidDownloadFavicon method. If the image has multiple frames, the
  // frame whose size is image_size is returned. If the image doesn't
  // have a frame at the specified size, the first is returned.
  bool DownloadFavicon(int id, const GURL& image_url, int image_size);

  GURL GetAlternateErrorPageURL(const GURL& failed_url,
                                ErrorPageType error_type);

  // Locates a sub frame with given xpath
  WebKit::WebFrame* GetChildFrame(const std::wstring& frame_xpath) const;

  WebUIBindings* GetWebUIBindings();

  // Should only be called if this object wraps a PluginDocument.
  WebKit::WebPlugin* GetWebPluginFromPluginDocument();

  // Inserts a string of CSS in a particular frame. |id| can be specified to
  // give the CSS style element an id, and (if specified) will replace the
  // element with the same id.
  void InsertCSS(const std::wstring& frame_xpath,
                 const std::string& css,
                 const std::string& id);

  // Returns false unless this is a top-level navigation that crosses origins.
  bool IsNonLocalTopLevelNavigation(const GURL& url,
                                    WebKit::WebFrame* frame,
                                    WebKit::WebNavigationType type);

  bool MaybeLoadAlternateErrorPage(WebKit::WebFrame* frame,
                                   const WebKit::WebURLError& error,
                                   bool replace);

  // Starts nav_state_sync_timer_ if it isn't already running.
  void StartNavStateSyncTimerIfNecessary();

  // Dispatches the current navigation state to the browser. Called on a
  // periodic timer so we don't send too many messages.
  void SyncNavigationState();

#if defined(OS_LINUX)
  void UpdateFontRenderingFromRendererPrefs();
#else
  void UpdateFontRenderingFromRendererPrefs() {}
#endif

  // Update the target url and tell the browser that the target URL has changed.
  // If |url| is empty, show |fallback_url|.
  void UpdateTargetURL(const GURL& url, const GURL& fallback_url);

  // ---------------------------------------------------------------------------
  // ADDING NEW FUNCTIONS? Please keep private functions alphabetized and put
  // it in the same order in the .cc file as it was in the header.
  // ---------------------------------------------------------------------------

  // Settings ------------------------------------------------------------------

  WebPreferences webkit_preferences_;
  RendererPreferences renderer_preferences_;

  HostZoomLevels host_zoom_levels_;

  // Whether content state (such as form state, scroll position and page
  // contents) should be sent to the browser immediately. This is normally
  // false, but set to true by some tests.
  bool send_content_state_immediately_;

  // Bitwise-ORed set of extra bindings that have been enabled.  See
  // BindingsPolicy for details.
  int enabled_bindings_;

  // The alternate error page URL, if one exists.
  GURL alternate_error_page_url_;

  // If true, we send IPC messages when |preferred_size_| changes.
  bool send_preferred_size_changes_;

  // If non-empty, and |send_preferred_size_changes_| is true, disable drawing
  // scroll bars on windows smaller than this size.  Used for windows that the
  // browser resizes to the size of the content, such as browser action popups.
  // If a render view is set to the minimum size of its content, webkit may add
  // scroll bars.  This makes sense for fixed sized windows, but it does not
  // make sense when the size of the view was chosen to fit the content.
  // This setting ensures that no scroll bars are drawn.  The size limit exists
  // because if the view grows beyond a size known to the browser, scroll bars
  // should be drawn.
  gfx::Size disable_scrollbars_size_limit_;

  // Loading state -------------------------------------------------------------

  // True if the top level frame is currently being loaded.
  bool is_loading_;

  // The gesture that initiated the current navigation.
  NavigationGesture navigation_gesture_;

  // Used for popups.
  bool opened_by_user_gesture_;
  GURL creator_url_;

  // Whether this RenderView was created by a frame that was suppressing its
  // opener. If so, we may want to load pages in a separate process.  See
  // decidePolicyForNavigation for details.
  bool opener_suppressed_;

  // If we are handling a top-level client-side redirect, this tracks the URL
  // of the page that initiated it. Specifically, when a load is committed this
  // is used to determine if that load originated from a client-side redirect.
  // It is empty if there is no top-level client-side redirect.
  GURL completed_client_redirect_src_;

  // Holds state pertaining to a navigation that we initiated.  This is held by
  // the WebDataSource::ExtraData attribute.  We use pending_navigation_state_
  // as a temporary holder for the state until the WebDataSource corresponding
  // to the new navigation is created.  See DidCreateDataSource.
  scoped_ptr<NavigationState> pending_navigation_state_;

  // Timer used to delay the updating of nav state (see SyncNavigationState).
  base::OneShotTimer<RenderView> nav_state_sync_timer_;

  // True if the RenderView is currently prerendering a page.
  bool is_prerendering_;

  // Page IDs ------------------------------------------------------------------
  //
  // Page IDs allow the browser to identify pages in each renderer process for
  // keeping back/forward history in sync.

  // ID of the current page.  Note that this is NOT updated for every main
  // frame navigation, only for "regular" navigations that go into session
  // history. In particular, client redirects, like the page cycler uses
  // (document.location.href="foo") do not count as regular navigations and do
  // not increment the page id.
  int32 page_id_;

  // Indicates the ID of the last page that we sent a FrameNavigate to the
  // browser for. This is used to determine if the most recent transition
  // generated a history entry (less than page_id_), or not (equal to or
  // greater than). Note that this will be greater than page_id_ if the user
  // goes back.
  int32 last_page_id_sent_to_browser_;

  // The next available page ID to use. This ensures that the page IDs are
  // globally unique in the renderer.
  static int32 next_page_id_;

  // Page info -----------------------------------------------------------------

  // The last gotten main frame's encoding.
  std::string last_encoding_name_;

  int history_list_offset_;
  int history_list_length_;

  // True if the page has any frame-level unload or beforeunload listeners.
  bool has_unload_listener_;

  // UI state ------------------------------------------------------------------

  // The state of our target_url transmissions. When we receive a request to
  // send a URL to the browser, we set this to TARGET_INFLIGHT until an ACK
  // comes back - if a new request comes in before the ACK, we store the new
  // URL in pending_target_url_ and set the status to TARGET_PENDING. If an
  // ACK comes back and we are in TARGET_PENDING, we send the stored URL and
  // revert to TARGET_INFLIGHT.
  //
  // We don't need a queue of URLs to send, as only the latest is useful.
  enum {
    TARGET_NONE,
    TARGET_INFLIGHT,  // We have a request in-flight, waiting for an ACK
    TARGET_PENDING    // INFLIGHT + we have a URL waiting to be sent
  } target_url_status_;

  // The URL we show the user in the status bar. We use this to determine if we
  // want to send a new one (we do not need to send duplicates). It will be
  // equal to either |mouse_over_url_| or |focus_url_|, depending on which was
  // updated last.
  GURL target_url_;

  // The URL the user's mouse is hovering over.
  GURL mouse_over_url_;

  // The URL that has keyboard focus.
  GURL focus_url_;

  // The next target URL we want to send to the browser.
  GURL pending_target_url_;

  // The text selection the last time DidChangeSelection got called.
  std::string last_selection_;

  // View ----------------------------------------------------------------------

  // Cache the preferred size of the page in order to prevent sending the IPC
  // when layout() recomputes but doesn't actually change sizes.
  gfx::Size preferred_size_;

  // Nasty hack. WebKit does not send us events when the preferred size changes,
  // so we must poll it. See also:
  // https://bugs.webkit.org/show_bug.cgi?id=32807.
  base::RepeatingTimer<RenderView> preferred_size_change_timer_;

#if defined(OS_MACOSX)
  // Track the fake plugin window handles allocated on the browser side for
  // the accelerated compositor and (currently) accelerated plugins so that
  // we can discard them when the view goes away.
  std::set<gfx::PluginWindowHandle> fake_plugin_window_handles_;
#endif

  // Plugins -------------------------------------------------------------------

  // Remember the first uninstalled plugin, so that we can ask the plugin
  // to install itself when user clicks on the info bar.
  base::WeakPtr<webkit::npapi::WebPluginDelegate> first_default_plugin_;

  PepperPluginDelegateImpl pepper_delegate_;

  // All the currently active plugin delegates for this RenderView; kept so that
  // we can enumerate them to send updates about things like window location
  // or tab focus and visibily. These are non-owning references.
  std::set<WebPluginDelegateProxy*> plugin_delegates_;

  // Helper objects ------------------------------------------------------------

  ScopedRunnableMethodFactory<RenderView> accessibility_method_factory_;

  RendererWebCookieJarImpl cookie_jar_;

  // The next group of objects all implement RenderViewObserver, so are deleted
  // along with the RenderView automatically.  This is why we just store weak
  // references.

  // Holds a reference to the service which provides desktop notifications.
  NotificationProvider* notification_provider_;

  // The geolocation dispatcher attached to this view, lazily initialized.
  GeolocationDispatcher* geolocation_dispatcher_;

  // The speech dispatcher attached to this view, lazily initialized.
  SpeechInputDispatcher* speech_input_dispatcher_;

  // Device orientation dispatcher attached to this view; lazily initialized.
  DeviceOrientationDispatcher* device_orientation_dispatcher_;

  scoped_refptr<AudioMessageFilter> audio_message_filter_;

  // Handles accessibility requests into the renderer side, as well as
  // maintains the cache and other features of the accessibility tree.
  scoped_ptr<WebKit::WebAccessibilityCache> accessibility_;

  // Collect renderer accessibility notifications until they are ready to be
  // sent to the browser.
  std::vector<RendererAccessibilityNotification>
      pending_accessibility_notifications_;

  // Set if we are waiting for a accessibility notification ack.
  bool accessibility_ack_pending_;

  // Dispatches all P2P socket used by the renderer.
  P2PSocketDispatcher* p2p_socket_dispatcher_;

  // Misc ----------------------------------------------------------------------

  // The current and pending file chooser completion objects. If the queue is
  // nonempty, the first item represents the currently running file chooser
  // callback, and the remaining elements are the other file chooser completion
  // still waiting to be run (in order).
  struct PendingFileChooser;
  std::deque< linked_ptr<PendingFileChooser> > file_chooser_completions_;

  // The current directory enumeration callback
  std::map<int, WebKit::WebFileChooserCompletion*> enumeration_completions_;
  int enumeration_completion_id_;

  // The SessionStorage namespace that we're assigned to has an ID, and that ID
  // is passed to us upon creation.  WebKit asks for this ID upon first use and
  // uses it whenever asking the browser process to allocate new storage areas.
  int64 session_storage_namespace_id_;

  // The total number of unrequested popups that exist and can be followed back
  // to a common opener. This count is shared among all RenderViews created
  // with createView(). All popups are treated as unrequested until
  // specifically instructed otherwise by the Browser process.
  scoped_refptr<SharedRenderViewCounter> shared_popup_counter_;

  // Whether this is a top level window (instead of a popup). Top level windows
  // shouldn't count against their own |shared_popup_counter_|.
  bool decrement_shared_popup_at_destruction_;

  // If the browser hasn't sent us an ACK for the last FindReply we sent
  // to it, then we need to queue up the message (keeping only the most
  // recent message if new ones come in).
  scoped_ptr<IPC::Message> queued_find_reply_message_;

  // Stores edit commands associated to the next key event.
  // Shall be cleared as soon as the next key event is processed.
  EditCommands edit_commands_;

  // Allows Web UI pages (new tab page, etc.) to talk to the browser. The JS
  // object is only exposed when Web UI bindings are enabled.
  scoped_ptr<WebUIBindings> web_ui_bindings_;

  // The external popup for the currently showing select popup.
  scoped_ptr<ExternalPopupMenu> external_popup_menu_;

  // The node that the context menu was pressed over.
  WebKit::WebNode context_menu_node_;

  // Reports load progress to the browser.
  scoped_ptr<LoadProgressTracker> load_progress_tracker_;

  // All the registered observers.  We expect this list to be small, so vector
  // is fine.
  ObserverList<RenderViewObserver> observers_;

  // ---------------------------------------------------------------------------
  // ADDING NEW DATA? Please see if it fits appropriately in one of the above
  // sections rather than throwing it randomly at the end. If you're adding a
  // bunch of stuff, you should probably create a helper class and put your
  // data and methods on that to avoid bloating RenderView more.  You can use
  // the Observer interface to filter IPC messages and receive frame change
  // notifications.
  // ---------------------------------------------------------------------------

  DISALLOW_COPY_AND_ASSIGN(RenderView);
};

#endif  // CONTENT_RENDERER_RENDER_VIEW_H_
