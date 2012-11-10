// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/content_browser_client.h"

#include "base/file_path.h"
#include "googleurl/src/gurl.h"
#include "ui/gfx/image/image_skia.h"

namespace content {

BrowserMainParts* ContentBrowserClient::CreateBrowserMainParts(
    const MainFunctionParams& parameters) {
  return NULL;
}

WebContentsView* ContentBrowserClient::OverrideCreateWebContentsView(
    WebContents* web_contents,
    RenderViewHostDelegateView** render_view_host_delegate_view) {
  return NULL;
}

WebContentsViewDelegate* ContentBrowserClient::GetWebContentsViewDelegate(
    WebContents* web_contents) {
  return NULL;
}

WebUIControllerFactory* ContentBrowserClient::GetWebUIControllerFactory() {
  return NULL;
}

GURL ContentBrowserClient::GetEffectiveURL(BrowserContext* browser_context,
                                           const GURL& url) {
  return url;
}

bool ContentBrowserClient::ShouldUseProcessPerSite(
    BrowserContext* browser_context, const GURL& effective_url) {
  return false;
}

bool ContentBrowserClient::IsHandledURL(const GURL& url) {
  return false;
}

bool ContentBrowserClient::IsSuitableHost(RenderProcessHost* process_host,
                                          const GURL& site_url) {
  return true;
}

bool ContentBrowserClient::ShouldTryToUseExistingProcessHost(
      BrowserContext* browser_context, const GURL& url) {
  return false;
}

bool ContentBrowserClient::ShouldSwapProcessesForNavigation(
    const GURL& current_url,
    const GURL& new_url) {
  return false;
}

bool ContentBrowserClient::ShouldSwapProcessesForRedirect(
    ResourceContext* resource_context, const GURL& current_url,
    const GURL& new_url) {
  return false;
}

std::string ContentBrowserClient::GetCanonicalEncodingNameByAliasName(
    const std::string& alias_name) {
  return std::string();
}

std::string ContentBrowserClient::GetApplicationLocale() {
  return std::string();
}

std::string ContentBrowserClient::GetAcceptLangs(BrowserContext* context) {
  return std::string();
}

gfx::ImageSkia* ContentBrowserClient::GetDefaultFavicon() {
  static gfx::ImageSkia* empty = new gfx::ImageSkia();
  return empty;
}

bool ContentBrowserClient::AllowAppCache(const GURL& manifest_url,
                                         const GURL& first_party,
                                         ResourceContext* context) {
  return true;
}

bool ContentBrowserClient::AllowGetCookie(const GURL& url,
                                          const GURL& first_party,
                                          const net::CookieList& cookie_list,
                                          ResourceContext* context,
                                          int render_process_id,
                                          int render_view_id) {
  return true;
}

bool ContentBrowserClient::AllowSetCookie(const GURL& url,
                                          const GURL& first_party,
                                          const std::string& cookie_line,
                                          ResourceContext* context,
                                          int render_process_id,
                                          int render_view_id,
                                          net::CookieOptions* options) {
  return true;
}

bool ContentBrowserClient::AllowPluginLocalDataAccess(
    const GURL& document_url,
    const GURL& plugin_url,
    content::ResourceContext* context) {
  return true;
}

bool ContentBrowserClient::AllowPluginLocalDataSessionOnly(
    const GURL& url,
    content::ResourceContext* context) {
  return false;
}

bool ContentBrowserClient::AllowSaveLocalState(ResourceContext* context) {
  return true;
}

bool ContentBrowserClient::AllowWorkerDatabase(
    const GURL& url,
    const string16& name,
    const string16& display_name,
    unsigned long estimated_size,
    ResourceContext* context,
    const std::vector<std::pair<int, int> >& render_views) {
  return true;
}

bool ContentBrowserClient::AllowWorkerFileSystem(
    const GURL& url,
    ResourceContext* context,
    const std::vector<std::pair<int, int> >& render_views) {
  return true;
}

bool ContentBrowserClient::AllowWorkerIndexedDB(
    const GURL& url,
    const string16& name,
    ResourceContext* context,
    const std::vector<std::pair<int, int> >& render_views) {
  return true;
}

QuotaPermissionContext* ContentBrowserClient::CreateQuotaPermissionContext() {
  return NULL;
}

net::URLRequestContext* ContentBrowserClient::OverrideRequestContextForURL(
    const GURL& url, ResourceContext* context) {
  return NULL;
}

std::string ContentBrowserClient::GetStoragePartitionIdForChildProcess(
    content::BrowserContext* browser_context,
    int child_process_id) {
  return std::string();
}

MediaObserver* ContentBrowserClient::GetMediaObserver() {
  return NULL;
}

WebKit::WebNotificationPresenter::Permission
    ContentBrowserClient::CheckDesktopNotificationPermission(
        const GURL& source_origin,
        ResourceContext* context,
        int render_process_id) {
  return WebKit::WebNotificationPresenter::PermissionAllowed;
}

bool ContentBrowserClient::CanCreateWindow(const GURL& opener_url,
                                           const GURL& origin,
                                           WindowContainerType container_type,
                                           ResourceContext* context,
                                           int render_process_id,
                                           bool* no_javascript_access) {
  *no_javascript_access = false;
  return true;
}

std::string ContentBrowserClient::GetWorkerProcessTitle(
    const GURL& url, ResourceContext* context) {
  return std::string();
}

SpeechRecognitionManagerDelegate*
    ContentBrowserClient::GetSpeechRecognitionManagerDelegate() {
  return NULL;
}

net::NetLog* ContentBrowserClient::GetNetLog() {
  return NULL;
}

AccessTokenStore* ContentBrowserClient::CreateAccessTokenStore() {
  return NULL;
}

bool ContentBrowserClient::IsFastShutdownPossible() {
  return true;
}

FilePath ContentBrowserClient::GetDefaultDownloadDirectory() {
  return FilePath();
}

std::string ContentBrowserClient::GetDefaultDownloadName() {
  return std::string();
}

bool ContentBrowserClient::AllowPepperSocketAPI(
    BrowserContext* browser_context, const GURL& url) {
  return false;
}

bool ContentBrowserClient::AllowPepperPrivateFileAPI() {
  return false;
}

#if defined(OS_WIN)
const wchar_t* ContentBrowserClient::GetResourceDllName() {
  return NULL;
}
#endif

#if defined(USE_NSS)
crypto::CryptoModuleBlockingPasswordDelegate*
    ContentBrowserClient::GetCryptoPasswordDelegate(const GURL& url) {
  return NULL;
}
#endif

}  // namespace content
