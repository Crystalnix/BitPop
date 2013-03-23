// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/devtools_ui.h"

#include <string>

#include "base/memory/ref_counted_memory.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_util.h"
#include "chrome/browser/net/chrome_url_request_context.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/devtools_client_host.h"
#include "content/public/browser/devtools_http_handler.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/resource/resource_bundle.h"

using content::BrowserThread;
using content::WebContents;

namespace {

std::string PathWithoutParams(const std::string& path) {
  return GURL(std::string("chrome-devtools://devtools/") + path)
      .path().substr(1);
}

}  // namespace

class DevToolsDataSource : public ChromeURLDataManager::DataSource {
 public:
  DevToolsDataSource();

  virtual void StartDataRequest(const std::string& path,
                                bool is_incognito,
                                int request_id);
  virtual std::string GetMimeType(const std::string& path) const;

 private:
  ~DevToolsDataSource() {}
  DISALLOW_COPY_AND_ASSIGN(DevToolsDataSource);
};


DevToolsDataSource::DevToolsDataSource()
    : DataSource(chrome::kChromeUIDevToolsHost, NULL) {
}

void DevToolsDataSource::StartDataRequest(const std::string& path,
                                          bool is_incognito,
                                          int request_id) {
  std::string filename = PathWithoutParams(path);


  int resource_id =
      content::DevToolsHttpHandler::GetFrontendResourceId(filename);

  DLOG_IF(WARNING, -1 == resource_id) << "Unable to find dev tool resource: "
      << filename << ". If you compiled with debug_devtools=1, try running"
      " with --debug-devtools.";
  const ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  scoped_refptr<base::RefCountedStaticMemory> bytes(rb.LoadDataResourceBytes(
      resource_id));
  SendResponse(request_id, bytes);
}

std::string DevToolsDataSource::GetMimeType(const std::string& path) const {
  std::string filename = PathWithoutParams(path);
  if (EndsWith(filename, ".html", false)) {
    return "text/html";
  } else if (EndsWith(filename, ".css", false)) {
    return "text/css";
  } else if (EndsWith(filename, ".js", false)) {
    return "application/javascript";
  } else if (EndsWith(filename, ".png", false)) {
    return "image/png";
  } else if (EndsWith(filename, ".gif", false)) {
    return "image/gif";
  }
  NOTREACHED();
  return "text/plain";
}

// static
void DevToolsUI::RegisterDevToolsDataSource(Profile* profile) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  static bool registered = false;
  if (!registered) {
    DevToolsDataSource* data_source = new DevToolsDataSource();
    ChromeURLDataManager::AddDataSource(profile, data_source);
    registered = true;
  }
}

DevToolsUI::DevToolsUI(content::WebUI* web_ui) : WebUIController(web_ui) {
  DevToolsDataSource* data_source = new DevToolsDataSource();
  Profile* profile = Profile::FromWebUI(web_ui);
  ChromeURLDataManager::AddDataSource(profile, data_source);
}

void DevToolsUI::RenderViewCreated(
    content::RenderViewHost* render_view_host) {
  content::DevToolsClientHost::SetupDevToolsFrontendClient(render_view_host);
}
