// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/pack_extension_handler.h"

#include "chrome/browser/extensions/extension_creator.h"
#include "base/bind.h"
#include "base/utf_string_conversions.h"
#include "content/public/browser/web_ui.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

PackExtensionHandler::PackExtensionHandler() {
}

PackExtensionHandler::~PackExtensionHandler() {
  if (pack_job_.get())
    pack_job_->ClearClient();
}

void PackExtensionHandler::Initialize() {
}

void PackExtensionHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);
  RegisterTitle(localized_strings, "packExtensionOverlay",
                IDS_EXTENSION_PACK_DIALOG_TITLE);

  localized_strings->SetString("packExtensionOverlay",
      l10n_util::GetStringUTF16(IDS_EXTENSION_PACK_DIALOG_TITLE));
  localized_strings->SetString("packExtensionHeading",
      l10n_util::GetStringUTF16(IDS_EXTENSION_PACK_DIALOG_HEADING));
  localized_strings->SetString("packExtensionCommit",
      l10n_util::GetStringUTF16(IDS_EXTENSION_PACK_BUTTON));
  localized_strings->SetString("ok", l10n_util::GetStringUTF16(IDS_OK));
  localized_strings->SetString("cancel", l10n_util::GetStringUTF16(IDS_CANCEL));
  localized_strings->SetString("packExtensionRootDir",
      l10n_util::GetStringUTF16(
          IDS_EXTENSION_PACK_DIALOG_ROOT_DIRECTORY_LABEL));
  localized_strings->SetString("packExtensionPrivateKey",
      l10n_util::GetStringUTF16(IDS_EXTENSION_PACK_DIALOG_PRIVATE_KEY_LABEL));
  localized_strings->SetString("packExtensionBrowseButton",
      l10n_util::GetStringUTF16(IDS_EXTENSION_PACK_DIALOG_BROWSE));
  localized_strings->SetString("packExtensionProceedAnyway",
      l10n_util::GetStringUTF16(IDS_EXTENSION_PROCEED_ANYWAY));
  localized_strings->SetString("packExtensionWarningTitle",
      l10n_util::GetStringUTF16(IDS_EXTENSION_PACK_WARNING_TITLE));
  localized_strings->SetString("packExtensionErrorTitle",
      l10n_util::GetStringUTF16(IDS_EXTENSION_PACK_ERROR_TITLE));
}

void PackExtensionHandler::RegisterMessages() {
  // Setup handlers specific to this panel.
  web_ui()->RegisterMessageCallback("pack",
      base::Bind(&PackExtensionHandler::HandlePackMessage,
                 base::Unretained(this)));
}

void PackExtensionHandler::OnPackSuccess(const FilePath& crx_file,
                                         const FilePath& pem_file) {
  ListValue arguments;
  arguments.Append(Value::CreateStringValue(
      UTF16ToUTF8(PackExtensionJob::StandardSuccessMessage(crx_file,
                                                           pem_file))));
  web_ui()->CallJavascriptFunction(
      "PackExtensionOverlay.showSuccessMessage", arguments);
}

void PackExtensionHandler::OnPackFailure(const std::string& error,
                                         ExtensionCreator::ErrorType type) {
  if (type == ExtensionCreator::kCRXExists) {
    base::StringValue error_str(error);
    base::StringValue extension_path_str(extension_path_);
    base::StringValue key_path_str(private_key_path_);
    base::FundamentalValue overwrite_flag(ExtensionCreator::kOverwriteCRX);

    web_ui()->CallJavascriptFunction(
        "ExtensionSettings.askToOverrideWarning", error_str, extension_path_str,
            key_path_str, overwrite_flag);
  } else {
    ShowAlert(error);
  }
}

void PackExtensionHandler::HandlePackMessage(const ListValue* args) {

  CHECK_EQ(3U, args->GetSize());
  CHECK(args->GetString(0, &extension_path_));
  CHECK(args->GetString(1, &private_key_path_));

  double flags_double = 0.0;
  CHECK(args->GetDouble(2, &flags_double));
  int run_flags = static_cast<int>(flags_double);

  FilePath root_directory =
      FilePath::FromWStringHack(UTF8ToWide(extension_path_));
  FilePath key_file = FilePath::FromWStringHack(UTF8ToWide(private_key_path_));

  if (root_directory.empty()) {
    if (extension_path_.empty()) {
      ShowAlert(l10n_util::GetStringUTF8(
          IDS_EXTENSION_PACK_DIALOG_ERROR_ROOT_REQUIRED));
    } else {
      ShowAlert(l10n_util::GetStringUTF8(
          IDS_EXTENSION_PACK_DIALOG_ERROR_ROOT_INVALID));
    }

    return;
  }

  if (!private_key_path_.empty() && key_file.empty()) {
    ShowAlert(l10n_util::GetStringUTF8(
        IDS_EXTENSION_PACK_DIALOG_ERROR_KEY_INVALID));
    return;
  }

  pack_job_ = new PackExtensionJob(this, root_directory, key_file, run_flags);
  pack_job_->Start();
}

void PackExtensionHandler::ShowAlert(const std::string& message) {
  ListValue arguments;
  arguments.Append(Value::CreateStringValue(message));
  web_ui()->CallJavascriptFunction("PackExtensionOverlay.showError", arguments);
}
