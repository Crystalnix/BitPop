// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/web_contents/web_contents_impl.h"

#include <utility>

#include "base/command_line.h"
#include "base/metrics/histogram.h"
#include "base/metrics/stats_counters.h"
#include "base/string16.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/sys_info.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "content/browser/browser_plugin/old/browser_plugin_host.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/debugger/devtools_manager_impl.h"
#include "content/browser/dom_storage/session_storage_namespace_impl.h"
#include "content/browser/download/download_stats.h"
#include "content/browser/download/mhtml_generation_manager.h"
#include "content/browser/download/save_package.h"
#include "content/browser/gpu/gpu_data_manager_impl.h"
#include "content/browser/gpu/gpu_process_host.h"
#include "content/browser/host_zoom_map_impl.h"
#include "content/browser/intents/web_intents_dispatcher_impl.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/resource_dispatcher_host_impl.h"
#include "content/browser/site_instance_impl.h"
#include "content/browser/web_contents/interstitial_page_impl.h"
#include "content/browser/web_contents/navigation_entry_impl.h"
#include "content/browser/webui/web_ui_impl.h"
#include "content/common/intents_messages.h"
#include "content/common/ssl_status_serialization.h"
#include "content/common/view_messages.h"
#include "content/port/browser/render_view_host_delegate_view.h"
#include "content/port/browser/render_widget_host_view_port.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/color_chooser.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/devtools_agent_host_registry.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/download_url_parameters.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/javascript_dialogs.h"
#include "content/public/browser/load_from_memory_cache_details.h"
#include "content/public/browser/load_notification_details.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/resource_request_details.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/browser/web_ui_controller_factory.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_restriction.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/url_constants.h"
#include "net/base/mime_util.h"
#include "net/base/net_util.h"
#include "net/base/network_change_notifier.h"
#include "net/url_request/url_request_context_getter.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebCompositor.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "ui/base/layout.h"
#include "ui/base/ui_base_switches.h"
#include "ui/gfx/display.h"
#include "ui/gfx/screen.h"
#include "ui/gl/gl_switches.h"
#include "webkit/glue/web_intent_data.h"
#include "webkit/glue/web_intent_service_data.h"
#include "webkit/glue/webpreferences.h"

#if defined(OS_MACOSX)
#include "base/mac/foundation_util.h"
#include "ui/surface/io_surface_support_mac.h"
#endif

// Cross-Site Navigations
//
// If a WebContentsImpl is told to navigate to a different web site (as
// determined by SiteInstance), it will replace its current RenderViewHost with
// a new RenderViewHost dedicated to the new SiteInstance.  This works as
// follows:
//
// - Navigate determines whether the destination is cross-site, and if so,
//   it creates a pending_render_view_host_.
// - The pending RVH is "suspended," so that no navigation messages are sent to
//   its renderer until the onbeforeunload JavaScript handler has a chance to
//   run in the current RVH.
// - The pending RVH tells CrossSiteRequestManager (a thread-safe singleton)
//   that it has a pending cross-site request.  ResourceDispatcherHost will
//   check for this when the response arrives.
// - The current RVH runs its onbeforeunload handler.  If it returns false, we
//   cancel all the pending logic.  Otherwise we allow the pending RVH to send
//   the navigation request to its renderer.
// - ResourceDispatcherHost receives a ResourceRequest on the IO thread for the
//   main resource load on the pending RVH. It checks CrossSiteRequestManager
//   to see that it is a cross-site request, and installs a
//   CrossSiteResourceHandler.
// - When RDH receives a response, the BufferedResourceHandler determines
//   whether it is a download.  If so, it sends a message to the new renderer
//   causing it to cancel the request, and the download proceeds. For now, the
//   pending RVH remains until the next DidNavigate event for this
//   WebContentsImpl. This isn't ideal, but it doesn't affect any functionality.
// - After RDH receives a response and determines that it is safe and not a
//   download, it pauses the response to first run the old page's onunload
//   handler.  It does this by asynchronously calling the OnCrossSiteResponse
//   method of WebContentsImpl on the UI thread, which sends a SwapOut message
//   to the current RVH.
// - Once the onunload handler is finished, a SwapOut_ACK message is sent to
//   the ResourceDispatcherHost, who unpauses the response.  Data is then sent
//   to the pending RVH.
// - The pending renderer sends a FrameNavigate message that invokes the
//   DidNavigate method.  This replaces the current RVH with the
//   pending RVH.
// - The previous renderer is kept swapped out in RenderViewHostManager in case
//   the user goes back.  The process only stays live if another tab is using
//   it, but if so, the existing frame relationships will be maintained.

using content::BrowserContext;
using content::DevToolsAgentHost;
using content::DevToolsAgentHostRegistry;
using content::DevToolsManagerImpl;
using content::DownloadItem;
using content::DownloadManager;
using content::DownloadUrlParameters;
using content::GlobalRequestID;
using content::HostZoomMap;
using content::InterstitialPage;
using content::LoadNotificationDetails;
using content::NativeWebKeyboardEvent;
using content::NavigationController;
using content::NavigationEntry;
using content::NavigationEntryImpl;
using content::OpenURLParams;
using content::RenderViewHost;
using content::RenderViewHostDelegate;
using content::RenderViewHostDelegateView;
using content::RenderViewHostImpl;
using content::RenderWidgetHost;
using content::RenderWidgetHostImpl;
using content::RenderWidgetHostView;
using content::RenderWidgetHostViewPort;
using content::ResourceDispatcherHostImpl;
using content::SSLStatus;
using content::SessionStorageNamespace;
using content::SiteInstance;
using content::UserMetricsAction;
using content::WebContents;
using content::WebContentsObserver;
using content::WebUI;
using content::WebUIController;
using content::WebUIControllerFactory;
using webkit_glue::WebPreferences;

namespace {

// Amount of time we wait between when a key event is received and the renderer
// is queried for its state and pushed to the NavigationEntry.
const int kQueryStateDelay = 5000;

const int kSyncWaitDelay = 40;

const char kDotGoogleDotCom[] = ".google.com";

#if defined(OS_WIN)

BOOL CALLBACK InvalidateWindow(HWND hwnd, LPARAM lparam) {
  // Note: erase is required to properly paint some widgets borders. This can
  // be seen with textfields.
  InvalidateRect(hwnd, NULL, TRUE);
  return TRUE;
}
#endif

ViewMsg_Navigate_Type::Value GetNavigationType(
    content::BrowserContext* browser_context, const NavigationEntryImpl& entry,
    NavigationController::ReloadType reload_type) {
  switch (reload_type) {
    case NavigationControllerImpl::RELOAD:
      return ViewMsg_Navigate_Type::RELOAD;
    case NavigationControllerImpl::RELOAD_IGNORING_CACHE:
      return ViewMsg_Navigate_Type::RELOAD_IGNORING_CACHE;
    case NavigationControllerImpl::RELOAD_ORIGINAL_REQUEST_URL:
      return ViewMsg_Navigate_Type::RELOAD_ORIGINAL_REQUEST_URL;
    case NavigationControllerImpl::NO_RELOAD:
      break;  // Fall through to rest of function.
  }

  // |RenderViewImpl::PopulateStateFromPendingNavigationParams| differentiates
  // between |RESTORE_WITH_POST| and |RESTORE|.
  if (entry.restore_type() == NavigationEntryImpl::RESTORE_LAST_SESSION &&
      browser_context->DidLastSessionExitCleanly()) {
    if (entry.GetHasPostData())
      return ViewMsg_Navigate_Type::RESTORE_WITH_POST;
    return ViewMsg_Navigate_Type::RESTORE;
  }

  return ViewMsg_Navigate_Type::NORMAL;
}

void MakeNavigateParams(const NavigationEntryImpl& entry,
                        const NavigationControllerImpl& controller,
                        content::WebContentsDelegate* delegate,
                        NavigationController::ReloadType reload_type,
                        const std::string& embedder_channel_name,
                        int embedder_container_id,
                        ViewMsg_Navigate_Params* params) {
  params->page_id = entry.GetPageID();
  params->pending_history_list_offset = controller.GetIndexOfEntry(&entry);
  params->current_history_list_offset = controller.GetLastCommittedEntryIndex();
  params->current_history_list_length = controller.GetEntryCount();
  if (!entry.GetBaseURLForDataURL().is_empty()) {
    params->base_url_for_data_url = entry.GetBaseURLForDataURL();
    params->history_url_for_data_url = entry.GetVirtualURL();
  }
  params->referrer = entry.GetReferrer();
  params->transition = entry.GetTransitionType();
  params->state = entry.GetContentState();
  params->navigation_type =
      GetNavigationType(controller.GetBrowserContext(), entry, reload_type);
  params->request_time = base::Time::Now();
  params->extra_headers = entry.extra_headers();
  params->transferred_request_child_id =
      entry.transferred_global_request_id().child_id;
  params->transferred_request_request_id =
      entry.transferred_global_request_id().request_id;
  params->is_overriding_user_agent = entry.GetIsOverridingUserAgent();
  // Avoid downloading when in view-source mode.
  params->allow_download = !entry.IsViewSourceMode();
  params->embedder_channel_name = embedder_channel_name;
  params->embedder_container_id = embedder_container_id;
  params->is_post = entry.GetHasPostData();
  if(entry.GetBrowserInitiatedPostData()) {
      params->browser_initiated_post_data.assign(
          entry.GetBrowserInitiatedPostData()->front(),
          entry.GetBrowserInitiatedPostData()->front() +
              entry.GetBrowserInitiatedPostData()->size());

  }

  if (reload_type == NavigationControllerImpl::RELOAD_ORIGINAL_REQUEST_URL &&
      entry.GetOriginalRequestURL().is_valid() && !entry.GetHasPostData()) {
    // We may have been redirected when navigating to the current URL.
    // Use the URL the user originally intended to visit, if it's valid and if a
    // POST wasn't involved; the latter case avoids issues with sending data to
    // the wrong page.
    params->url = entry.GetOriginalRequestURL();
  } else {
    params->url = entry.GetURL();
  }

  if (delegate)
    delegate->AddNavigationHeaders(params->url, &params->extra_headers);
}

int GetSwitchValueAsInt(
    const CommandLine& command_line,
    const std::string& switch_string,
    int min_value) {
  std::string string_value = command_line.GetSwitchValueASCII(switch_string);
  int int_value;
  if (base::StringToInt(string_value, &int_value))
    return std::max(min_value, int_value);
  else
    return min_value;
}

}  // namespace

namespace content {

WebContents* WebContents::Create(
    BrowserContext* browser_context,
    SiteInstance* site_instance,
    int routing_id,
    const WebContents* base_web_contents,
    SessionStorageNamespace* session_storage_namespace) {
  return new WebContentsImpl(
      browser_context,
      site_instance,
      routing_id,
      static_cast<const WebContentsImpl*>(base_web_contents),
      NULL,
      static_cast<SessionStorageNamespaceImpl*>(session_storage_namespace));
}

WebContents* WebContents::FromRenderViewHost(const RenderViewHost* rvh) {
  return rvh->GetDelegate()->GetAsWebContents();
}

}

// WebContentsImpl -------------------------------------------------------------

WebContentsImpl::WebContentsImpl(
    content::BrowserContext* browser_context,
    SiteInstance* site_instance,
    int routing_id,
    const WebContentsImpl* base_web_contents,
    WebContentsImpl* opener,
    SessionStorageNamespaceImpl* session_storage_namespace)
    : delegate_(NULL),
      ALLOW_THIS_IN_INITIALIZER_LIST(controller_(
          this, browser_context, session_storage_namespace)),
      render_view_host_delegate_view_(NULL),
      opener_(opener),
      ALLOW_THIS_IN_INITIALIZER_LIST(render_manager_(this, this, this)),
      is_loading_(false),
      crashed_status_(base::TERMINATION_STATUS_STILL_RUNNING),
      crashed_error_code_(0),
      waiting_for_response_(false),
      load_state_(net::LOAD_STATE_IDLE, string16()),
      upload_size_(0),
      upload_position_(0),
      displayed_insecure_content_(false),
      capturing_contents_(false),
      is_being_destroyed_(false),
      notify_disconnection_(false),
      dialog_creator_(NULL),
#if defined(OS_WIN)
      message_box_active_(CreateEvent(NULL, TRUE, FALSE, NULL)),
#endif
      is_showing_before_unload_dialog_(false),
      opener_web_ui_type_(WebUI::kNoWebUI),
      closed_by_user_gesture_(false),
      minimum_zoom_percent_(
          static_cast<int>(content::kMinimumZoomFactor * 100)),
      maximum_zoom_percent_(
          static_cast<int>(content::kMaximumZoomFactor * 100)),
      temporary_zoom_settings_(false),
      content_restrictions_(0),
      color_chooser_(NULL) {
  render_manager_.Init(browser_context, site_instance, routing_id);

  view_.reset(content::GetContentClient()->browser()->
      OverrideCreateWebContentsView(this, &render_view_host_delegate_view_));
  if (view_.get()) {
    CHECK(render_view_host_delegate_view_);
  } else {
    content::WebContentsViewDelegate* delegate =
        content::GetContentClient()->browser()->GetWebContentsViewDelegate(
            this);
    view_.reset(CreateWebContentsView(
        this, delegate, &render_view_host_delegate_view_));
    CHECK(render_view_host_delegate_view_);
  }
  CHECK(view_.get());

  // We have the initial size of the view be based on the size of the view of
  // the passed in WebContents.
  view_->CreateView(base_web_contents ?
      base_web_contents->GetView()->GetContainerSize() : gfx::Size());

  // Listen for whether our opener gets destroyed.
  if (opener_) {
    registrar_.Add(this, content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
                   content::Source<WebContents>(opener_));
  }

  registrar_.Add(this,
                 content::NOTIFICATION_RENDER_WIDGET_HOST_DESTROYED,
                 content::NotificationService::AllBrowserContextsAndSources());

#if defined(ENABLE_JAVA_BRIDGE)
  java_bridge_dispatcher_host_manager_.reset(
      new JavaBridgeDispatcherHostManager(this));
#endif

  browser_plugin_host_.reset(new content::BrowserPluginHost(this));
}

