// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXECUTE_CODE_IN_TAB_FUNCTION_H__
#define CHROME_BROWSER_EXTENSIONS_EXECUTE_CODE_IN_TAB_FUNCTION_H__
#pragma once

#include <string>

#include "chrome/browser/extensions/extension_function.h"
#include "chrome/common/extensions/extension_resource.h"
#include "content/public/browser/web_contents_observer.h"

// Implement API call tabs.executeScript and tabs.insertCSS.
class ExecuteCodeInTabFunction : public AsyncExtensionFunction,
                                 public content::WebContentsObserver {
 public:
  ExecuteCodeInTabFunction();
  virtual ~ExecuteCodeInTabFunction();

 private:
  virtual bool RunImpl() OVERRIDE;

  // content::WebContentsObserver overrides.
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

  // Message handler.
  void OnExecuteCodeFinished(int request_id, bool success,
                             const std::string& error);

  // Called when contents from the file whose path is specified in JSON
  // arguments has been loaded.
  void DidLoadFile(bool success, const std::string& data);

  // Runs on FILE thread. Loads message bundles for the extension and
  // localizes the CSS data. Calls back DidLoadAndLocalizeFile on the UI thread.
  void LocalizeCSS(
      const std::string& data,
      const std::string& extension_id,
      const FilePath& extension_path,
      const std::string& extension_default_locale);

  // Called when contents from the loaded file have been localized.
  void DidLoadAndLocalizeFile(bool success, const std::string& data);

  // Run in UI thread.  Code string contains the code to be executed. Returns
  // true on success. If true is returned, this does an AddRef.
  bool Execute(const std::string& code_string);

  // Id of tab which executes code.
  int execute_tab_id_;

  // Contains extension resource built from path of file which is
  // specified in JSON arguments.
  ExtensionResource resource_;

  // If all_frames_ is true, script or CSS text would be injected
  // to all frames; Otherwise only injected to top main frame.
  bool all_frames_;
};

class TabsExecuteScriptFunction : public ExecuteCodeInTabFunction {
  DECLARE_EXTENSION_FUNCTION_NAME("tabs.executeScript")
};

class TabsInsertCSSFunction : public ExecuteCodeInTabFunction {
  DECLARE_EXTENSION_FUNCTION_NAME("tabs.insertCSS")
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXECUTE_CODE_IN_TAB_FUNCTION_H__
