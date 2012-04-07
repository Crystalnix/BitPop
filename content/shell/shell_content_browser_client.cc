// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/shell_content_browser_client.h"

#include "base/command_line.h"
#include "base/file_path.h"
#include "content/shell/shell.h"
#include "content/shell/shell_browser_main.h"
#include "content/shell/shell_devtools_delegate.h"
#include "content/shell/shell_render_view_host_observer.h"
#include "content/shell/shell_switches.h"
#include "googleurl/src/gurl.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "webkit/glue/webpreferences.h"

#if defined(OS_WIN)
#include "content/browser/tab_contents/tab_contents.h"
#include "content/browser/tab_contents/tab_contents_view_win.h"
#include "content/common/view_messages.h"
#elif defined(OS_LINUX)
#include "content/browser/tab_contents/tab_contents_view_gtk.h"
#endif

namespace content {

ShellContentBrowserClient::ShellContentBrowserClient()
    : shell_browser_main_parts_(NULL) {
}

ShellContentBrowserClient::~ShellContentBrowserClient() {
}

BrowserMainParts* ShellContentBrowserClient::CreateBrowserMainParts(
    const content::MainFunctionParams& parameters) {
  return new ShellBrowserMainParts(parameters);
}

WebContentsView* ShellContentBrowserClient::CreateWebContentsView(
    WebContents* web_contents) {
  ShellDevToolsDelegate* devtools_delegate =
      shell_browser_main_parts_->devtools_delegate();
  if (devtools_delegate)
    devtools_delegate->AddWebContents(web_contents);

#if defined(OS_WIN)
  return new TabContentsViewWin(web_contents);
#elif defined(OS_LINUX)
  return new TabContentsViewGtk(web_contents, NULL);
#else
  return NULL;
#endif
}

void ShellContentBrowserClient::RenderViewHostCreated(
    RenderViewHost* render_view_host) {
  new ShellRenderViewHostObserver(render_view_host);
}

void ShellContentBrowserClient::RenderProcessHostCreated(
    RenderProcessHost* host) {
}

WebUIControllerFactory* ShellContentBrowserClient::GetWebUIControllerFactory() {
  return NULL;
}

GURL ShellContentBrowserClient::GetEffectiveURL(
    content::BrowserContext* browser_context, const GURL& url) {
  return GURL();
}

bool ShellContentBrowserClient::ShouldUseProcessPerSite(
    BrowserContext* browser_context, const GURL& effective_url) {
  return false;
}

bool ShellContentBrowserClient::IsURLSameAsAnySiteInstance(const GURL& url) {
  return false;
}

bool ShellContentBrowserClient::IsHandledURL(const GURL& url) {
  return false;
}

bool ShellContentBrowserClient::IsSuitableHost(
    RenderProcessHost* process_host,
    const GURL& site_url) {
  return true;
}

void ShellContentBrowserClient::SiteInstanceGotProcess(
    SiteInstance* site_instance) {
}

void ShellContentBrowserClient::SiteInstanceDeleting(
    SiteInstance* site_instance) {
}

bool ShellContentBrowserClient::ShouldSwapProcessesForNavigation(
    const GURL& current_url,
    const GURL& new_url) {
  return false;
}

std::string ShellContentBrowserClient::GetCanonicalEncodingNameByAliasName(
    const std::string& alias_name) {
  return std::string();
}

void ShellContentBrowserClient::AppendExtraCommandLineSwitches(
    CommandLine* command_line, int child_process_id) {
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kDumpRenderTree))
    command_line->AppendSwitch(switches::kDumpRenderTree);
}

std::string ShellContentBrowserClient::GetApplicationLocale() {
  return std::string();
}

std::string ShellContentBrowserClient::GetAcceptLangs(
    content::BrowserContext* context) {
  return std::string();
}

SkBitmap* ShellContentBrowserClient::GetDefaultFavicon() {
  static SkBitmap empty;
  return &empty;
}

bool ShellContentBrowserClient::AllowAppCache(
    const GURL& manifest_url,
    const GURL& first_party,
    const content::ResourceContext& context) {
  return true;
}

bool ShellContentBrowserClient::AllowGetCookie(
    const GURL& url,
    const GURL& first_party,
    const net::CookieList& cookie_list,
    const content::ResourceContext& context,
    int render_process_id,
    int render_view_id) {
  return true;
}

bool ShellContentBrowserClient::AllowSetCookie(
    const GURL& url,
    const GURL& first_party,
    const std::string& cookie_line,
    const content::ResourceContext& context,
    int render_process_id,
    int render_view_id,
    net::CookieOptions* options) {
  return true;
}

bool ShellContentBrowserClient::AllowSaveLocalState(
    const content::ResourceContext& context) {
  return true;
}