WebContentsImpl::~WebContentsImpl() {
  is_being_destroyed_ = true;

  for (std::set<RenderWidgetHostImpl*>::iterator iter =
           created_widgets_.begin(); iter != created_widgets_.end(); ++iter) {
    (*iter)->DetachDelegate();
  }
  created_widgets_.clear();

  // Clear out any JavaScript state.
  if (dialog_creator_)
    dialog_creator_->ResetJavaScriptState(this);

  if (color_chooser_)
    color_chooser_->End();

  NotifyDisconnected();

  // Notify any observer that have a reference on this WebContents.
  content::NotificationService::current()->Notify(
      content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
      content::Source<WebContents>(this),
      content::NotificationService::NoDetails());

  // TODO(brettw) this should be moved to the view.
#if defined(OS_WIN) && !defined(USE_AURA)
  // If we still have a window handle, destroy it. GetNativeView can return
  // NULL if this contents was part of a window that closed.
  if (GetNativeView()) {
    RenderViewHost* host = GetRenderViewHost();
    if (host && host->GetView())
      RenderWidgetHostViewPort::FromRWHV(host->GetView())->WillWmDestroy();
  }
#endif

  // OnCloseStarted isn't called in unit tests.
  if (!close_start_time_.is_null()) {
    UMA_HISTOGRAM_TIMES("Tab.Close",
        base::TimeTicks::Now() - close_start_time_);
  }

  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    WebContentsImplDestroyed());

  SetDelegate(NULL);
}

WebPreferences WebContentsImpl::GetWebkitPrefs(RenderViewHost* rvh,
                                               const GURL& url) {
  WebPreferences prefs;

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();

  prefs.developer_extras_enabled = true;
  prefs.javascript_enabled =
      !command_line.HasSwitch(switches::kDisableJavaScript);
  prefs.web_security_enabled =
      !command_line.HasSwitch(switches::kDisableWebSecurity);
  prefs.plugins_enabled =
      !command_line.HasSwitch(switches::kDisablePlugins);
  prefs.java_enabled =
      !command_line.HasSwitch(switches::kDisableJava);

  prefs.uses_page_cache =
      command_line.HasSwitch(switches::kEnableFastback);
  prefs.remote_fonts_enabled =
      !command_line.HasSwitch(switches::kDisableRemoteFonts);
  prefs.xss_auditor_enabled =
      !command_line.HasSwitch(switches::kDisableXSSAuditor);
  prefs.application_cache_enabled =
      !command_line.HasSwitch(switches::kDisableApplicationCache);

  prefs.local_storage_enabled =
      !command_line.HasSwitch(switches::kDisableLocalStorage);
  prefs.databases_enabled =
      !command_line.HasSwitch(switches::kDisableDatabases);
#if defined(OS_ANDROID)
  prefs.webaudio_enabled =
      command_line.HasSwitch(switches::kEnableWebAudio);
#else
  prefs.webaudio_enabled =
      !command_line.HasSwitch(switches::kDisableWebAudio);
#endif

  prefs.experimental_webgl_enabled =
      GpuProcessHost::gpu_enabled() &&
      !command_line.HasSwitch(switches::kDisable3DAPIs) &&
#if defined(OS_ANDROID)
      command_line.HasSwitch(switches::kEnableExperimentalWebGL);
#else
      !command_line.HasSwitch(switches::kDisableExperimentalWebGL);
#endif

  prefs.flash_3d_enabled =
      GpuProcessHost::gpu_enabled() &&
      !command_line.HasSwitch(switches::kDisableFlash3d);
  prefs.flash_stage3d_enabled =
      GpuProcessHost::gpu_enabled() &&
      !command_line.HasSwitch(switches::kDisableFlashStage3d);

  prefs.gl_multisampling_enabled =
      !command_line.HasSwitch(switches::kDisableGLMultisampling);
  prefs.privileged_webgl_extensions_enabled =
      command_line.HasSwitch(switches::kEnablePrivilegedWebGLExtensions);
  prefs.site_specific_quirks_enabled =
      !command_line.HasSwitch(switches::kDisableSiteSpecificQuirks);
  prefs.allow_file_access_from_file_urls =
      command_line.HasSwitch(switches::kAllowFileAccessFromFiles);
  prefs.show_composited_layer_borders =
      command_line.HasSwitch(switches::kShowCompositedLayerBorders);
  prefs.show_composited_layer_tree =
      command_line.HasSwitch(switches::kShowCompositedLayerTree);
  prefs.show_fps_counter =
      command_line.HasSwitch(switches::kShowFPSCounter);
  prefs.show_paint_rects =
      command_line.HasSwitch(switches::kShowPaintRects);
  prefs.render_vsync_enabled =
      !command_line.HasSwitch(switches::kDisableGpuVsync);
  prefs.accelerated_compositing_enabled =
      GpuProcessHost::gpu_enabled() &&
      !command_line.HasSwitch(switches::kDisableAcceleratedCompositing);
  prefs.force_compositing_mode =
      command_line.HasSwitch(switches::kForceCompositingMode) &&
      !command_line.HasSwitch(switches::kDisableForceCompositingMode);
  prefs.fixed_position_compositing_enabled =
      command_line.HasSwitch(switches::kEnableCompositingForFixedPosition);
  prefs.accelerated_2d_canvas_enabled =
      GpuProcessHost::gpu_enabled() &&
      !command_line.HasSwitch(switches::kDisableAccelerated2dCanvas);
  prefs.deferred_2d_canvas_enabled =
      command_line.HasSwitch(switches::kEnableDeferred2dCanvas);
  prefs.accelerated_painting_enabled =
      GpuProcessHost::gpu_enabled() &&
      command_line.HasSwitch(switches::kEnableAcceleratedPainting);
  prefs.accelerated_filters_enabled =
      GpuProcessHost::gpu_enabled() &&
      command_line.HasSwitch(switches::kEnableAcceleratedFilters);
  prefs.accelerated_layers_enabled =
      prefs.accelerated_animation_enabled =
          !command_line.HasSwitch(switches::kDisableAcceleratedLayers);
  prefs.accelerated_plugins_enabled =
      !command_line.HasSwitch(switches::kDisableAcceleratedPlugins);
  prefs.accelerated_video_enabled =
      !command_line.HasSwitch(switches::kDisableAcceleratedVideo);
  prefs.fullscreen_enabled =
      !command_line.HasSwitch(switches::kDisableFullScreen);
  prefs.css_regions_enabled =
      command_line.HasSwitch(switches::kEnableCssRegions);
  prefs.css_shaders_enabled =
      command_line.HasSwitch(switches::kEnableCssShaders);
  prefs.css_variables_enabled =
      command_line.HasSwitch(switches::kEnableCssVariables);
  prefs.device_supports_touch =
      ui::GetDisplayLayout() == ui::LAYOUT_TOUCH;
#if defined(OS_ANDROID)
  prefs.device_supports_mouse = false;
#endif

#if defined(OS_MACOSX)
  bool default_enable_scroll_animator = true;
#else
  // On CrOS, the launcher always passes in the --enable flag.
  bool default_enable_scroll_animator = false;
#endif
  prefs.enable_scroll_animator = default_enable_scroll_animator;
  if (command_line.HasSwitch(switches::kEnableSmoothScrolling))
    prefs.enable_scroll_animator = true;
  if (command_line.HasSwitch(switches::kDisableSmoothScrolling))
    prefs.enable_scroll_animator = false;

  prefs.visual_word_movement_enabled =
      command_line.HasSwitch(switches::kEnableVisualWordMovement);

  {  // Certain GPU features might have been blacklisted.
    GpuDataManagerImpl* gpu_data_manager = GpuDataManagerImpl::GetInstance();
    DCHECK(gpu_data_manager);
    uint32 blacklist_type = gpu_data_manager->GetGpuFeatureType();
    if (blacklist_type & content::GPU_FEATURE_TYPE_ACCELERATED_COMPOSITING)
      prefs.accelerated_compositing_enabled = false;
    if (blacklist_type & content::GPU_FEATURE_TYPE_WEBGL)
      prefs.experimental_webgl_enabled = false;
    if (blacklist_type & content::GPU_FEATURE_TYPE_FLASH3D)
      prefs.flash_3d_enabled = false;
    if (blacklist_type & content::GPU_FEATURE_TYPE_FLASH_STAGE3D)
      prefs.flash_stage3d_enabled = false;
    if (blacklist_type & content::GPU_FEATURE_TYPE_ACCELERATED_2D_CANVAS)
      prefs.accelerated_2d_canvas_enabled = false;
    if (blacklist_type & content::GPU_FEATURE_TYPE_MULTISAMPLING)
      prefs.gl_multisampling_enabled = false;

    // Accelerated video and animation are slower than regular when using a
    // software 3d rasterizer. 3D CSS may also be too slow to be worthwhile.
    if (gpu_data_manager->ShouldUseSoftwareRendering()) {
      prefs.accelerated_video_enabled = false;
      prefs.accelerated_animation_enabled = false;
      prefs.accelerated_layers_enabled = false;
      prefs.accelerated_plugins_enabled = false;
    }
  }

  if (ChildProcessSecurityPolicyImpl::GetInstance()->HasWebUIBindings(
          rvh->GetProcess()->GetID())) {
    prefs.loads_images_automatically = true;
    prefs.javascript_enabled = true;
  }

  prefs.is_online = !net::NetworkChangeNotifier::IsOffline();

  // Force accelerated compositing and 2d canvas off for chrome:, about: and
  // chrome-devtools: pages (unless it's specifically allowed).
  if ((url.SchemeIs(chrome::kChromeDevToolsScheme) ||
      url.SchemeIs(chrome::kChromeUIScheme) ||
      (url.SchemeIs(chrome::kAboutScheme) &&
       url.spec() != chrome::kAboutBlankURL)) &&
      !command_line.HasSwitch(switches::kAllowWebUICompositing)) {
    prefs.accelerated_compositing_enabled = false;
    prefs.accelerated_2d_canvas_enabled = false;
  }

#if !defined(WEBCOMPOSITOR_OWNS_SETTINGS)
  prefs.threaded_animation_enabled =
      !command_line.HasSwitch(switches::kDisableThreadedAnimation);
  prefs.per_tile_painting_enabled =
      command_line.HasSwitch(switches::kEnablePerTilePainting);
  prefs.partial_swap_enabled =
      command_line.HasSwitch(switches::kEnablePartialSwap);
#endif

  if (command_line.HasSwitch(switches::kDefaultTileWidth))
    prefs.default_tile_width =
        GetSwitchValueAsInt(command_line, switches::kDefaultTileWidth, 1);
  if (command_line.HasSwitch(switches::kDefaultTileHeight))
    prefs.default_tile_height =
        GetSwitchValueAsInt(command_line, switches::kDefaultTileHeight, 1);
  if (command_line.HasSwitch(switches::kMaxUntiledLayerWidth))
    prefs.max_untiled_layer_width =
        GetSwitchValueAsInt(command_line, switches::kMaxUntiledLayerWidth, 1);
  if (command_line.HasSwitch(switches::kMaxUntiledLayerHeight))
    prefs.max_untiled_layer_height =
        GetSwitchValueAsInt(command_line, switches::kMaxUntiledLayerHeight, 1);

  if (gfx::Screen::IsDIPEnabled()) {
    // Only apply when using DIP coordinate system as this setting interferes
    // with fixed layout mode.
    prefs.apply_default_device_scale_factor_in_compositor = true;
  }

  prefs.fixed_position_creates_stacking_context = !command_line.HasSwitch(
      switches::kDisableFixedPositionCreatesStackingContext);

  prefs.number_of_cpu_cores = base::SysInfo::NumberOfProcessors();

  content::GetContentClient()->browser()->OverrideWebkitPrefs(rvh, url, &prefs);

  return prefs;
}

NavigationControllerImpl& WebContentsImpl::GetControllerImpl() {
  return controller_;
}

RenderViewHostManager* WebContentsImpl::GetRenderManagerForTesting() {
  return &render_manager_;
}

bool WebContentsImpl::OnMessageReceived(RenderViewHost* render_view_host,
                                        const IPC::Message& message) {
  if (GetWebUI() &&
      static_cast<WebUIImpl*>(GetWebUI())->OnMessageReceived(message)) {
    return true;
  }

  ObserverListBase<WebContentsObserver>::Iterator it(observers_);
  WebContentsObserver* observer;
  while ((observer = it.GetNext()) != NULL)
    if (observer->OnMessageReceived(message))
      return true;

  // Message handlers should be aware of which RenderViewHost sent the
  // message, which is temporarily stored in message_source_.
  message_source_ = render_view_host;
  bool handled = true;
  bool message_is_ok = true;
  IPC_BEGIN_MESSAGE_MAP_EX(WebContentsImpl, message, message_is_ok)
    IPC_MESSAGE_HANDLER(IntentsHostMsg_RegisterIntentService,
                        OnRegisterIntentService)
    IPC_MESSAGE_HANDLER(IntentsHostMsg_WebIntentDispatch,
                        OnWebIntentDispatch)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DidLoadResourceFromMemoryCache,
                        OnDidLoadResourceFromMemoryCache)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DidDisplayInsecureContent,
                        OnDidDisplayInsecureContent)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DidRunInsecureContent,
                        OnDidRunInsecureContent)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DocumentLoadedInFrame,
                        OnDocumentLoadedInFrame)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DidFinishLoad, OnDidFinishLoad)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DidFailLoadWithError,
                        OnDidFailLoadWithError)
    IPC_MESSAGE_HANDLER(ViewHostMsg_UpdateContentRestrictions,
                        OnUpdateContentRestrictions)
    IPC_MESSAGE_HANDLER(ViewHostMsg_GoToEntryAtOffset, OnGoToEntryAtOffset)
    IPC_MESSAGE_HANDLER(ViewHostMsg_UpdateZoomLimits, OnUpdateZoomLimits)
    IPC_MESSAGE_HANDLER(ViewHostMsg_SaveURLAs, OnSaveURL)
    IPC_MESSAGE_HANDLER(ViewHostMsg_EnumerateDirectory, OnEnumerateDirectory)
    IPC_MESSAGE_HANDLER(ViewHostMsg_JSOutOfMemory, OnJSOutOfMemory)
    IPC_MESSAGE_HANDLER(ViewHostMsg_RegisterProtocolHandler,
                        OnRegisterProtocolHandler)
    IPC_MESSAGE_HANDLER(ViewHostMsg_Find_Reply, OnFindReply)
    IPC_MESSAGE_HANDLER(ViewHostMsg_CrashedPlugin, OnCrashedPlugin)
    IPC_MESSAGE_HANDLER(ViewHostMsg_AppCacheAccessed, OnAppCacheAccessed)
    IPC_MESSAGE_HANDLER(ViewHostMsg_OpenColorChooser, OnOpenColorChooser)
    IPC_MESSAGE_HANDLER(ViewHostMsg_EndColorChooser, OnEndColorChooser)
    IPC_MESSAGE_HANDLER(ViewHostMsg_SetSelectedColorInColorChooser,
                        OnSetSelectedColorInColorChooser)
    IPC_MESSAGE_HANDLER(ViewHostMsg_PepperPluginHung, OnPepperPluginHung)
    IPC_MESSAGE_HANDLER(ViewHostMsg_WebUISend, OnWebUISend)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP_EX()
  message_source_ = NULL;

  if (!message_is_ok) {
    content::RecordAction(UserMetricsAction("BadMessageTerminate_RVD"));
    GetRenderProcessHost()->ReceivedBadMessage();
  }

  return handled;
}

