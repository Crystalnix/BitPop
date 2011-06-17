// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_RENDER_MESSAGE_FILTER_H_
#define CONTENT_BROWSER_RENDERER_HOST_RENDER_MESSAGE_FILTER_H_
#pragma once

#if defined(OS_WIN)
#include <windows.h>
#endif

#include <string>
#include <vector>

#include "base/file_path.h"
#include "base/memory/linked_ptr.h"
#include "base/shared_memory.h"
#include "base/string16.h"
#include "base/task.h"
#include "build/build_config.h"
#include "chrome/common/content_settings.h"
#include "content/browser/browser_message_filter.h"
#include "content/browser/in_process_webkit/webkit_context.h"
#include "content/browser/renderer_host/resource_dispatcher_host.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPopupType.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/surface/transport_dib.h"

class ChromeURLRequestContext;
struct FontDescriptor;
class HostContentSettingsMap;
class HostZoomMap;
class NotificationsPrefsCache;
class Profile;
class RenderWidgetHelper;
struct ViewHostMsg_CreateWindow_Params;
struct ViewHostMsg_CreateWorker_Params;

namespace WebKit {
struct WebScreenInfo;
}

namespace gfx {
class Rect;
}
namespace base {
class SharedMemory;
}

namespace net {
class CookieStore;
class URLRequestContextGetter;
}

namespace webkit {
namespace npapi {
struct WebPluginInfo;
}
}

// This class filters out incoming IPC messages for the renderer process on the
// IPC thread.
class RenderMessageFilter : public BrowserMessageFilter {
 public:
  // Create the filter.
  RenderMessageFilter(int render_process_id,
                      PluginService* plugin_service,
                      Profile* profile,
                      net::URLRequestContextGetter* request_context,
                      RenderWidgetHelper* render_widget_helper);

  // BrowserMessageFilter methods:
  virtual void OverrideThreadForMessage(const IPC::Message& message,
                                        BrowserThread::ID* thread);
  virtual bool OnMessageReceived(const IPC::Message& message,
                                 bool* message_was_ok);
  virtual void OnDestruct() const;

  int render_process_id() const { return render_process_id_; }
  ResourceDispatcherHost* resource_dispatcher_host() {
    return resource_dispatcher_host_;
  }
  bool incognito() { return incognito_; }

  // Returns either the extension net::URLRequestContext or regular
  // net::URLRequestContext depending on whether |url| is an extension URL.
  // Only call on the IO thread.
  ChromeURLRequestContext* GetRequestContextForURL(const GURL& url);

 private:
  friend class BrowserThread;
  friend class DeleteTask<RenderMessageFilter>;

  virtual ~RenderMessageFilter();

  void OnMsgCreateWindow(const ViewHostMsg_CreateWindow_Params& params,
                         int* route_id,
                         int64* cloned_session_storage_namespace_id);
  void OnMsgCreateWidget(int opener_id,
                         WebKit::WebPopupType popup_type,
                         int* route_id);
  void OnMsgCreateFullscreenWidget(int opener_id, int* route_id);
  void OnSetCookie(const IPC::Message& message,
                   const GURL& url,
                   const GURL& first_party_for_cookies,
                   const std::string& cookie);
  void OnGetCookies(const GURL& url,
                    const GURL& first_party_for_cookies,
                    IPC::Message* reply_msg);
  void OnGetRawCookies(const GURL& url,
                       const GURL& first_party_for_cookies,
                       IPC::Message* reply_msg);
  void OnDeleteCookie(const GURL& url,
                      const std::string& cookieName);
  void OnCookiesEnabled(const GURL& url,
                        const GURL& first_party_for_cookies,
                        bool* cookies_enabled);
  void OnPluginFileDialog(const IPC::Message& msg,
                          bool multiple_files,
                          const std::wstring& title,
                          const std::wstring& filter,
                          uint32 user_data);

#if defined(OS_MACOSX)
  void OnLoadFont(const FontDescriptor& font,
                  uint32* handle_size,
                  base::SharedMemoryHandle* handle);
#endif

#if defined(OS_WIN)  // This hack is Windows-specific.
  // Cache fonts for the renderer. See RenderMessageFilter::OnPreCacheFont
  // implementation for more details.
  void OnPreCacheFont(LOGFONT font);
#endif

#if !defined(OS_MACOSX)
  // Not handled in the IO thread on Mac.
  void OnGetScreenInfo(gfx::NativeViewId window,
                       WebKit::WebScreenInfo* results);
  void OnGetWindowRect(gfx::NativeViewId window, gfx::Rect* rect);
  void OnGetRootWindowRect(gfx::NativeViewId window, gfx::Rect* rect);
#endif
  void OnGetPlugins(bool refresh,
                    std::vector<webkit::npapi::WebPluginInfo>* plugins);
  void OnGetPluginInfo(int routing_id,
                       const GURL& url,
                       const GURL& policy_url,
                       const std::string& mime_type,
                       bool* found,
                       webkit::npapi::WebPluginInfo* info,
                       int* setting,
                       std::string* actual_mime_type);
  void OnOpenChannelToPlugin(int routing_id,
                             const GURL& url,
                             const std::string& mime_type,
                             IPC::Message* reply_msg);
  void OnOpenChannelToPepperPlugin(const FilePath& path,
                                   IPC::Message* reply_msg);
  void OnOpenChannelToPpapiBroker(int routing_id,
                                  int request_id,
                                  const FilePath& path);
  void OnGenerateRoutingID(int* route_id);
  void OnDownloadUrl(const IPC::Message& message,
                     const GURL& url,
                     const GURL& referrer);
  void OnCheckNotificationPermission(const GURL& source_url,
                                     int* permission_level);