bool ShellContentBrowserClient::AllowWorkerDatabase(
    int worker_route_id,
    const GURL& url,
    const string16& name,
    const string16& display_name,
    unsigned long estimated_size,
    WorkerProcessHost* worker_process_host) {
  return true;
}

bool ShellContentBrowserClient::AllowWorkerFileSystem(
    int worker_route_id,
    const GURL& url,
    WorkerProcessHost* worker_process_host) {
  return true;
}

QuotaPermissionContext*
    ShellContentBrowserClient::CreateQuotaPermissionContext() {
  return NULL;
}

net::URLRequestContext* ShellContentBrowserClient::OverrideRequestContextForURL(
    const GURL& url, const content::ResourceContext& context) {
  return NULL;
}

void ShellContentBrowserClient::OpenItem(const FilePath& path) {
}

void ShellContentBrowserClient::ShowItemInFolder(const FilePath& path) {
}

void ShellContentBrowserClient::AllowCertificateError(
    SSLCertErrorHandler* handler,
    bool overridable,
    const base::Callback<void(SSLCertErrorHandler*, bool)>& callback) {
}

void ShellContentBrowserClient::SelectClientCertificate(
    int render_process_id,
    int render_view_id,
    SSLClientAuthHandler* handler) {
}

void ShellContentBrowserClient::AddNewCertificate(
    net::URLRequest* request,
    net::X509Certificate* cert,
    int render_process_id,
    int render_view_id) {
}

void ShellContentBrowserClient::RequestDesktopNotificationPermission(
    const GURL& source_origin,
    int callback_context,
    int render_process_id,
    int render_view_id) {
}

WebKit::WebNotificationPresenter::Permission
    ShellContentBrowserClient::CheckDesktopNotificationPermission(
        const GURL& source_origin,
        const content::ResourceContext& context,
        int render_process_id) {
  return WebKit::WebNotificationPresenter::PermissionAllowed;
}

void ShellContentBrowserClient::ShowDesktopNotification(
    const content::ShowDesktopNotificationHostMsgParams& params,
    int render_process_id,
    int render_view_id,
    bool worker) {
}

void ShellContentBrowserClient::CancelDesktopNotification(
    int render_process_id,
    int render_view_id,
    int notification_id) {
}

bool ShellContentBrowserClient::CanCreateWindow(
    const GURL& origin,
    WindowContainerType container_type,
    const content::ResourceContext& context,
    int render_process_id) {
  return true;
}

std::string ShellContentBrowserClient::GetWorkerProcessTitle(
    const GURL& url, const content::ResourceContext& context) {
  return std::string();
}

void ShellContentBrowserClient::ResourceDispatcherHostCreated() {
}

ui::Clipboard* ShellContentBrowserClient::GetClipboard() {
  return shell_browser_main_parts_->GetClipboard();
}

MHTMLGenerationManager* ShellContentBrowserClient::GetMHTMLGenerationManager() {
  return NULL;
}

net::NetLog* ShellContentBrowserClient::GetNetLog() {
  return NULL;
}

speech_input::SpeechInputManager*
    ShellContentBrowserClient::GetSpeechInputManager() {
  return NULL;
}

AccessTokenStore* ShellContentBrowserClient::CreateAccessTokenStore() {
  return NULL;
}

bool ShellContentBrowserClient::IsFastShutdownPossible() {
  return true;
}

WebPreferences ShellContentBrowserClient::GetWebkitPrefs(RenderViewHost* rvh) {
  return WebPreferences();
}

void ShellContentBrowserClient::UpdateInspectorSetting(
    RenderViewHost* rvh, const std::string& key, const std::string& value) {
}

void ShellContentBrowserClient::ClearInspectorSettings(RenderViewHost* rvh) {
}

void ShellContentBrowserClient::BrowserURLHandlerCreated(
    BrowserURLHandler* handler) {
}

void ShellContentBrowserClient::ClearCache(RenderViewHost* rvh) {
}

void ShellContentBrowserClient::ClearCookies(RenderViewHost* rvh) {
}

FilePath ShellContentBrowserClient::GetDefaultDownloadDirectory() {
  return FilePath();
}

std::string ShellContentBrowserClient::GetDefaultDownloadName() {
  return "download";
}

bool ShellContentBrowserClient::AllowSocketAPI(const GURL& url) {
  return false;
}

#if defined(OS_POSIX) && !defined(OS_MACOSX)
int ShellContentBrowserClient::GetCrashSignalFD(
    const CommandLine& command_line) {
  return -1;
}
#endif

#if defined(OS_WIN)
const wchar_t* ShellContentBrowserClient::GetResourceDllName() {
  return NULL;
}
#endif

#if defined(USE_NSS)
crypto::CryptoModuleBlockingPasswordDelegate*
    ShellContentBrowserClient::GetCryptoPasswordDelegate(const GURL& url) {
  return NULL;
}
#endif

}  // namespace content