void WebContentsImpl::RunFileChooser(
    RenderViewHost* render_view_host,
    const content::FileChooserParams& params) {
  if (delegate_)
    delegate_->RunFileChooser(this, params);
}

NavigationController& WebContentsImpl::GetController() {
  return controller_;
}

const NavigationController& WebContentsImpl::GetController() const {
  return controller_;
}

content::BrowserContext* WebContentsImpl::GetBrowserContext() const {
  return controller_.GetBrowserContext();
}

const GURL& WebContentsImpl::GetURL() const {
  // We may not have a navigation entry yet
  NavigationEntry* entry = controller_.GetActiveEntry();
  return entry ? entry->GetVirtualURL() : GURL::EmptyGURL();
}


const base::PropertyBag* WebContentsImpl::GetPropertyBag() const {
  return &property_bag_;
}

base::PropertyBag* WebContentsImpl::GetPropertyBag() {
  return &property_bag_;
}

content::WebContentsDelegate* WebContentsImpl::GetDelegate() {
  return delegate_;
}

void WebContentsImpl::SetDelegate(content::WebContentsDelegate* delegate) {
  // TODO(cbentzel): remove this debugging code?
  if (delegate == delegate_)
    return;
  if (delegate_)
    delegate_->Detach(this);
  delegate_ = delegate;
  if (delegate_)
    delegate_->Attach(this);
}

content::RenderProcessHost* WebContentsImpl::GetRenderProcessHost() const {
  RenderViewHostImpl* host = render_manager_.current_host();
  return host ? host->GetProcess() : NULL;
}

RenderViewHost* WebContentsImpl::GetRenderViewHost() const {
  return render_manager_.current_host();
}

RenderWidgetHostView* WebContentsImpl::GetRenderWidgetHostView() const {
  return render_manager_.GetRenderWidgetHostView();
}

content::WebContentsView* WebContentsImpl::GetView() const {
  return view_.get();
}

content::WebUI* WebContentsImpl::CreateWebUI(const GURL& url) {
  WebUIControllerFactory* factory =
      content::GetContentClient()->browser()->GetWebUIControllerFactory();
  if (!factory)
    return NULL;
  WebUIImpl* web_ui = new WebUIImpl(this);
  WebUIController* controller =
      factory->CreateWebUIControllerForURL(web_ui, url);
  if (controller) {
    web_ui->SetController(controller);
    return web_ui;
  }

  delete web_ui;
  return NULL;
}

content::WebUI* WebContentsImpl::GetWebUI() const {
  return render_manager_.web_ui() ? render_manager_.web_ui()
      : render_manager_.pending_web_ui();
}

content::WebUI* WebContentsImpl::GetCommittedWebUI() const {
  return render_manager_.web_ui();
}

void WebContentsImpl::SetUserAgentOverride(const std::string& override) {
  if (GetUserAgentOverride() == override)
    return;

  renderer_preferences_.user_agent_override = override;

  // Send the new override string to the renderer.
  RenderViewHost* host = GetRenderViewHost();
  if (host)
    host->SyncRendererPrefs();

  // Reload the page if a load is currently in progress to avoid having
  // different parts of the page loaded using different user agents.
  NavigationEntry* entry = controller_.GetActiveEntry();
  if (is_loading_ && entry != NULL && entry->GetIsOverridingUserAgent())
    controller_.ReloadIgnoringCache(true);
}

const std::string& WebContentsImpl::GetUserAgentOverride() const {
  return renderer_preferences_.user_agent_override;
}

const string16& WebContentsImpl::GetTitle() const {
  // Transient entries take precedence. They are used for interstitial pages
  // that are shown on top of existing pages.
  NavigationEntry* entry = controller_.GetTransientEntry();
  std::string accept_languages =
      content::GetContentClient()->browser()->GetAcceptLangs(
          GetBrowserContext());
  if (entry) {
    return entry->GetTitleForDisplay(accept_languages);
  }
  WebUI* our_web_ui = render_manager_.pending_web_ui() ?
      render_manager_.pending_web_ui() : render_manager_.web_ui();
  if (our_web_ui) {
    // Don't override the title in view source mode.
    entry = controller_.GetActiveEntry();
    if (!(entry && entry->IsViewSourceMode())) {
      // Give the Web UI the chance to override our title.
      const string16& title = our_web_ui->GetOverriddenTitle();
      if (!title.empty())
        return title;
    }
  }

  // We use the title for the last committed entry rather than a pending
  // navigation entry. For example, when the user types in a URL, we want to
  // keep the old page's title until the new load has committed and we get a new
  // title.
  entry = controller_.GetLastCommittedEntry();
  if (entry) {
    return entry->GetTitleForDisplay(accept_languages);
  }

  // |page_title_when_no_navigation_entry_| is finally used
  // if no title cannot be retrieved.
  return page_title_when_no_navigation_entry_;
}

int32 WebContentsImpl::GetMaxPageID() {
  return GetMaxPageIDForSiteInstance(GetSiteInstance());
}

int32 WebContentsImpl::GetMaxPageIDForSiteInstance(
    SiteInstance* site_instance) {
  if (max_page_ids_.find(site_instance->GetId()) == max_page_ids_.end())
    max_page_ids_[site_instance->GetId()] = -1;

  return max_page_ids_[site_instance->GetId()];
}

void WebContentsImpl::UpdateMaxPageID(int32 page_id) {
  UpdateMaxPageIDForSiteInstance(GetSiteInstance(), page_id);
}

void WebContentsImpl::UpdateMaxPageIDForSiteInstance(
    SiteInstance* site_instance, int32 page_id) {
  if (GetMaxPageIDForSiteInstance(site_instance) < page_id)
    max_page_ids_[site_instance->GetId()] = page_id;
}

void WebContentsImpl::CopyMaxPageIDsFrom(WebContentsImpl* web_contents) {
  max_page_ids_ = web_contents->max_page_ids_;
}

SiteInstance* WebContentsImpl::GetSiteInstance() const {
  return render_manager_.current_host()->GetSiteInstance();
}

SiteInstance* WebContentsImpl::GetPendingSiteInstance() const {
  RenderViewHost* dest_rvh = render_manager_.pending_render_view_host() ?
      render_manager_.pending_render_view_host() :
      render_manager_.current_host();
  return dest_rvh->GetSiteInstance();
}

bool WebContentsImpl::IsLoading() const {
  return is_loading_;
}

bool WebContentsImpl::IsWaitingForResponse() const {
  return waiting_for_response_;
}

const net::LoadStateWithParam& WebContentsImpl::GetLoadState() const {
  return load_state_;
}

const string16& WebContentsImpl::GetLoadStateHost() const {
  return load_state_host_;
}

uint64 WebContentsImpl::GetUploadSize() const {
  return upload_size_;
}

uint64 WebContentsImpl::GetUploadPosition() const {
  return upload_position_;
}

const std::string& WebContentsImpl::GetEncoding() const {
  return encoding_;
}

bool WebContentsImpl::DisplayedInsecureContent() const {
  return displayed_insecure_content_;
}

void WebContentsImpl::SetCapturingContents(bool cap) {
  capturing_contents_ = cap;
}

bool WebContentsImpl::IsCrashed() const {
  return (crashed_status_ == base::TERMINATION_STATUS_PROCESS_CRASHED ||
          crashed_status_ == base::TERMINATION_STATUS_ABNORMAL_TERMINATION ||
          crashed_status_ == base::TERMINATION_STATUS_PROCESS_WAS_KILLED);
}

void WebContentsImpl::SetIsCrashed(base::TerminationStatus status,
                                   int error_code) {
  if (status == crashed_status_)
    return;

  crashed_status_ = status;
  crashed_error_code_ = error_code;
  NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TAB);
}

base::TerminationStatus WebContentsImpl::GetCrashedStatus() const {
  return crashed_status_;
}

bool WebContentsImpl::IsBeingDestroyed() const {
  return is_being_destroyed_;
}

void WebContentsImpl::NotifyNavigationStateChanged(unsigned changed_flags) {
  if (delegate_)
    delegate_->NavigationStateChanged(this, changed_flags);
}

base::TimeTicks WebContentsImpl::GetLastSelectedTime() const {
  return last_selected_time_;
}

void WebContentsImpl::WasShown() {
  controller_.SetActive(true);
  RenderWidgetHostViewPort* rwhv =
      RenderWidgetHostViewPort::FromRWHV(GetRenderWidgetHostView());
  if (rwhv) {
    rwhv->WasShown();
#if defined(OS_MACOSX)
    rwhv->SetActive(true);
#endif
  }

  last_selected_time_ = base::TimeTicks::Now();

  FOR_EACH_OBSERVER(WebContentsObserver, observers_, WasShown());

  // The resize rect might have changed while this was inactive -- send the new
  // one to make sure it's up to date.
  RenderViewHostImpl* rvh =
      static_cast<RenderViewHostImpl*>(GetRenderViewHost());
  if (rvh) {
    rvh->ResizeRectChanged(GetRootWindowResizerRect());
  }

  bool is_visible = true;
  content::NotificationService::current()->Notify(
      content::NOTIFICATION_WEB_CONTENTS_VISIBILITY_CHANGED,
      content::Source<WebContents>(this),
      content::Details<bool>(&is_visible));
}

void WebContentsImpl::WasHidden() {
  if (!capturing_contents_) {
    // |GetRenderViewHost()| can be NULL if the user middle clicks a link to
    // open a tab in then background, then closes the tab before selecting it.
    // This is because closing the tab calls WebContentsImpl::Destroy(), which
    // removes the |GetRenderViewHost()|; then when we actually destroy the
    // window, OnWindowPosChanged() notices and calls WasHidden() (which
    // calls us).
    RenderWidgetHostViewPort* rwhv =
        RenderWidgetHostViewPort::FromRWHV(GetRenderWidgetHostView());
    if (rwhv)
      rwhv->WasHidden();
  }

  bool is_visible = false;
  content::NotificationService::current()->Notify(
      content::NOTIFICATION_WEB_CONTENTS_VISIBILITY_CHANGED,
      content::Source<WebContents>(this),
      content::Details<bool>(&is_visible));
}

bool WebContentsImpl::NeedToFireBeforeUnload() {
  // TODO(creis): Should we fire even for interstitial pages?
  return WillNotifyDisconnection() &&
      !ShowingInterstitialPage() &&
      !static_cast<RenderViewHostImpl*>(
          GetRenderViewHost())->SuddenTerminationAllowed();
}

void WebContentsImpl::Stop() {
  render_manager_.Stop();
  FOR_EACH_OBSERVER(WebContentsObserver, observers_, StopNavigation());
}

WebContents* WebContentsImpl::Clone() {
  // We use our current SiteInstance since the cloned entry will use it anyway.
  // We pass |this| for the |base_web_contents| to size the view correctly, and
  // our own opener so that the cloned page can access it if it was before.
  WebContentsImpl* tc = new WebContentsImpl(
      GetBrowserContext(), GetSiteInstance(),
      MSG_ROUTING_NONE, this, opener_, NULL);
  tc->GetControllerImpl().CopyStateFrom(controller_);
  return tc;
}

void WebContentsImpl::AddNewContents(WebContents* new_contents,
                                     WindowOpenDisposition disposition,
                                     const gfx::Rect& initial_pos,
                                     bool user_gesture) {
  if (!delegate_)
    return;

  delegate_->AddNewContents(this, new_contents, disposition, initial_pos,
                            user_gesture);
}

gfx::NativeView WebContentsImpl::GetContentNativeView() const {
  return view_->GetContentNativeView();
}

gfx::NativeView WebContentsImpl::GetNativeView() const {
  return view_->GetNativeView();
}

void WebContentsImpl::GetContainerBounds(gfx::Rect* out) const {
  view_->GetContainerBounds(out);
}

void WebContentsImpl::Focus() {
  view_->Focus();
}

void WebContentsImpl::Observe(int type,
                              const content::NotificationSource& source,
                              const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_WEB_CONTENTS_DESTROYED:
      OnWebContentsDestroyed(
          content::Source<content::WebContents>(source).ptr());
      break;
    case content::NOTIFICATION_RENDER_WIDGET_HOST_DESTROYED: {
      RenderWidgetHost* host = content::Source<RenderWidgetHost>(source).ptr();
      for (PendingWidgetViews::iterator i = pending_widget_views_.begin();
           i != pending_widget_views_.end(); ++i) {
        if (host->GetView() == i->second) {
          pending_widget_views_.erase(i);
          break;
        }
      }
      break;
    }
    default:
      NOTREACHED();
  }
}

void WebContentsImpl::OnWebContentsDestroyed(WebContents* web_contents) {
  // Clear the opener if it has been closed.
  if (web_contents == opener_) {
    registrar_.Remove(this, content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
                      content::Source<WebContents>(opener_));
    opener_ = NULL;
  }
}

void WebContentsImpl::AddObserver(WebContentsObserver* observer) {
  observers_.AddObserver(observer);
}

void WebContentsImpl::RemoveObserver(WebContentsObserver* observer) {
  observers_.RemoveObserver(observer);
}

void WebContentsImpl::Activate() {
  if (delegate_)
    delegate_->ActivateContents(this);
}

void WebContentsImpl::Deactivate() {
  if (delegate_)
    delegate_->DeactivateContents(this);
}

void WebContentsImpl::LostCapture() {
  if (delegate_)
    delegate_->LostCapture();
}

void WebContentsImpl::RenderWidgetDeleted(
    RenderWidgetHostImpl* render_widget_host) {
  if (is_being_destroyed_) {
    // |created_widgets_| might have been destroyed.
    return;
  }

  std::set<RenderWidgetHostImpl*>::iterator iter =
      created_widgets_.find(render_widget_host);
  if (iter != created_widgets_.end())
    created_widgets_.erase(iter);
}

bool WebContentsImpl::PreHandleKeyboardEvent(
    const NativeWebKeyboardEvent& event,
    bool* is_keyboard_shortcut) {
  return delegate_ &&
      delegate_->PreHandleKeyboardEvent(event, is_keyboard_shortcut);
}

void WebContentsImpl::HandleKeyboardEvent(const NativeWebKeyboardEvent& event) {
  if (delegate_)
    delegate_->HandleKeyboardEvent(event);
}

void WebContentsImpl::HandleMouseDown() {
  if (delegate_)
    delegate_->HandleMouseDown();
}

void WebContentsImpl::HandleMouseUp() {
  if (delegate_)
    delegate_->HandleMouseUp();
}

void WebContentsImpl::HandlePointerActivate() {
  if (delegate_)
    delegate_->HandlePointerActivate();
}

void WebContentsImpl::HandleGestureBegin() {
  if (delegate_)
    delegate_->HandleGestureBegin();
}

void WebContentsImpl::HandleGestureEnd() {
  if (delegate_)
    delegate_->HandleGestureEnd();
}

void WebContentsImpl::ToggleFullscreenMode(bool enter_fullscreen) {
  if (delegate_)
    delegate_->ToggleFullscreenModeForTab(this, enter_fullscreen);
}

