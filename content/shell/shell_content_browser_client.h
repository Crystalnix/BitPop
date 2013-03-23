// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_SHELL_CONTENT_BROWSER_CLIENT_H_
#define CONTENT_SHELL_SHELL_CONTENT_BROWSER_CLIENT_H_

#include <string>

#include "base/compiler_specific.h"
#include "base/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/browser/content_browser_client.h"

namespace content {

class ShellBrowserContext;
class ShellBrowserMainParts;
class ShellResourceDispatcherHostDelegate;

class ShellContentBrowserClient : public ContentBrowserClient {
 public:
  ShellContentBrowserClient();
  virtual ~ShellContentBrowserClient();

  // ContentBrowserClient overrides.
  virtual BrowserMainParts* CreateBrowserMainParts(
      const MainFunctionParams& parameters) OVERRIDE;
  virtual void RenderProcessHostCreated(RenderProcessHost* host) OVERRIDE;
  virtual void AppendExtraCommandLineSwitches(CommandLine* command_line,
                                              int child_process_id) OVERRIDE;
  virtual void OverrideWebkitPrefs(RenderViewHost* render_view_host,
                                   const GURL& url,
                                   webkit_glue::WebPreferences* prefs) OVERRIDE;
  virtual void ResourceDispatcherHostCreated() OVERRIDE;
  virtual AccessTokenStore* CreateAccessTokenStore() OVERRIDE;
  virtual std::string GetDefaultDownloadName() OVERRIDE;
  virtual WebContentsViewDelegate* GetWebContentsViewDelegate(
      WebContents* web_contents) OVERRIDE;
  virtual bool CanCreateWindow(
      const GURL& opener_url,
      const GURL& source_origin,
      WindowContainerType container_type,
      ResourceContext* context,
      int render_process_id,
      bool* no_javascript_access) OVERRIDE;

#if defined(OS_ANDROID)
  virtual void GetAdditionalMappedFilesForChildProcess(
      const CommandLine& command_line,
      int child_process_id,
      std::vector<content::FileDescriptorInfo>* mappings) OVERRIDE;
#endif

  ShellBrowserContext* browser_context();
  ShellBrowserContext* off_the_record_browser_context();
  ShellResourceDispatcherHostDelegate* resource_dispatcher_host_delegate() {
    return resource_dispatcher_host_delegate_.get();
  }
  ShellBrowserMainParts* shell_browser_main_parts() {
    return shell_browser_main_parts_;
  }

 private:
  scoped_ptr<ShellResourceDispatcherHostDelegate>
      resource_dispatcher_host_delegate_;

  FilePath webkit_source_dir_;

  ShellBrowserMainParts* shell_browser_main_parts_;
};

}  // namespace content

#endif  // CONTENT_SHELL_SHELL_CONTENT_BROWSER_CLIENT_H_