  void OnRevealFolderInOS(const FilePath& path);

// Used to ask the browser to allocate a block of shared memory for the
  // renderer to send back data in, since shared memory can't be created
  // in the renderer on POSIX due to the sandbox.
  void OnAllocateSharedMemoryBuffer(uint32 buffer_size,
                                    base::SharedMemoryHandle* handle);
  void OnDidZoomURL(const IPC::Message& message,
                    double zoom_level,
                    bool remember,
                    const GURL& url);
  void UpdateHostZoomLevelsOnUIThread(double zoom_level,
                                      bool remember,
                                      const GURL& url,
                                      int render_process_id,
                                      int render_view_id);

  void OnResolveProxy(const GURL& url, IPC::Message* reply_msg);

  // Browser side transport DIB allocation
  void OnAllocTransportDIB(size_t size,
                           bool cache_in_browser,
                           TransportDIB::Handle* result);
  void OnFreeTransportDIB(TransportDIB::Id dib_id);
  void OnCloseCurrentConnections();
  void OnSetCacheMode(bool enabled);
  void OnClearCache(bool preserve_ssl_host_info, IPC::Message* reply_msg);
  void OnClearHostResolverCache(int* result);
  void OnClearPredictorCache(int* result);
  void OnCacheableMetadataAvailable(const GURL& url,
                                    double expected_response_time,
                                    const std::vector<char>& data);
  void OnEnableSpdy(bool enable);
  void OnKeygen(uint32 key_size_index, const std::string& challenge_string,
                const GURL& url, IPC::Message* reply_msg);
  void OnKeygenOnWorkerThread(
      int key_size_in_bits,
      const std::string& challenge_string,
      const GURL& url,
      IPC::Message* reply_msg);
  void OnAsyncOpenFile(const IPC::Message& msg,
                       const FilePath& path,
                       int flags,
                       int message_id);
  void AsyncOpenFileOnFileThread(const FilePath& path,
                                 int flags,
                                 int message_id,
                                 int routing_id);

  bool CheckBenchmarkingEnabled() const;
  bool CheckPreparsedJsCachingEnabled() const;

  // Cached resource request dispatcher host and plugin service, guaranteed to
  // be non-null if Init succeeds. We do not own the objects, they are managed
  // by the BrowserProcess, which has a wider scope than we do.
  ResourceDispatcherHost* resource_dispatcher_host_;
  PluginService* plugin_service_;

  // The Profile associated with our renderer process.  This should only be
  // accessed on the UI thread!
  Profile* profile_;

  // The host content settings map. Stored separately from the profile so we can
  // access it on other threads.
  HostContentSettingsMap* content_settings_;

  // Contextual information to be used for requests created here.
  scoped_refptr<net::URLRequestContextGetter> request_context_;

  // A request context that holds a cookie store for chrome-extension URLs.
  scoped_refptr<net::URLRequestContextGetter> extensions_request_context_;

  scoped_refptr<RenderWidgetHelper> render_widget_helper_;

  // A cache of notifications preferences which is used to handle
  // Desktop Notifications permission messages.
  scoped_refptr<NotificationsPrefsCache> notification_prefs_;

  // Handles zoom-related messages.
  scoped_refptr<HostZoomMap> host_zoom_map_;

  // Whether this process is used for incognito tabs.
  bool incognito_;

  // Initialized to 0, accessed on FILE thread only.
  base::TimeTicks last_plugin_refresh_time_;

  scoped_refptr<WebKitContext> webkit_context_;

  int render_process_id_;

  DISALLOW_COPY_AND_ASSIGN(RenderMessageFilter);
};

// These classes implement completion callbacks for getting and setting
// cookies.
class SetCookieCompletion : public net::CompletionCallback {
 public:
  SetCookieCompletion(int render_process_id,
                      int render_view_id,
                      const GURL& url,
                      const std::string& cookie_line,
                      ChromeURLRequestContext* context);
  virtual ~SetCookieCompletion();

  virtual void RunWithParams(const Tuple1<int>& params);

  int render_process_id() const {
    return render_process_id_;
  }

  int render_view_id() const {
    return render_view_id_;
  }

 private:
  int render_process_id_;
  int render_view_id_;
  GURL url_;
  std::string cookie_line_;
  scoped_refptr<ChromeURLRequestContext> context_;
};

class GetCookiesCompletion : public net::CompletionCallback {
 public:
  GetCookiesCompletion(int render_process_id,
                       int render_view_id,
                       const GURL& url, IPC::Message* reply_msg,
                       RenderMessageFilter* filter,
                       ChromeURLRequestContext* context,
                       bool raw_cookies);
  virtual ~GetCookiesCompletion();

  virtual void RunWithParams(const Tuple1<int>& params);

  int render_process_id() const {
    return render_process_id_;
  }

  int render_view_id() const {
    return render_view_id_;
  }

  void set_cookie_store(net::CookieStore* cookie_store);

  net::CookieStore* cookie_store() {
    return cookie_store_.get();
  }

 private:
  GURL url_;
  IPC::Message* reply_msg_;
  scoped_refptr<RenderMessageFilter> filter_;
  scoped_refptr<ChromeURLRequestContext> context_;
  int render_process_id_;
  int render_view_id_;
  bool raw_cookies_;
  scoped_refptr<net::CookieStore> cookie_store_;
};

#endif  // CONTENT_BROWSER_RENDERER_HOST_RENDER_MESSAGE_FILTER_H_