bool WebContentsImpl::IsFullscreenForCurrentTab() const {
  return delegate_ ? delegate_->IsFullscreenForTabOrPending(this) : false;
}

void WebContentsImpl::RequestToLockMouse(bool user_gesture,
                                         bool last_unlocked_by_target) {
  if (delegate_) {
    delegate_->RequestToLockMouse(this, user_gesture, last_unlocked_by_target);
  } else {
    GotResponseToLockMouseRequest(false);
  }
}

void WebContentsImpl::LostMouseLock() {
  if (delegate_)
    delegate_->LostMouseLock();
}

void WebContentsImpl::CreateNewWindow(
    int route_id,
    const ViewHostMsg_CreateWindow_Params& params,
    SessionStorageNamespace* session_storage_namespace) {
  if (delegate_ && !delegate_->ShouldCreateWebContents(
          this, route_id, params.window_container_type, params.frame_name,
          params.target_url)) {
    GetRenderViewHost()->GetProcess()->ResumeRequestsForView(route_id);
    return;
  }

  // We usually create the new window in the same BrowsingInstance (group of
  // script-related windows), by passing in the current SiteInstance.  However,
  // if the opener is being suppressed, we create a new SiteInstance in its own
  // BrowsingInstance.
  scoped_refptr<SiteInstance> site_instance =
      params.opener_suppressed ?
      SiteInstance::Create(GetBrowserContext()) :
      GetSiteInstance();

  // Create the new web contents. This will automatically create the new
  // WebContentsView. In the future, we may want to create the view separately.
  WebContentsImpl* new_contents = new WebContentsImpl(
      GetBrowserContext(),
                          site_instance,
                          route_id,
                          this,
                          params.opener_suppressed ? NULL : this,
      static_cast<SessionStorageNamespaceImpl*>(session_storage_namespace));
  new_contents->set_opener_web_ui_type(GetWebUITypeForCurrentState());

  if (!params.opener_suppressed) {
    content::WebContentsView* new_view = new_contents->GetView();

    // TODO(brettw): It seems bogus that we have to call this function on the
    // newly created object and give it one of its own member variables.
    new_view->CreateViewForWidget(new_contents->GetRenderViewHost());

    // Save the created window associated with the route so we can show it
    // later.
    DCHECK_NE(MSG_ROUTING_NONE, route_id);
    pending_contents_[route_id] = new_contents;
  }

  if (delegate_) {
    delegate_->WebContentsCreated(
        this, params.opener_frame_id, params.target_url, new_contents);
  }

  if (params.opener_suppressed) {
    // When the opener is suppressed, the original renderer cannot access the
    // new window.  As a result, we need to show and navigate the window here.
    gfx::Rect initial_pos;
    // TODO(cdn) Fix popup white-listing for links that open in a new process.
    AddNewContents(
        new_contents, params.user_gesture ? params.disposition : NEW_POPUP,
        initial_pos, params.user_gesture);

    content::OpenURLParams open_params(params.target_url, content::Referrer(),
                                       CURRENT_TAB,
                                       content::PAGE_TRANSITION_LINK,
                                       true /* is_renderer_initiated */);
    new_contents->OpenURL(open_params);
  }
}

void WebContentsImpl::CreateNewWidget(int route_id,
                                      WebKit::WebPopupType popup_type) {
  CreateNewWidget(route_id, false, popup_type);
}

void WebContentsImpl::CreateNewFullscreenWidget(int route_id) {
  CreateNewWidget(route_id, true, WebKit::WebPopupTypeNone);
}

void WebContentsImpl::CreateNewWidget(int route_id,
                                      bool is_fullscreen,
                                      WebKit::WebPopupType popup_type) {
  content::RenderProcessHost* process = GetRenderProcessHost();
  RenderWidgetHostImpl* widget_host =
      new RenderWidgetHostImpl(this, process, route_id);
  created_widgets_.insert(widget_host);

  RenderWidgetHostViewPort* widget_view =
      RenderWidgetHostViewPort::CreateViewForWidget(widget_host);
  if (!is_fullscreen) {
    // Popups should not get activated.
    widget_view->SetPopupType(popup_type);
  }
  // Save the created widget associated with the route so we can show it later.
  pending_widget_views_[route_id] = widget_view;

#if defined(OS_MACOSX)
  // A RenderWidgetHostViewMac has lifetime scoped to the view. We'll retain it
  // to allow it to survive the trip without being hosted.
  base::mac::NSObjectRetain(widget_view->GetNativeView());
#endif
}

void WebContentsImpl::ShowCreatedWindow(int route_id,
                                        WindowOpenDisposition disposition,
                                        const gfx::Rect& initial_pos,
                                        bool user_gesture) {
  WebContentsImpl* contents = GetCreatedWindow(route_id);
  if (contents)
    AddNewContents(contents, disposition, initial_pos, user_gesture);
}

void WebContentsImpl::ShowCreatedWidget(int route_id,
                                        const gfx::Rect& initial_pos) {
  ShowCreatedWidget(route_id, false, initial_pos);
}

void WebContentsImpl::ShowCreatedFullscreenWidget(int route_id) {
  ShowCreatedWidget(route_id, true, gfx::Rect());
}

void WebContentsImpl::ShowCreatedWidget(int route_id,
                                        bool is_fullscreen,
                                        const gfx::Rect& initial_pos) {
  if (delegate_)
    delegate_->RenderWidgetShowing();

  RenderWidgetHostViewPort* widget_host_view =
      RenderWidgetHostViewPort::FromRWHV(GetCreatedWidget(route_id));
  if (!widget_host_view)
    return;
  if (is_fullscreen) {
    widget_host_view->InitAsFullscreen(GetRenderWidgetHostView());
  } else {
    widget_host_view->InitAsPopup(GetRenderWidgetHostView(), initial_pos);
  }

  RenderWidgetHostImpl* render_widget_host_impl =
      RenderWidgetHostImpl::From(widget_host_view->GetRenderWidgetHost());
  render_widget_host_impl->Init();
  // Only allow privileged mouse lock for fullscreen render widget, which is
  // used to implement Pepper Flash fullscreen.
  render_widget_host_impl->set_allow_privileged_mouse_lock(is_fullscreen);

#if defined(OS_MACOSX)
  // A RenderWidgetHostViewMac has lifetime scoped to the view. Now that it's
  // properly embedded (or purposefully ignored) we can release the retain we
  // took in CreateNewWidget().
  base::mac::NSObjectRelease(widget_host_view->GetNativeView());
#endif
}

WebContentsImpl* WebContentsImpl::GetCreatedWindow(int route_id) {
  PendingContents::iterator iter = pending_contents_.find(route_id);

  // Certain systems can block the creation of new windows. If we didn't succeed
  // in creating one, just return NULL.
  if (iter == pending_contents_.end()) {
    return NULL;
  }

  WebContentsImpl* new_contents = iter->second;
  pending_contents_.erase(route_id);

  if (!new_contents->GetRenderProcessHost()->HasConnection() ||
      !new_contents->GetRenderViewHost()->GetView())
    return NULL;

  // TODO(brettw): It seems bogus to reach into here and initialize the host.
  static_cast<RenderViewHostImpl*>(new_contents->GetRenderViewHost())->Init();
  return new_contents;
}

RenderWidgetHostView* WebContentsImpl::GetCreatedWidget(int route_id) {
  PendingWidgetViews::iterator iter = pending_widget_views_.find(route_id);
  if (iter == pending_widget_views_.end()) {
    DCHECK(false);
    return NULL;
  }

  RenderWidgetHostView* widget_host_view = iter->second;
  pending_widget_views_.erase(route_id);

  RenderWidgetHost* widget_host = widget_host_view->GetRenderWidgetHost();
  if (!widget_host->GetProcess()->HasConnection()) {
    // The view has gone away or the renderer crashed. Nothing to do.
    return NULL;
  }

  return widget_host_view;
}

void WebContentsImpl::ShowContextMenu(
    const content::ContextMenuParams& params) {
  // Allow WebContentsDelegates to handle the context menu operation first.
  if (delegate_ && delegate_->HandleContextMenu(params))
    return;

  render_view_host_delegate_view_->ShowContextMenu(params);
}

void WebContentsImpl::RequestMediaAccessPermission(
    const content::MediaStreamRequest* request,
    const content::MediaResponseCallback& callback) {
  if (delegate_)
    delegate_->RequestMediaAccessPermission(this, request, callback);
  else
    callback.Run(content::MediaStreamDevices());
}

void WebContentsImpl::UpdatePreferredSize(const gfx::Size& pref_size) {
  preferred_size_ = pref_size;
  if (delegate_)
    delegate_->UpdatePreferredSize(this, pref_size);
}

void WebContentsImpl::ResizeDueToAutoResize(const gfx::Size& new_size) {
  if (delegate_)
    delegate_->ResizeDueToAutoResize(this, new_size);
}

WebContents* WebContentsImpl::OpenURL(const OpenURLParams& params) {
  if (!delegate_)
    return NULL;

  WebContents* new_contents = delegate_->OpenURLFromTab(this, params);
  // Notify observers.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidOpenURL(params.url, params.referrer,
                               params.disposition, params.transition));
  return new_contents;
}

bool WebContentsImpl::NavigateToPendingEntry(
    NavigationController::ReloadType reload_type) {
  return NavigateToEntry(
      *NavigationEntryImpl::FromNavigationEntry(controller_.GetPendingEntry()),
      reload_type);
}

void WebContentsImpl::RenderViewForInterstitialPageCreated(
    content::RenderViewHost* render_view_host) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    RenderViewForInterstitialPageCreated(render_view_host));
}

bool WebContentsImpl::NavigateToEntry(
    const NavigationEntryImpl& entry,
    NavigationController::ReloadType reload_type) {
  // The renderer will reject IPC messages with URLs longer than
  // this limit, so don't attempt to navigate with a longer URL.
  if (entry.GetURL().spec().size() > content::kMaxURLChars)
    return false;

  RenderViewHostImpl* dest_render_view_host =
      static_cast<RenderViewHostImpl*>(render_manager_.Navigate(entry));
  if (!dest_render_view_host)
    return false;  // Unable to create the desired render view host.

  // For security, we should never send non-Web-UI URLs to a Web UI renderer.
  // Double check that here.
  int enabled_bindings = dest_render_view_host->GetEnabledBindings();
  WebUIControllerFactory* factory =
      content::GetContentClient()->browser()->GetWebUIControllerFactory();
  bool data_urls_allowed = delegate_ && delegate_->CanLoadDataURLsInWebUI();
  bool is_allowed_in_web_ui_renderer =
      factory &&
      factory->IsURLAcceptableForWebUI(GetBrowserContext(), entry.GetURL(),
                                       data_urls_allowed);
  if ((enabled_bindings & content::BINDINGS_POLICY_WEB_UI) &&
      !is_allowed_in_web_ui_renderer) {
    // Log the URL to help us diagnose any future failures of this CHECK.
    content::GetContentClient()->SetActiveURL(entry.GetURL());
    CHECK(0);
  }

  // Tell DevTools agent that it is attached prior to the navigation.
  DevToolsManagerImpl::GetInstance()->OnNavigatingToPendingEntry(
      GetRenderViewHost(),
      dest_render_view_host,
      entry.GetURL());

  // Notify observers that we will navigate in this RV.
  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    AboutToNavigateRenderView(dest_render_view_host));

  // Used for page load time metrics.
  current_load_start_ = base::TimeTicks::Now();

  // Navigate in the desired RenderViewHost.
  std::string embedder_channel_name;
  int embedder_container_id;
  GetBrowserPluginEmbedderInfo(dest_render_view_host,
                               &embedder_channel_name,
                               &embedder_container_id);
  ViewMsg_Navigate_Params navigate_params;
  MakeNavigateParams(entry, controller_, delegate_, reload_type,
                     embedder_channel_name,
                     embedder_container_id,
                     &navigate_params);
  dest_render_view_host->Navigate(navigate_params);

  if (entry.GetPageID() == -1) {
    // HACK!!  This code suppresses javascript: URLs from being added to
    // session history, which is what we want to do for javascript: URLs that
    // do not generate content.  What we really need is a message from the
    // renderer telling us that a new page was not created.  The same message
    // could be used for mailto: URLs and the like.
    if (entry.GetURL().SchemeIs(chrome::kJavaScriptScheme))
      return false;
  }

  // Notify observers about navigation.
  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    NavigateToPendingEntry(entry.GetURL(), reload_type));

  if (delegate_)
    delegate_->DidNavigateToPendingEntry(this);

  return true;
}

void WebContentsImpl::SetHistoryLengthAndPrune(
    const SiteInstance* site_instance,
    int history_length,
    int32 minimum_page_id) {
  // SetHistoryLengthAndPrune doesn't work when there are pending cross-site
  // navigations. Callers should ensure that this is the case.
  if (render_manager_.pending_render_view_host()) {
    NOTREACHED();
    return;
  }
  RenderViewHostImpl* rvh = GetRenderViewHostImpl();
  if (!rvh) {
    NOTREACHED();
    return;
  }
  if (site_instance && rvh->GetSiteInstance() != site_instance) {
    NOTREACHED();
    return;
  }
  rvh->Send(new ViewMsg_SetHistoryLengthAndPrune(rvh->GetRoutingID(),
                                                 history_length,
                                                 minimum_page_id));
}

void WebContentsImpl::FocusThroughTabTraversal(bool reverse) {
  if (ShowingInterstitialPage()) {
    render_manager_.interstitial_page()->FocusThroughTabTraversal(reverse);
    return;
  }
  GetRenderViewHostImpl()->SetInitialFocus(reverse);
}

bool WebContentsImpl::ShowingInterstitialPage() const {
  return render_manager_.interstitial_page() != NULL;
}

InterstitialPage* WebContentsImpl::GetInterstitialPage() const {
  return render_manager_.interstitial_page();
}

bool WebContentsImpl::IsSavable() {
  // WebKit creates Document object when MIME type is application/xhtml+xml,
  // so we also support this MIME type.
  return contents_mime_type_ == "text/html" ||
         contents_mime_type_ == "text/xml" ||
         contents_mime_type_ == "application/xhtml+xml" ||
         contents_mime_type_ == "text/plain" ||
         contents_mime_type_ == "text/css" ||
         net::IsSupportedJavascriptMimeType(contents_mime_type_.c_str());
}

void WebContentsImpl::OnSavePage() {
  // If we can not save the page, try to download it.
  if (!IsSavable()) {
    download_stats::RecordDownloadSource(
        download_stats::INITIATED_BY_SAVE_PACKAGE_ON_NON_HTML);
    SaveURL(GetURL(), content::Referrer(), true);
    return;
  }

  Stop();

  // Create the save package and possibly prompt the user for the name to save
  // the page as. The user prompt is an asynchronous operation that runs on
  // another thread.
  save_package_ = new SavePackage(this);
  save_package_->GetSaveInfo();
}

