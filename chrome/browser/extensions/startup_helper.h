// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_STARTUP_HELPER_H_
#define CHROME_BROWSER_EXTENSIONS_STARTUP_HELPER_H_

#include "base/memory/ref_counted.h"
#include "chrome/browser/extensions/pack_extension_job.h"

class CommandLine;
class Profile;

namespace extensions {

// Initialization helpers for various Extension startup actions.
class StartupHelper : public PackExtensionJob::Client {
 public:
  StartupHelper();
  virtual ~StartupHelper();

  virtual void OnPackSuccess(const FilePath& crx_path,
                             const FilePath& output_private_key_path) OVERRIDE;
  virtual void OnPackFailure(const std::string& error_message,
                             ExtensionCreator::ErrorType type) OVERRIDE;

  // Handle --pack-extension flag from the |cmd_line| by packing the specified
  // extension. Returns false if the pack job failed.
  bool PackExtension(const CommandLine& cmd_line);

  // Handle --uninstall-extension flag from the |cmd_line| by uninstalling the
  // specified extension from |profile|. Returns false if the uninstall job
  // could not be started.
  bool UninstallExtension(const CommandLine& cmd_line, Profile* profile);

  // Handle --install-from-webstore flag from |cmd_line| by downloading
  // metadata from the webstore for the given id, prompting the user to
  // confirm, and then downloading the crx and installing it.
  bool InstallFromWebstore(const CommandLine& cmd_line, Profile* profile);

 private:
  scoped_refptr<PackExtensionJob> pack_job_;
  bool pack_job_succeeded_;

  DISALLOW_COPY_AND_ASSIGN(StartupHelper);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_STARTUP_HELPER_H_
