// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS_PACK_EXTENSION_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS_PACK_EXTENSION_HANDLER_H_
#pragma once

#include <string>

#include "chrome/browser/browsing_data_remover.h"
#include "chrome/browser/extensions/pack_extension_job.h"
#include "chrome/browser/plugin_data_remover_helper.h"
#include "chrome/browser/ui/webui/options/options_ui.h"

// Clear browser data handler page UI handler.
class PackExtensionHandler : public OptionsPageUIHandler,
                             public PackExtensionJob::Client {
 public:
  PackExtensionHandler();
  virtual ~PackExtensionHandler();

  // OptionsPageUIHandler implementation.
  virtual void Initialize() OVERRIDE;
  virtual void GetLocalizedValues(DictionaryValue* localized_strings) OVERRIDE;

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  // ExtensionPackJob::Client
  virtual void OnPackSuccess(const FilePath& crx_file,
                             const FilePath& key_file) OVERRIDE;

  virtual void OnPackFailure(const std::string& error,
                             ExtensionCreator::ErrorType) OVERRIDE;

 private:
  // Javascript callback to start packing an extension.
  void HandlePackMessage(const ListValue* args);

  // A function to ask the webpage to show an alert.
  void ShowAlert(const std::string& message);

  // Used to package the extension.
  scoped_refptr<PackExtensionJob> pack_job_;

  // Path to root directory of extension
  std::string extension_path_;

  // Path to private key file, or null if none specified
  std::string private_key_path_;

  DISALLOW_COPY_AND_ASSIGN(PackExtensionHandler);
};

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS_PACK_EXTENSION_HANDLER_H_