// Used in automated testing to bypass prompting the user for file names.
// Instead, the names and paths are hard coded rather than running them through
// file name sanitation and extension / mime checking.
bool WebContentsImpl::SavePage(const FilePath& main_file,
                               const FilePath& dir_path,
                               content::SavePageType save_type) {
  // Stop the page from navigating.
  Stop();

  save_package_ = new SavePackage(this, save_type, main_file, dir_path);
  return save_package_->Init(content::SavePackageDownloadCreatedCallback());
}

void WebContentsImpl::GenerateMHTML(
    const FilePath& file,
    const base::Callback<void(const FilePath&, int64)>& callback) {
  MHTMLGenerationManager::GetInstance()->GenerateMHTML(this, file, callback);
}

bool WebContentsImpl::IsActiveEntry(int32 page_id) {
  NavigationEntryImpl* active_entry =
      NavigationEntryImpl::FromNavigationEntry(controller_.GetActiveEntry());
  return (active_entry != NULL &&
          active_entry->site_instance() == GetSiteInstance() &&
          active_entry->GetPageID() == page_id);
}

const std::string& WebContentsImpl::GetContentsMimeType() const {
  return contents_mime_type_;
}

bool WebContentsImpl::WillNotifyDisconnection() const {
  return notify_disconnection_;
}

void WebContentsImpl::SetOverrideEncoding(const std::string& encoding) {
  SetEncoding(encoding);
  GetRenderViewHostImpl()->Send(new ViewMsg_SetPageEncoding(
      GetRenderViewHost()->GetRoutingID(), encoding));
}

void WebContentsImpl::ResetOverrideEncoding() {
  encoding_.clear();
  GetRenderViewHostImpl()->Send(new ViewMsg_ResetPageEncodingToDefault(
      GetRenderViewHost()->GetRoutingID()));
}

content::RendererPreferences* WebContentsImpl::GetMutableRendererPrefs() {
  return &renderer_preferences_;
}

void WebContentsImpl::SetNewTabStartTime(const base::TimeTicks& time) {
  new_tab_start_time_ = time;
}

base::TimeTicks WebContentsImpl::GetNewTabStartTime() const {
  return new_tab_start_time_;
}

void WebContentsImpl::Close() {
  Close(GetRenderViewHost());
}

void WebContentsImpl::OnCloseStarted() {
  if (close_start_time_.is_null())
    close_start_time_ = base::TimeTicks::Now();
}

bool WebContentsImpl::ShouldAcceptDragAndDrop() const {
#if defined(OS_CHROMEOS)
  // ChromeOS panels (pop-ups) do not take drag-n-drop.
  // See http://crosbug.com/2413
  if (delegate_ && delegate_->IsPopupOrPanel(this))
    return false;
  return true;
#else
  return true;
#endif
}

void WebContentsImpl::SystemDragEnded() {
  if (GetRenderViewHost())
    GetRenderViewHostImpl()->DragSourceSystemDragEnded();
  if (delegate_)
    delegate_->DragEnded();
}

void WebContentsImpl::UserGestureDone() {
  OnUserGesture();
}

void WebContentsImpl::SetClosedByUserGesture(bool value) {
  closed_by_user_gesture_ = value;
}

bool WebContentsImpl::GetClosedByUserGesture() const {
  return closed_by_user_gesture_;
}

double WebContentsImpl::GetZoomLevel() const {
  HostZoomMapImpl* zoom_map = static_cast<HostZoomMapImpl*>(
      HostZoomMap::GetForBrowserContext(GetBrowserContext()));
  if (!zoom_map)
    return 0;

  double zoom_level;
  if (temporary_zoom_settings_) {
    zoom_level = zoom_map->GetTemporaryZoomLevel(
        GetRenderProcessHost()->GetID(), GetRenderViewHost()->GetRoutingID());
  } else {
    GURL url;
    NavigationEntry* active_entry = GetController().GetActiveEntry();
    // Since zoom map is updated using rewritten URL, use rewritten URL
    // to get the zoom level.
    url = active_entry ? active_entry->GetURL() : GURL::EmptyGURL();
    zoom_level = zoom_map->GetZoomLevel(net::GetHostOrSpecFromURL(url));
  }
  return zoom_level;
}

int WebContentsImpl::GetZoomPercent(bool* enable_increment,
                                    bool* enable_decrement) {
  *enable_decrement = *enable_increment = false;
  // Calculate the zoom percent from the factor. Round up to the nearest whole
  // number.
  int percent = static_cast<int>(
      WebKit::WebView::zoomLevelToZoomFactor(GetZoomLevel()) * 100 + 0.5);
  *enable_decrement = percent > minimum_zoom_percent_;
  *enable_increment = percent < maximum_zoom_percent_;
  return percent;
}

void WebContentsImpl::ViewSource() {
  if (!delegate_)
    return;

  NavigationEntry* active_entry = GetController().GetActiveEntry();
  if (!active_entry)
    return;

  delegate_->ViewSourceForTab(this, active_entry->GetURL());
}

void WebContentsImpl::ViewFrameSource(const GURL& url,
                                      const std::string& content_state) {
  if (!delegate_)
    return;

  delegate_->ViewSourceForFrame(this, url, content_state);
}

int WebContentsImpl::GetMinimumZoomPercent() const {
  return minimum_zoom_percent_;
}

int WebContentsImpl::GetMaximumZoomPercent() const {
  return maximum_zoom_percent_;
}

gfx::Size WebContentsImpl::GetPreferredSize() const {
  return preferred_size_;
}

int WebContentsImpl::GetContentRestrictions() const {
  return content_restrictions_;
}

WebUI::TypeID WebContentsImpl::GetWebUITypeForCurrentState() {
  WebUIControllerFactory* factory =
      content::GetContentClient()->browser()->GetWebUIControllerFactory();
  if (!factory)
    return WebUI::kNoWebUI;
  return factory->GetWebUIType(GetBrowserContext(), GetURL());
}

content::WebUI* WebContentsImpl::GetWebUIForCurrentState() {
  // When there is a pending navigation entry, we want to use the pending WebUI
  // that goes along with it to control the basic flags. For example, we want to
  // show the pending URL in the URL bar, so we want the display_url flag to
  // be from the pending entry.
  //
  // The confusion comes because there are multiple possibilities for the
  // initial load in a tab as a side effect of the way the RenderViewHostManager
  // works.
  //
  //  - For the very first tab the load looks "normal". The new tab Web UI is
  //    the pending one, and we want it to apply here.
  //
  //  - For subsequent new tabs, they'll get a new SiteInstance which will then
  //    get switched to the one previously associated with the new tab pages.
  //    This switching will cause the manager to commit the RVH/WebUI. So we'll
  //    have a committed Web UI in this case.
  //
  // This condition handles all of these cases:
  //
  //  - First load in first tab: no committed nav entry + pending nav entry +
  //    pending dom ui:
  //    -> Use pending Web UI if any.
  //
  //  - First load in second tab: no committed nav entry + pending nav entry +
  //    no pending Web UI:
  //    -> Use the committed Web UI if any.
  //
  //  - Second navigation in any tab: committed nav entry + pending nav entry:
  //    -> Use pending Web UI if any.
  //
  //  - Normal state with no load: committed nav entry + no pending nav entry:
  //    -> Use committed Web UI.
  if (controller_.GetPendingEntry() &&
      (controller_.GetLastCommittedEntry() ||
       render_manager_.pending_web_ui()))
    return render_manager_.pending_web_ui();
  return render_manager_.web_ui();
}

bool WebContentsImpl::GotResponseToLockMouseRequest(bool allowed) {
  return GetRenderViewHost() ?
      GetRenderViewHostImpl()->GotResponseToLockMouseRequest(allowed) : false;
}

bool WebContentsImpl::HasOpener() const {
  return opener_ != NULL;
}

void WebContentsImpl::DidChooseColorInColorChooser(int color_chooser_id,
                                                   SkColor color) {
  GetRenderViewHost()->Send(new ViewMsg_DidChooseColorResponse(
      GetRenderViewHost()->GetRoutingID(), color_chooser_id, color));
}

void WebContentsImpl::DidEndColorChooser(int color_chooser_id) {
  GetRenderViewHost()->Send(new ViewMsg_DidEndColorChooser(
      GetRenderViewHost()->GetRoutingID(), color_chooser_id));
  if (delegate_)
    delegate_->DidEndColorChooser();
  color_chooser_ = NULL;
}

bool WebContentsImpl::FocusLocationBarByDefault() {
  content::WebUI* web_ui = GetWebUIForCurrentState();
  if (web_ui)
    return web_ui->ShouldFocusLocationBarByDefault();
  NavigationEntry* entry = controller_.GetActiveEntry();
  return (entry && entry->GetURL() == GURL(chrome::kAboutBlankURL));
}

void WebContentsImpl::SetFocusToLocationBar(bool select_all) {
  if (delegate_)
    delegate_->SetFocusToLocationBar(select_all);
}

void WebContentsImpl::OnRegisterIntentService(
    const webkit_glue::WebIntentServiceData& data,
    bool user_gesture) {
  if (delegate_)
    delegate_->RegisterIntentHandler(this, data, user_gesture);
}

void WebContentsImpl::OnWebIntentDispatch(
    const webkit_glue::WebIntentData& intent,
    int intent_id) {
  if (!delegate_)
    return;

  WebIntentsDispatcherImpl* intents_dispatcher =
      new WebIntentsDispatcherImpl(this, intent, intent_id);
  delegate_->WebIntentDispatch(this, intents_dispatcher);
}

void WebContentsImpl::DidStartProvisionalLoadForFrame(
    content::RenderViewHost* render_view_host,
    int64 frame_id,
    bool is_main_frame,
    const GURL& opener_url,
    const GURL& url) {
  bool is_error_page = (url.spec() == content::kUnreachableWebDataURL);
  GURL validated_url(url);
  GURL validated_opener_url(opener_url);
  content::RenderProcessHost* render_process_host =
      render_view_host->GetProcess();
  RenderViewHost::FilterURL(
      render_process_host->GetID(),
      false,
      &validated_url);
  RenderViewHost::FilterURL(
      render_process_host->GetID(),
      true,
      &validated_opener_url);

  // Notify observers about the start of the provisional load.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidStartProvisionalLoadForFrame(frame_id, is_main_frame,
                    validated_url, is_error_page, render_view_host));

  if (is_main_frame) {
    // Notify observers about the provisional change in the main frame URL.
    FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                      ProvisionalChangeToMainFrameUrl(validated_url,
                                                      validated_opener_url,
                                                      render_view_host));
  }
}

void WebContentsImpl::DidRedirectProvisionalLoad(
    content::RenderViewHost* render_view_host,
    int32 page_id,
    const GURL& opener_url,
    const GURL& source_url,
    const GURL& target_url) {
  // TODO(creis): Remove this method and have the pre-rendering code listen to
  // the ResourceDispatcherHost's RESOURCE_RECEIVED_REDIRECT notification
  // instead.  See http://crbug.com/78512.
  GURL validated_source_url(source_url);
  GURL validated_target_url(target_url);
  GURL validated_opener_url(opener_url);
  content::RenderProcessHost* render_process_host =
      render_view_host->GetProcess();
  RenderViewHostImpl::FilterURL(
      ChildProcessSecurityPolicyImpl::GetInstance(),
      render_process_host->GetID(),
      false,
      &validated_source_url);
  RenderViewHostImpl::FilterURL(
      ChildProcessSecurityPolicyImpl::GetInstance(),
      render_process_host->GetID(),
      false,
      &validated_target_url);
  RenderViewHostImpl::FilterURL(
      ChildProcessSecurityPolicyImpl::GetInstance(),
      render_process_host->GetID(),
      true,
      &validated_opener_url);
  NavigationEntry* entry;
  if (page_id == -1) {
    entry = controller_.GetPendingEntry();
  } else {
    entry = controller_.GetEntryWithPageID(render_view_host->GetSiteInstance(),
                                           page_id);
  }
  if (!entry || entry->GetURL() != validated_source_url)
    return;

  // Notify observers about the provisional change in the main frame URL.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    ProvisionalChangeToMainFrameUrl(validated_target_url,
                                                    validated_opener_url,
                                                    render_view_host));
}

void WebContentsImpl::DidFailProvisionalLoadWithError(
    content::RenderViewHost* render_view_host,
    const ViewHostMsg_DidFailProvisionalLoadWithError_Params& params) {
  VLOG(1) << "Failed Provisional Load: " << params.url.possibly_invalid_spec()
          << ", error_code: " << params.error_code
          << ", error_description: " << params.error_description
          << ", is_main_frame: " << params.is_main_frame
          << ", showing_repost_interstitial: " <<
            params.showing_repost_interstitial
          << ", frame_id: " << params.frame_id;
  GURL validated_url(params.url);
  content::RenderProcessHost* render_process_host =
      render_view_host->GetProcess();
  RenderViewHost::FilterURL(
      render_process_host->GetID(),
      false,
      &validated_url);

  if (net::ERR_ABORTED == params.error_code) {
    // EVIL HACK ALERT! Ignore failed loads when we're showing interstitials.
    // This means that the interstitial won't be torn down properly, which is
    // bad. But if we have an interstitial, go back to another tab type, and
    // then load the same interstitial again, we could end up getting the first
    // interstitial's "failed" message (as a result of the cancel) when we're on
    // the second one.
    //
    // We can't tell this apart, so we think we're tearing down the current page
    // which will cause a crash later one. There is also some code in
    // RenderViewHostManager::RendererAbortedProvisionalLoad that is commented
    // out because of this problem.
    //
    // http://code.google.com/p/chromium/issues/detail?id=2855
    // Because this will not tear down the interstitial properly, if "back" is
    // back to another tab type, the interstitial will still be somewhat alive
    // in the previous tab type. If you navigate somewhere that activates the
    // tab with the interstitial again, you'll see a flash before the new load
    // commits of the interstitial page.
    if (ShowingInterstitialPage()) {
      LOG(WARNING) << "Discarding message during interstitial.";
      return;
    }

    // Do not clear the pending entry if one exists, so that the user's typed
    // URL is not lost when a navigation fails or is aborted.  We'll allow
    // the view to clear the pending entry and typed URL if the user requests.

    render_manager_.RendererAbortedProvisionalLoad(render_view_host);
  }

  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    DidFailProvisionalLoad(params.frame_id,
                                           params.is_main_frame,
                                           validated_url,
                                           params.error_code,
                                           params.error_description,
                                           render_view_host));
}

