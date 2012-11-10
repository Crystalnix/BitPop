// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extensions_startup.h"

#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"

namespace {

void PrintPackExtensionMessage(const std::string& message) {
  base::StringPrintf("%s\n", message.c_str());
}

}  // namespace

ExtensionsStartupUtil::ExtensionsStartupUtil() : pack_job_succeeded_(false) {}

void ExtensionsStartupUtil::OnPackSuccess(
    const FilePath& crx_path,
    const FilePath& output_private_key_path) {
  pack_job_succeeded_ = true;
  PrintPackExtensionMessage(
      UTF16ToUTF8(
          PackExtensionJob::StandardSuccessMessage(crx_path,
                                                   output_private_key_path)));
}

void ExtensionsStartupUtil::OnPackFailure(
    const std::string& error_message,
    extensions::ExtensionCreator::ErrorType type) {
  PrintPackExtensionMessage(error_message);
}

bool ExtensionsStartupUtil::PackExtension(const CommandLine& cmd_line) {
  if (!cmd_line.HasSwitch(switches::kPackExtension))
    return false;

  // Input Paths.
  FilePath src_dir = cmd_line.GetSwitchValuePath(switches::kPackExtension);
  FilePath private_key_path;
  if (cmd_line.HasSwitch(switches::kPackExtensionKey)) {
    private_key_path = cmd_line.GetSwitchValuePath(switches::kPackExtensionKey);
  }

  // Launch a job to perform the packing on the file thread.  Ignore warnings
  // from the packing process. (e.g. Overwrite any existing crx file.)
  pack_job_ = new PackExtensionJob(this, src_dir, private_key_path,
                                   extensions::ExtensionCreator::kOverwriteCRX);
  pack_job_->set_asynchronous(false);
  pack_job_->Start();

  return pack_job_succeeded_;
}

bool ExtensionsStartupUtil::UninstallExtension(const CommandLine& cmd_line,
                                               Profile* profile) {
  DCHECK(profile);

  if (!cmd_line.HasSwitch(switches::kUninstallExtension))
    return false;

  ExtensionService* extension_service = profile->GetExtensionService();
  if (!extension_service)
    return false;

  std::string extension_id = cmd_line.GetSwitchValueASCII(
      switches::kUninstallExtension);
  return ExtensionService::UninstallExtensionHelper(extension_service,
                                                    extension_id);
}

ExtensionsStartupUtil::~ExtensionsStartupUtil() {
  if (pack_job_.get())
    pack_job_->ClearClient();
}