void WebContentsImpl::OnDidLoadResourceFromMemoryCache(
    const GURL& url,
    const std::string& security_info,
    const std::string& http_method,
    const std::string& mime_type,
    ResourceType::Type resource_type) {
  base::StatsCounter cache("WebKit.CacheHit");
  cache.Increment();

  // Send out a notification that we loaded a resource from our memory cache.
  int cert_id = 0;
  net::CertStatus cert_status = 0;
  int security_bits = -1;
  int connection_status = 0;
  content::DeserializeSecurityInfo(security_info, &cert_id, &cert_status,
                                   &security_bits, &connection_status);
  content::LoadFromMemoryCacheDetails details(
      url, GetRenderProcessHost()->GetID(), cert_id, cert_status, http_method,
      mime_type, resource_type);

  content::NotificationService::current()->Notify(
      content::NOTIFICATION_LOAD_FROM_MEMORY_CACHE,
      content::Source<NavigationController>(&controller_),
      content::Details<content::LoadFromMemoryCacheDetails>(&details));
}

void WebContentsImpl::OnDidDisplayInsecureContent() {
  content::RecordAction(UserMetricsAction("SSL.DisplayedInsecureContent"));
  displayed_insecure_content_ = true;
  SSLManager::NotifySSLInternalStateChanged(&GetControllerImpl());
}

void WebContentsImpl::OnDidRunInsecureContent(
    const std::string& security_origin, const GURL& target_url) {
  LOG(INFO) << security_origin << " ran insecure content from "
            << target_url.possibly_invalid_spec();
  content::RecordAction(UserMetricsAction("SSL.RanInsecureContent"));
  if (EndsWith(security_origin, kDotGoogleDotCom, false))
    content::RecordAction(UserMetricsAction("SSL.RanInsecureContentGoogle"));
  controller_.ssl_manager()->DidRunInsecureContent(security_origin);
  displayed_insecure_content_ = true;
  SSLManager::NotifySSLInternalStateChanged(&GetControllerImpl());
}

void WebContentsImpl::OnDocumentLoadedInFrame(int64 frame_id) {
  controller_.DocumentLoadedInFrame();
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DocumentLoadedInFrame(frame_id, message_source_));
}

void WebContentsImpl::OnDidFinishLoad(
    int64 frame_id,
    const GURL& validated_url,
    bool is_main_frame) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidFinishLoad(frame_id, validated_url, is_main_frame,
                                  message_source_));
}

void WebContentsImpl::OnDidFailLoadWithError(
    int64 frame_id,
    const GURL& validated_url,
    bool is_main_frame,
    int error_code,
    const string16& error_description) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidFailLoad(frame_id, validated_url, is_main_frame,
                                error_code, error_description,
                                message_source_));
}

void WebContentsImpl::OnUpdateContentRestrictions(int restrictions) {
  content_restrictions_ = restrictions;
  if (delegate_)
    delegate_->ContentRestrictionsChanged(this);
}

void WebContentsImpl::OnGoToEntryAtOffset(int offset) {
  if (!delegate_ || delegate_->OnGoToEntryOffset(offset)) {
    NavigationEntryImpl* entry = NavigationEntryImpl::FromNavigationEntry(
        controller_.GetEntryAtOffset(offset));
    if (!entry)
      return;
    // Note that we don't call NavigationController::GotToOffset() as we don't
    // want to create a pending navigation entry (it might end up lingering
    // http://crbug.com/51680).
    entry->SetTransitionType(
        content::PageTransitionFromInt(
            entry->GetTransitionType() |
            content::PAGE_TRANSITION_FORWARD_BACK));
    NavigateToEntry(*entry, NavigationControllerImpl::NO_RELOAD);

    // If the entry is being restored and doesn't have a SiteInstance yet, fill
    // it in now that we know. This allows us to find the entry when it commits.
    if (!entry->site_instance() &&
        entry->restore_type() != NavigationEntryImpl::RESTORE_NONE) {
      entry->set_site_instance(
          static_cast<SiteInstanceImpl*>(GetPendingSiteInstance()));
    }
  }
}

void WebContentsImpl::OnUpdateZoomLimits(int minimum_percent,
                                         int maximum_percent,
                                         bool remember) {
  minimum_zoom_percent_ = minimum_percent;
  maximum_zoom_percent_ = maximum_percent;
  temporary_zoom_settings_ = !remember;
}

void WebContentsImpl::OnSaveURL(const GURL& url,
                                const content::Referrer& referrer) {
  download_stats::RecordDownloadSource(
      download_stats::INITIATED_BY_PEPPER_SAVE);
  // Check if the URL to save matches the URL of the main frame. Since this
  // message originates from Pepper plugins, it may not be the case if the
  // plugin is an embedded element.
  GURL main_frame_url = GetURL();
  if (!main_frame_url.is_valid())
    return;
  bool is_main_frame = (url == main_frame_url);
  SaveURL(url, referrer, is_main_frame);
}

void WebContentsImpl::OnEnumerateDirectory(int request_id,
                                           const FilePath& path) {
  if (!delegate_)
    return;

  ChildProcessSecurityPolicyImpl* policy =
      ChildProcessSecurityPolicyImpl::GetInstance();
  if (policy->CanReadDirectory(GetRenderProcessHost()->GetID(), path))
    delegate_->EnumerateDirectory(this, request_id, path);
}

void WebContentsImpl::OnJSOutOfMemory() {
  if (delegate_)
    delegate_->JSOutOfMemory(this);
}

void WebContentsImpl::OnRegisterProtocolHandler(const std::string& protocol,
                                                const GURL& url,
                                                const string16& title,
                                                bool user_gesture) {
  if (!delegate_)
    return;

  ChildProcessSecurityPolicyImpl* policy =
      ChildProcessSecurityPolicyImpl::GetInstance();
  if (policy->IsPseudoScheme(protocol) || policy->IsDisabledScheme(protocol))
    return;
  delegate_->RegisterProtocolHandler(this, protocol, url, title, user_gesture);
}

void WebContentsImpl::OnFindReply(int request_id,
                                  int number_of_matches,
                                  const gfx::Rect& selection_rect,
                                  int active_match_ordinal,
                                  bool final_update) {
  if (delegate_) {
    delegate_->FindReply(this, request_id, number_of_matches, selection_rect,
                         active_match_ordinal, final_update);
  }

  // Send a notification to the renderer that we are ready to receive more
  // results from the scoping effort of the Find operation. The FindInPage
  // scoping is asynchronous and periodically sends results back up to the
  // browser using IPC. In an effort to not spam the browser we have the
  // browser send an ACK for each FindReply message and have the renderer
  // queue up the latest status message while waiting for this ACK.
  GetRenderViewHostImpl()->Send(
      new ViewMsg_FindReplyACK(GetRenderViewHost()->GetRoutingID()));
}

void WebContentsImpl::OnCrashedPlugin(const FilePath& plugin_path) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    PluginCrashed(plugin_path));
}

void WebContentsImpl::OnAppCacheAccessed(const GURL& manifest_url,
                                         bool blocked_by_policy) {
  // Notify observers about navigation.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    AppCacheAccessed(manifest_url, blocked_by_policy));
}

void WebContentsImpl::OnOpenColorChooser(int color_chooser_id,
                                         SkColor color) {
  color_chooser_ = delegate_ ?
      delegate_->OpenColorChooser(this, color_chooser_id, color) : NULL;
}

void WebContentsImpl::OnEndColorChooser(int color_chooser_id) {
  if (color_chooser_ &&
      color_chooser_id == color_chooser_->identifier())
    color_chooser_->End();
}

void WebContentsImpl::OnSetSelectedColorInColorChooser(int color_chooser_id,
                                                       SkColor color) {
  if (color_chooser_ &&
      color_chooser_id == color_chooser_->identifier())
    color_chooser_->SetSelectedColor(color);
}

void WebContentsImpl::OnPepperPluginHung(int plugin_child_id,
                                         const FilePath& path,
                                         bool is_hung) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    PluginHungStatusChanged(plugin_child_id, path, is_hung));
}

// This exists for render views that don't have a WebUI, but do have WebUI
// bindings enabled.
void WebContentsImpl::OnWebUISend(const GURL& source_url,
                                  const std::string& name,
                                  const base::ListValue& args) {
  if (delegate_)
    delegate_->WebUISend(this, source_url, name, args);
}

// Notifies the RenderWidgetHost instance about the fact that the page is
// loading, or done loading and calls the base implementation.
void WebContentsImpl::SetIsLoading(bool is_loading,
                                   LoadNotificationDetails* details) {
  if (is_loading == is_loading_)
    return;

  if (!is_loading) {
    load_state_ = net::LoadStateWithParam(net::LOAD_STATE_IDLE, string16());
    load_state_host_.clear();
    upload_size_ = 0;
    upload_position_ = 0;
  }

  render_manager_.SetIsLoading(is_loading);

  is_loading_ = is_loading;
  waiting_for_response_ = is_loading;

  if (delegate_)
    delegate_->LoadingStateChanged(this);
  NotifyNavigationStateChanged(content::INVALIDATE_TYPE_LOAD);

  int type = is_loading ? content::NOTIFICATION_LOAD_START :
      content::NOTIFICATION_LOAD_STOP;
  content::NotificationDetails det = content::NotificationService::NoDetails();
  if (details)
      det = content::Details<LoadNotificationDetails>(details);
  content::NotificationService::current()->Notify(type,
      content::Source<NavigationController>(&controller_),
      det);
}

void WebContentsImpl::DidNavigateMainFramePostCommit(
    const content::LoadCommittedDetails& details,
    const ViewHostMsg_FrameNavigate_Params& params) {
  if (opener_web_ui_type_ != WebUI::kNoWebUI) {
    // If this is a window.open navigation, use the same WebUI as the renderer
    // that opened the window, as long as both renderers have the same
    // privileges.
    if (delegate_ && opener_web_ui_type_ == GetWebUITypeForCurrentState()) {
      WebUIImpl* web_ui = static_cast<WebUIImpl*>(CreateWebUI(GetURL()));
      // web_ui might be NULL if the URL refers to a non-existent extension.
      if (web_ui) {
        render_manager_.SetWebUIPostCommit(web_ui);
        web_ui->RenderViewCreated(GetRenderViewHost());
      }
    }
    opener_web_ui_type_ = WebUI::kNoWebUI;
  }

  if (details.is_navigation_to_different_page()) {
    // Clear the status bubble. This is a workaround for a bug where WebKit
    // doesn't let us know that the cursor left an element during a
    // transition (this is also why the mouse cursor remains as a hand after
    // clicking on a link); see bugs 1184641 and 980803. We don't want to
    // clear the bubble when a user navigates to a named anchor in the same
    // page.
    UpdateTargetURL(details.entry->GetPageID(), GURL());
  }

  if (!details.is_in_page) {
    // Once the main frame is navigated, we're no longer considered to have
    // displayed insecure content.
    displayed_insecure_content_ = false;
  }

  // Notify observers about navigation.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidNavigateMainFrame(details, params));
}

void WebContentsImpl::DidNavigateAnyFramePostCommit(
    RenderViewHost* render_view_host,
    const content::LoadCommittedDetails& details,
    const ViewHostMsg_FrameNavigate_Params& params) {
  // If we navigate off the page, reset JavaScript state. This does nothing
  // to prevent a malicious script from spamming messages, since the script
  // could just reload the page to stop blocking.
  if (dialog_creator_ && !details.is_in_page) {
    dialog_creator_->ResetJavaScriptState(this);
    dialog_creator_ = NULL;
  }

  // Notify observers about navigation.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidNavigateAnyFrame(details, params));
}

void WebContentsImpl::UpdateMaxPageIDIfNecessary(RenderViewHost* rvh) {
  // If we are creating a RVH for a restored controller, then we need to make
  // sure the RenderView starts with a next_page_id_ larger than the number
  // of restored entries.  This must be called before the RenderView starts
  // navigating (to avoid a race between the browser updating max_page_id and
  // the renderer updating next_page_id_).  Because of this, we only call this
  // from CreateRenderView and allow that to notify the RenderView for us.
  int max_restored_page_id = controller_.GetMaxRestoredPageID();
  if (max_restored_page_id >
      GetMaxPageIDForSiteInstance(rvh->GetSiteInstance()))
    UpdateMaxPageIDForSiteInstance(rvh->GetSiteInstance(),
                                   max_restored_page_id);
}

bool WebContentsImpl::UpdateTitleForEntry(NavigationEntryImpl* entry,
                                          const string16& title) {
  // For file URLs without a title, use the pathname instead. In the case of a
  // synthesized title, we don't want the update to count toward the "one set
  // per page of the title to history."
  string16 final_title;
  bool explicit_set;
  if (entry && entry->GetURL().SchemeIsFile() && title.empty()) {
    final_title = UTF8ToUTF16(entry->GetURL().ExtractFileName());
    explicit_set = false;  // Don't count synthetic titles toward the set limit.
  } else {
    TrimWhitespace(title, TRIM_ALL, &final_title);
    explicit_set = true;
  }

  // If a page is created via window.open and never navigated,
  // there will be no navigation entry. In this situation,
  // |page_title_when_no_navigation_entry_| will be used for page title.
  if (entry) {
    if (final_title == entry->GetTitle())
      return false;  // Nothing changed, don't bother.

    entry->SetTitle(final_title);
  } else {
    if (page_title_when_no_navigation_entry_ == final_title)
      return false;  // Nothing changed, don't bother.

    page_title_when_no_navigation_entry_ = final_title;
  }

  // Lastly, set the title for the view.
  view_->SetPageTitle(final_title);

  std::pair<content::NavigationEntry*, bool> details =
      std::make_pair(entry, explicit_set);

  content::NotificationService::current()->Notify(
      content::NOTIFICATION_WEB_CONTENTS_TITLE_UPDATED,
      content::Source<WebContents>(this),
      content::Details<std::pair<content::NavigationEntry*, bool> >(&details));

  return true;
}

void WebContentsImpl::NotifySwapped() {
  // After sending out a swap notification, we need to send a disconnect
  // notification so that clients that pick up a pointer to |this| can NULL the
  // pointer.  See Bug 1230284.
  notify_disconnection_ = true;
  content::NotificationService::current()->Notify(
      content::NOTIFICATION_WEB_CONTENTS_SWAPPED,
      content::Source<WebContents>(this),
      content::NotificationService::NoDetails());
}

void WebContentsImpl::NotifyConnected() {
  notify_disconnection_ = true;
  content::NotificationService::current()->Notify(
      content::NOTIFICATION_WEB_CONTENTS_CONNECTED,
      content::Source<WebContents>(this),
      content::NotificationService::NoDetails());
}

void WebContentsImpl::NotifyDisconnected() {
  if (!notify_disconnection_)
    return;

  notify_disconnection_ = false;
  content::NotificationService::current()->Notify(
      content::NOTIFICATION_WEB_CONTENTS_DISCONNECTED,
      content::Source<WebContents>(this),
      content::NotificationService::NoDetails());
}

RenderViewHostDelegateView* WebContentsImpl::GetDelegateView() {
  return render_view_host_delegate_view_;
}

RenderViewHostDelegate::RendererManagement*
WebContentsImpl::GetRendererManagementDelegate() {
  return &render_manager_;
}

content::RendererPreferences WebContentsImpl::GetRendererPrefs(
    content::BrowserContext* browser_context) const {
  return renderer_preferences_;
}

WebContents* WebContentsImpl::GetAsWebContents() {
  return this;
}

gfx::Rect WebContentsImpl::GetRootWindowResizerRect() const {
  if (delegate_)
    return delegate_->GetRootWindowResizerRect();
  return gfx::Rect();
}

void WebContentsImpl::RenderViewCreated(RenderViewHost* render_view_host) {
  // Don't send notifications if we are just creating a swapped-out RVH for
  // the opener chain.  These won't be used for view-source or WebUI, so it's
  // ok to return early.
  if (static_cast<RenderViewHostImpl*>(render_view_host)->is_swapped_out())
    return;

  content::NotificationService::current()->Notify(
      content::NOTIFICATION_WEB_CONTENTS_RENDER_VIEW_HOST_CREATED,
      content::Source<WebContents>(this),
      content::Details<RenderViewHost>(render_view_host));
  NavigationEntry* entry = controller_.GetActiveEntry();
  if (!entry)
    return;

  // When we're creating views, we're still doing initial setup, so we always
  // use the pending Web UI rather than any possibly existing committed one.
  if (render_manager_.pending_web_ui())
    render_manager_.pending_web_ui()->RenderViewCreated(render_view_host);

  if (entry->IsViewSourceMode()) {
    // Put the renderer in view source mode.
    static_cast<RenderViewHostImpl*>(render_view_host)->Send(
        new ViewMsg_EnableViewSourceMode(render_view_host->GetRoutingID()));
  }

  GetView()->RenderViewCreated(render_view_host);

  FOR_EACH_OBSERVER(
      WebContentsObserver, observers_, RenderViewCreated(render_view_host));
}

void WebContentsImpl::RenderViewReady(RenderViewHost* rvh) {
  if (rvh != GetRenderViewHost()) {
    // Don't notify the world, since this came from a renderer in the
    // background.
    return;
  }

  NotifyConnected();
  bool was_crashed = IsCrashed();
  SetIsCrashed(base::TERMINATION_STATUS_STILL_RUNNING, 0);

  // Restore the focus to the tab (otherwise the focus will be on the top
  // window).
  if (was_crashed && !FocusLocationBarByDefault() &&
      (!delegate_ || delegate_->ShouldFocusPageAfterCrash())) {
    Focus();
  }

  FOR_EACH_OBSERVER(WebContentsObserver, observers_, RenderViewReady());
}

void WebContentsImpl::RenderViewGone(RenderViewHost* rvh,
                                     base::TerminationStatus status,
                                     int error_code) {
  if (rvh != GetRenderViewHost()) {
    // The pending page's RenderViewHost is gone.
    return;
  }

  SetIsLoading(false, NULL);
  NotifyDisconnected();
  SetIsCrashed(status, error_code);
  GetView()->OnTabCrashed(GetCrashedStatus(), crashed_error_code_);

  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    RenderViewGone(GetCrashedStatus()));
}

void WebContentsImpl::RenderViewDeleted(RenderViewHost* rvh) {
  render_manager_.RenderViewDeleted(rvh);
  FOR_EACH_OBSERVER(WebContentsObserver, observers_, RenderViewDeleted(rvh));
}

void WebContentsImpl::DidNavigate(
    RenderViewHost* rvh,
    const ViewHostMsg_FrameNavigate_Params& params) {
  if (content::PageTransitionIsMainFrame(params.transition))
    render_manager_.DidNavigateMainFrame(rvh);

  // Update the site of the SiteInstance if it doesn't have one yet, unless
  // this is for about:blank.  In that case, the SiteInstance can still be
  // considered unused until a navigation to a real page.
  if (!static_cast<SiteInstanceImpl*>(GetSiteInstance())->HasSite() &&
      params.url != GURL(chrome::kAboutBlankURL)) {
    static_cast<SiteInstanceImpl*>(GetSiteInstance())->SetSite(params.url);
  }

  // Need to update MIME type here because it's referred to in
  // UpdateNavigationCommands() called by RendererDidNavigate() to
  // determine whether or not to enable the encoding menu.
  // It's updated only for the main frame. For a subframe,
  // RenderView::UpdateURL does not set params.contents_mime_type.
  // (see http://code.google.com/p/chromium/issues/detail?id=2929 )
  // TODO(jungshik): Add a test for the encoding menu to avoid
  // regressing it again.
  if (content::PageTransitionIsMainFrame(params.transition))
    contents_mime_type_ = params.contents_mime_type;

  content::LoadCommittedDetails details;
  bool did_navigate = controller_.RendererDidNavigate(params, &details);

  // Send notification about committed provisional loads. This notification is
  // different from the NAV_ENTRY_COMMITTED notification which doesn't include
  // the actual URL navigated to and isn't sent for AUTO_SUBFRAME navigations.
  if (details.type != content::NAVIGATION_TYPE_NAV_IGNORE) {
    // For AUTO_SUBFRAME navigations, an event for the main frame is generated
    // that is not recorded in the navigation history. For the purpose of
    // tracking navigation events, we treat this event as a sub frame navigation
    // event.
    bool is_main_frame = did_navigate ? details.is_main_frame : false;
    content::PageTransition transition_type = params.transition;
    // Whether or not a page transition was triggered by going backward or
    // forward in the history is only stored in the navigation controller's
    // entry list.
    if (did_navigate &&
        (controller_.GetActiveEntry()->GetTransitionType() &
            content::PAGE_TRANSITION_FORWARD_BACK)) {
      transition_type = content::PageTransitionFromInt(
          params.transition | content::PAGE_TRANSITION_FORWARD_BACK);
    }
    // Notify observers about the commit of the provisional load.
    FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                      DidCommitProvisionalLoadForFrame(params.frame_id,
                      is_main_frame, params.url, transition_type, rvh));
  }

  if (!did_navigate)
    return;  // No navigation happened.

  // DO NOT ADD MORE STUFF TO THIS FUNCTION! Your component should either listen
  // for the appropriate notification (best) or you can add it to
  // DidNavigateMainFramePostCommit / DidNavigateAnyFramePostCommit (only if
  // necessary, please).

  // Run post-commit tasks.
  if (details.is_main_frame) {
    DidNavigateMainFramePostCommit(details, params);
    if (delegate_)
      delegate_->DidNavigateMainFramePostCommit(this);
  }
  DidNavigateAnyFramePostCommit(rvh, details, params);
}

void WebContentsImpl::UpdateState(RenderViewHost* rvh,
                                  int32 page_id,
                                  const std::string& state) {
  // Ensure that this state update comes from either the active RVH or one of
  // the swapped out RVHs.  We don't expect to hear from any other RVHs.
  DCHECK(rvh == GetRenderViewHost() || render_manager_.IsSwappedOut(rvh));

  // We must be prepared to handle state updates for any page, these occur
  // when the user is scrolling and entering form data, as well as when we're
  // leaving a page, in which case our state may have already been moved to
  // the next page. The navigation controller will look up the appropriate
  // NavigationEntry and update it when it is notified via the delegate.

  int entry_index = controller_.GetEntryIndexWithPageID(
      rvh->GetSiteInstance(), page_id);
  if (entry_index < 0)
    return;
  NavigationEntry* entry = controller_.GetEntryAtIndex(entry_index);

  if (state == entry->GetContentState())
    return;  // Nothing to update.
  entry->SetContentState(state);
  controller_.NotifyEntryChanged(entry, entry_index);
}

void WebContentsImpl::UpdateTitle(RenderViewHost* rvh,
                                  int32 page_id,
                                  const string16& title,
                                  base::i18n::TextDirection title_direction) {
  // If we have a title, that's a pretty good indication that we've started
  // getting useful data.
  SetNotWaitingForResponse();

  // Try to find the navigation entry, which might not be the current one.
  // For example, it might be from a pending RVH for the pending entry.
  NavigationEntryImpl* entry = controller_.GetEntryWithPageID(
      rvh->GetSiteInstance(), page_id);

  // We can handle title updates when we don't have an entry in
  // UpdateTitleForEntry, but only if the update is from the current RVH.
  if (!entry && rvh != GetRenderViewHost())
    return;

  // TODO(evan): make use of title_direction.
  // http://code.google.com/p/chromium/issues/detail?id=27094
  if (!UpdateTitleForEntry(entry, title))
    return;

  // Broadcast notifications when the UI should be updated.
  if (entry == controller_.GetEntryAtOffset(0))
    NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TITLE);
}

void WebContentsImpl::UpdateEncoding(RenderViewHost* render_view_host,
                                     const std::string& encoding) {
  SetEncoding(encoding);
}

void WebContentsImpl::UpdateTargetURL(int32 page_id, const GURL& url) {
  if (delegate_)
    delegate_->UpdateTargetURL(this, page_id, url);
}

void WebContentsImpl::Close(RenderViewHost* rvh) {
  // The UI may be in an event-tracking loop, such as between the
  // mouse-down and mouse-up in text selection or a button click.
  // Defer the close until after tracking is complete, so that we
  // don't free objects out from under the UI.
  // TODO(shess): This could probably be integrated with the
  // IsDoingDrag() test below.  Punting for now because I need more
  // research to understand how this impacts platforms other than Mac.
  // TODO(shess): This could get more fine-grained.  For instance,
  // closing a tab in another window while selecting text in the
  // current window's Omnibox should be just fine.
  if (GetView()->IsEventTracking()) {
    GetView()->CloseTabAfterEventTracking();
    return;
  }

  // If we close the tab while we're in the middle of a drag, we'll crash.
  // Instead, cancel the drag and close it as soon as the drag ends.
  if (GetView()->IsDoingDrag()) {
    GetView()->CancelDragAndCloseTab();
    return;
  }

  // Ignore this if it comes from a RenderViewHost that we aren't showing.
  if (delegate_ && rvh == GetRenderViewHost())
    delegate_->CloseContents(this);
}

void WebContentsImpl::SwappedOut(RenderViewHost* rvh) {
  if (delegate_ && rvh == GetRenderViewHost())
    delegate_->SwappedOut(this);
}

void WebContentsImpl::RequestMove(const gfx::Rect& new_bounds) {
  if (delegate_ && delegate_->IsPopupOrPanel(this))
    delegate_->MoveContents(this, new_bounds);
}

void WebContentsImpl::DidStartLoading(
    content::RenderViewHost* render_view_host) {
  SetIsLoading(true, NULL);

  if (delegate_ && content_restrictions_)
    OnUpdateContentRestrictions(0);

  // Notify observers about navigation.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidStartLoading(render_view_host));
}

void WebContentsImpl::DidStopLoading(
    content::RenderViewHost* render_view_host) {
  scoped_ptr<LoadNotificationDetails> details;

  NavigationEntry* entry = controller_.GetActiveEntry();
  // An entry may not exist for a stop when loading an initial blank page or
  // if an iframe injected by script into a blank page finishes loading.
  if (entry) {
    base::TimeDelta elapsed = base::TimeTicks::Now() - current_load_start_;

    details.reset(new LoadNotificationDetails(
        entry->GetVirtualURL(),
        entry->GetTransitionType(),
        elapsed,
        &controller_,
        controller_.GetCurrentEntryIndex()));
  }

  SetIsLoading(false, details.get());

  // Notify observers about navigation.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidStopLoading(render_view_host));
}

void WebContentsImpl::DidCancelLoading() {
  controller_.DiscardNonCommittedEntries();

  // Update the URL display.
  NotifyNavigationStateChanged(content::INVALIDATE_TYPE_URL);
}

void WebContentsImpl::DidChangeLoadProgress(double progress) {
  if (delegate_)
    delegate_->LoadProgressChanged(progress);
}

void WebContentsImpl::DocumentAvailableInMainFrame(
    RenderViewHost* render_view_host) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DocumentAvailableInMainFrame());
}

void WebContentsImpl::DocumentOnLoadCompletedInMainFrame(
    RenderViewHost* render_view_host,
    int32 page_id) {
  content::NotificationService::current()->Notify(
      content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
      content::Source<WebContents>(this),
      content::Details<int>(&page_id));
}

void WebContentsImpl::RequestOpenURL(RenderViewHost* rvh,
                                     const GURL& url,
                                     const content::Referrer& referrer,
                                     WindowOpenDisposition disposition,
                                     int64 source_frame_id) {
  // If this came from a swapped out RenderViewHost, we only allow the request
  // if we are still in the same BrowsingInstance.
  if (static_cast<RenderViewHostImpl*>(rvh)->is_swapped_out() &&
      !rvh->GetSiteInstance()->IsRelatedSiteInstance(GetSiteInstance())) {
    return;
  }

  // Delegate to RequestTransferURL because this is just the generic
  // case where |old_request_id| is empty.
  RequestTransferURL(url, referrer, disposition, source_frame_id,
                     GlobalRequestID());
}

void WebContentsImpl::RequestTransferURL(
    const GURL& url,
    const content::Referrer& referrer,
    WindowOpenDisposition disposition,
    int64 source_frame_id,
    const GlobalRequestID& old_request_id) {
  WebContents* new_contents = NULL;
  content::PageTransition transition_type = content::PAGE_TRANSITION_LINK;
  if (render_manager_.web_ui()) {
    // When we're a Web UI, it will provide a page transition type for us (this
    // is so the new tab page can specify AUTO_BOOKMARK for automatically
    // generated suggestions).
    //
    // Note also that we hide the referrer for Web UI pages. We don't really
    // want web sites to see a referrer of "chrome://blah" (and some
    // chrome: URLs might have search terms or other stuff we don't want to
    // send to the site), so we send no referrer.
    OpenURLParams params(url, content::Referrer(), source_frame_id, disposition,
        render_manager_.web_ui()->GetLinkTransitionType(),
        false /* is_renderer_initiated */);
    params.transferred_global_request_id = old_request_id;
    new_contents = OpenURL(params);
    transition_type = render_manager_.web_ui()->GetLinkTransitionType();
  } else {
    OpenURLParams params(url, referrer, source_frame_id, disposition,
        content::PAGE_TRANSITION_LINK, true /* is_renderer_initiated */);
    params.transferred_global_request_id = old_request_id;
    new_contents = OpenURL(params);
  }
  if (new_contents) {
    // Notify observers.
    FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                      DidOpenRequestedURL(new_contents,
                                          url,
                                          referrer,
                                          disposition,
                                          transition_type,
                                          source_frame_id));
  }
}

void WebContentsImpl::RouteCloseEvent(RenderViewHost* rvh) {
  // Tell the active RenderViewHost to run unload handlers and close, as long
  // as the request came from a RenderViewHost in the same BrowsingInstance.
  // In most cases, we receive this from a swapped out RenderViewHost.
  // It is possible to receive it from one that has just been swapped in,
  // in which case we might as well deliver the message anyway.
  if (rvh->GetSiteInstance()->IsRelatedSiteInstance(GetSiteInstance()))
    GetRenderViewHost()->ClosePage();
}

void WebContentsImpl::RouteMessageEvent(
    RenderViewHost* rvh,
    const ViewMsg_PostMessage_Params& params) {
  // Only deliver the message to the active RenderViewHost if the request
  // came from a RenderViewHost in the same BrowsingInstance.
  if (!rvh->GetSiteInstance()->IsRelatedSiteInstance(GetSiteInstance()))
    return;

  ViewMsg_PostMessage_Params new_params(params);

  // If there is a source_routing_id, translate it to the routing ID for
  // the equivalent swapped out RVH in the target process.  If we need
  // to create a swapped out RVH for the source tab, we create its opener
  // chain as well, since those will also be accessible to the target page.
  if (new_params.source_routing_id != MSG_ROUTING_NONE) {
    // Try to look up the WebContents for the source page.
    WebContentsImpl* source_contents = NULL;
    RenderViewHostImpl* source_rvh = RenderViewHostImpl::FromID(
        rvh->GetProcess()->GetID(), params.source_routing_id);
    if (source_rvh) {
      source_contents = static_cast<WebContentsImpl*>(
          source_rvh->GetDelegate()->GetAsWebContents());
    }

    if (source_contents) {
      new_params.source_routing_id =
          source_contents->CreateOpenerRenderViews(GetSiteInstance());
    } else {
      // We couldn't find it, so don't pass a source frame.
      new_params.source_routing_id = MSG_ROUTING_NONE;
    }
  }

  // In most cases, we receive this from a swapped out RenderViewHost.
  // It is possible to receive it from one that has just been swapped in,
  // in which case we might as well deliver the message anyway.
  GetRenderViewHost()->Send(new ViewMsg_PostMessageEvent(
      GetRenderViewHost()->GetRoutingID(), new_params));
}

void WebContentsImpl::RunJavaScriptMessage(
    RenderViewHost* rvh,
    const string16& message,
    const string16& default_prompt,
    const GURL& frame_url,
    content::JavaScriptMessageType javascript_message_type,
    IPC::Message* reply_msg,
    bool* did_suppress_message) {
  // Suppress JavaScript dialogs when requested. Also suppress messages when
  // showing an interstitial as it's shown over the previous page and we don't
  // want the hidden page's dialogs to interfere with the interstitial.
  bool suppress_this_message =
      static_cast<RenderViewHostImpl*>(rvh)->is_swapped_out() ||
      ShowingInterstitialPage() ||
      !delegate_ ||
      delegate_->ShouldSuppressDialogs() ||
      !delegate_->GetJavaScriptDialogCreator();

  if (!suppress_this_message) {
    std::string accept_lang = content::GetContentClient()->browser()->
      GetAcceptLangs(GetBrowserContext());
    dialog_creator_ = delegate_->GetJavaScriptDialogCreator();
    dialog_creator_->RunJavaScriptDialog(
        this,
        frame_url.GetOrigin(),
        accept_lang,
        javascript_message_type,
        message,
        default_prompt,
        base::Bind(&WebContentsImpl::OnDialogClosed,
                   base::Unretained(this),
                   rvh,
                   reply_msg),
        &suppress_this_message);
  }

  if (suppress_this_message) {
    // If we are suppressing messages, just reply as if the user immediately
    // pressed "Cancel".
    OnDialogClosed(rvh, reply_msg, false, string16());
  }

  *did_suppress_message = suppress_this_message;
}

void WebContentsImpl::RunBeforeUnloadConfirm(RenderViewHost* rvh,
                                             const string16& message,
                                             bool is_reload,
                                             IPC::Message* reply_msg) {
  RenderViewHostImpl* rvhi = static_cast<RenderViewHostImpl*>(rvh);
  if (delegate_)
    delegate_->WillRunBeforeUnloadConfirm();

  bool suppress_this_message =
      rvhi->is_swapped_out() ||
      !delegate_ ||
      delegate_->ShouldSuppressDialogs() ||
      !delegate_->GetJavaScriptDialogCreator();
  if (suppress_this_message) {
    // The reply must be sent to the RVH that sent the request.
    rvhi->JavaScriptDialogClosed(reply_msg, true, string16());
    return;
  }

  is_showing_before_unload_dialog_ = true;
  dialog_creator_ = delegate_->GetJavaScriptDialogCreator();
  dialog_creator_->RunBeforeUnloadDialog(
      this, message, is_reload,
      base::Bind(&WebContentsImpl::OnDialogClosed, base::Unretained(this), rvh,
                 reply_msg));
}

bool WebContentsImpl::AddMessageToConsole(int32 level,
                                          const string16& message,
                                          int32 line_no,
                                          const string16& source_id) {
  if (!delegate_)
    return false;
  return delegate_->AddMessageToConsole(this, level, message, line_no,
                                        source_id);
}

WebPreferences WebContentsImpl::GetWebkitPrefs() {
  // We want to base the page config off of the real URL, rather than the
  // display URL.
  GURL url = controller_.GetActiveEntry()
      ? controller_.GetActiveEntry()->GetURL() : GURL::EmptyGURL();
  return GetWebkitPrefs(GetRenderViewHost(), url);
}

void WebContentsImpl::OnUserGesture() {
  // Notify observers.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_, DidGetUserGesture());

  ResourceDispatcherHostImpl* rdh = ResourceDispatcherHostImpl::Get();
  if (rdh)  // NULL in unittests.
    rdh->OnUserGesture(this);
}

void WebContentsImpl::OnIgnoredUIEvent() {
  // Notify observers.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_, DidGetIgnoredUIEvent());
}

void WebContentsImpl::RendererUnresponsive(RenderViewHost* rvh,
                                           bool is_during_unload) {
  // Don't show hung renderer dialog for a swapped out RVH.
  if (rvh != GetRenderViewHost())
    return;

  RenderViewHostImpl* rvhi = static_cast<RenderViewHostImpl*>(rvh);

  // Ignore renderer unresponsive event if debugger is attached to the tab
  // since the event may be a result of the renderer sitting on a breakpoint.
  // See http://crbug.com/65458
  DevToolsAgentHost* agent =
      content::DevToolsAgentHostRegistry::GetDevToolsAgentHost(rvh);
  if (agent &&
      DevToolsManagerImpl::GetInstance()->GetDevToolsClientHostFor(agent))
    return;

  if (is_during_unload) {
    // Hang occurred while firing the beforeunload/unload handler.
    // Pretend the handler fired so tab closing continues as if it had.
    rvhi->set_sudden_termination_allowed(true);

    if (!render_manager_.ShouldCloseTabOnUnresponsiveRenderer())
      return;

    // If the tab hangs in the beforeunload/unload handler there's really
    // nothing we can do to recover. Pretend the unload listeners have
    // all fired and close the tab. If the hang is in the beforeunload handler
    // then the user will not have the option of cancelling the close.
    Close(rvh);
    return;
  }

  if (!GetRenderViewHostImpl() || !GetRenderViewHostImpl()->IsRenderViewLive())
    return;

  if (delegate_)
    delegate_->RendererUnresponsive(this);
}

void WebContentsImpl::RendererResponsive(RenderViewHost* render_view_host) {
  if (delegate_)
    delegate_->RendererResponsive(this);
}

void WebContentsImpl::LoadStateChanged(
    const GURL& url,
    const net::LoadStateWithParam& load_state,
    uint64 upload_position,
    uint64 upload_size) {
  load_state_ = load_state;
  upload_position_ = upload_position;
  upload_size_ = upload_size;
  load_state_host_ = net::IDNToUnicode(url.host(),
      content::GetContentClient()->browser()->GetAcceptLangs(
          GetBrowserContext()));
  if (load_state_.state == net::LOAD_STATE_READING_RESPONSE)
    SetNotWaitingForResponse();
  if (IsLoading()) {
    NotifyNavigationStateChanged(
        content::INVALIDATE_TYPE_LOAD | content::INVALIDATE_TYPE_TAB);
  }
}

void WebContentsImpl::WorkerCrashed() {
  if (delegate_)
    delegate_->WorkerCrashed(this);
}

void WebContentsImpl::BeforeUnloadFiredFromRenderManager(
    bool proceed,
    bool* proceed_to_fire_unload) {
  if (delegate_)
    delegate_->BeforeUnloadFired(this, proceed, proceed_to_fire_unload);
}

void WebContentsImpl::RenderViewGoneFromRenderManager(
    RenderViewHost* render_view_host) {
  DCHECK(crashed_status_ != base::TERMINATION_STATUS_STILL_RUNNING);
  RenderViewGone(render_view_host, crashed_status_, crashed_error_code_);
}

void WebContentsImpl::UpdateRenderViewSizeForRenderManager() {
  // TODO(brettw) this is a hack. See WebContentsView::SizeContents.
  gfx::Size size = view_->GetContainerSize();
  // 0x0 isn't a valid window size (minimal window size is 1x1) but it may be
  // here during container initialization and normal window size will be set
  // later. In case of tab duplication this resizing to 0x0 prevents setting
  // normal size later so just ignore it.
  if (!size.IsEmpty())
    view_->SizeContents(size);
}

void WebContentsImpl::NotifySwappedFromRenderManager() {
  NotifySwapped();
}

int WebContentsImpl::CreateOpenerRenderViewsForRenderManager(
    SiteInstance* instance) {
  if (!opener_)
    return MSG_ROUTING_NONE;

  // Recursively create RenderViews for anything else in the opener chain.
  return opener_->CreateOpenerRenderViews(instance);
}

int WebContentsImpl::CreateOpenerRenderViews(SiteInstance* instance) {
  int opener_route_id = MSG_ROUTING_NONE;

  // If this tab has an opener, ensure it has a RenderView in the given
  // SiteInstance as well.
  if (opener_)
    opener_route_id = opener_->CreateOpenerRenderViews(instance);

  // Create a swapped out RenderView in the given SiteInstance if none exists,
  // setting its opener to the given route_id.  Return the new view's route_id.
  return render_manager_.CreateRenderView(instance, opener_route_id, true);
}

NavigationControllerImpl& WebContentsImpl::GetControllerForRenderManager() {
  return GetControllerImpl();
}

WebUIImpl* WebContentsImpl::CreateWebUIForRenderManager(const GURL& url) {
  return static_cast<WebUIImpl*>(CreateWebUI(url));
}

NavigationEntry*
    WebContentsImpl::GetLastCommittedNavigationEntryForRenderManager() {
  return controller_.GetLastCommittedEntry();
}

bool WebContentsImpl::CreateRenderViewForRenderManager(
    RenderViewHost* render_view_host, int opener_route_id) {
  // Can be NULL during tests.
  RenderWidgetHostView* rwh_view = view_->CreateViewForWidget(render_view_host);

  // Now that the RenderView has been created, we need to tell it its size.
  if (rwh_view)
    rwh_view->SetSize(view_->GetContainerSize());

  // Make sure we use the correct starting page_id in the new RenderView.
  UpdateMaxPageIDIfNecessary(render_view_host);
  int32 max_page_id =
      GetMaxPageIDForSiteInstance(render_view_host->GetSiteInstance());

  std::string embedder_channel_name;
  int embedder_container_id;
  GetBrowserPluginEmbedderInfo(render_view_host,
                               &embedder_channel_name,
                               &embedder_container_id);
  if (!static_cast<RenderViewHostImpl*>(
          render_view_host)->CreateRenderView(string16(),
                                              opener_route_id,
                                              max_page_id,
                                              embedder_channel_name,
                                              embedder_container_id)) {
    return false;
  }

#if defined(OS_LINUX) || defined(OS_OPENBSD)
  // Force a ViewMsg_Resize to be sent, needed to make plugins show up on
  // linux. See crbug.com/83941.
  if (rwh_view) {
    if (RenderWidgetHost* render_widget_host = rwh_view->GetRenderWidgetHost())
      render_widget_host->WasResized();
  }
#endif

  return true;
}

void WebContentsImpl::OnDialogClosed(RenderViewHost* rvh,
                                     IPC::Message* reply_msg,
                                     bool success,
                                     const string16& user_input) {
  if (is_showing_before_unload_dialog_ && !success) {
    // If a beforeunload dialog is canceled, we need to stop the throbber from
    // spinning, since we forced it to start spinning in Navigate.
    DidStopLoading(rvh);
    controller_.DiscardNonCommittedEntries();

    close_start_time_ = base::TimeTicks();
  }
  is_showing_before_unload_dialog_ = false;
  static_cast<RenderViewHostImpl*>(
      rvh)->JavaScriptDialogClosed(reply_msg, success, user_input);
}

void WebContentsImpl::SetEncoding(const std::string& encoding) {
  encoding_ = content::GetContentClient()->browser()->
      GetCanonicalEncodingNameByAliasName(encoding);
}

void WebContentsImpl::SaveURL(const GURL& url,
                              const content::Referrer& referrer,
                              bool is_main_frame) {
  DownloadManager* dlm =
      BrowserContext::GetDownloadManager(GetBrowserContext());
  if (!dlm)
    return;
  int64 post_id = -1;
  if (is_main_frame) {
    const NavigationEntry* entry = controller_.GetActiveEntry();
    if (entry)
      post_id = entry->GetPostID();
  }
  content::DownloadSaveInfo save_info;
  save_info.prompt_for_save_location = true;
  scoped_ptr<DownloadUrlParameters> params(
      DownloadUrlParameters::FromWebContents(this, url, save_info));
  params->set_referrer(referrer);
  params->set_post_id(post_id);
  params->set_prefer_cache(true);
  if (post_id >= 0)
    params->set_method("POST");
  dlm->DownloadUrl(params.Pass());
}

void WebContentsImpl::CreateViewAndSetSizeForRVH(RenderViewHost* rvh) {
  RenderWidgetHostView* rwh_view = GetView()->CreateViewForWidget(rvh);
  // Can be NULL during tests.
  if (rwh_view)
    rwh_view->SetSize(GetView()->GetContainerSize());
}

RenderViewHostImpl* WebContentsImpl::GetRenderViewHostImpl() {
  return static_cast<RenderViewHostImpl*>(GetRenderViewHost());
}

void WebContentsImpl::GetBrowserPluginEmbedderInfo(
    content::RenderViewHost* render_view_host,
    std::string* embedder_channel_name,
    int* embedder_container_id) {
  content::RenderProcessHost* embedder_render_process_host =
      browser_plugin_host()->embedder_render_process_host();
  *embedder_container_id = browser_plugin_host()->instance_id();
  int embedder_process_id =
      embedder_render_process_host ? embedder_render_process_host->GetID() : -1;
  if (embedder_process_id != -1) {
    *embedder_channel_name =
        StringPrintf("%d.r%d", render_view_host->GetProcess()->GetID(),
                     embedder_process_id);
  }
}
