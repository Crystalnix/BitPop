// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/extensions/pack_extension_handler.h"

#include "chrome/browser/extensions/extension_creator.h"
#include "chrome/browser/ui/chrome_select_file_policy.h"
#include "base/bind.h"
#include "base/utf_string_conversions.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/browser/web_ui.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

PackExtensionHandler::PackExtensionHandler() {
}

PackExtensionHandler::~PackExtensionHandler() {
  // There may be pending file dialogs, we need to tell them that we've gone
  // away so they don't try and call back to us.
  if (load_extension_dialog_)
    load_extension_dialog_->ListenerDestroyed();

  if (pack_job_.get())
    pack_job_->ClearClient();
}

void PackExtensionHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

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
  web_ui()->RegisterMessageCallback(
      "pack",
      base::Bind(&PackExtensionHandler::HandlePackMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "packExtensionSelectFilePath",
      base::Bind(&PackExtensionHandler::HandleSelectFilePathMessage,
                 base::Unretained(this)));
}

void PackExtensionHandler::OnPackSuccess(const FilePath& crx_file,
                                         const FilePath& pem_file) {
  ListValue arguments;
  arguments.Append(Value::CreateStringValue(
      UTF16ToUTF8(extensions::PackExtensionJob::StandardSuccessMessage(
          crx_file, pem_file))));
  web_ui()->CallJavascriptFunction(
      "PackExtensionOverlay.showSuccessMessage", arguments);
}

void PackExtensionHandler::OnPackFailure(
    const std::string& error,
    extensions::ExtensionCreator::ErrorType type) {
  if (type == extensions::ExtensionCreator::kCRXExists) {
    base::StringValue error_str(error);
    base::StringValue extension_path_str(extension_path_);
    base::StringValue key_path_str(private_key_path_);
    base::FundamentalValue overwrite_flag(
        extensions::ExtensionCreator::kOverwriteCRX);

    web_ui()->CallJavascriptFunction(
        "ExtensionSettings.askToOverrideWarning", error_str, extension_path_str,
            key_path_str, overwrite_flag);
  } else {
    ShowAlert(error);
  }
}

void PackExtensionHandler::FileSelected(const FilePath& path, int index,
                                        void* params) {
  ListValue results;
  results.Append(Value::CreateStringValue(path.value()));
  web_ui()->CallJavascriptFunction("window.handleFilePathSelected", results);
}

void PackExtensionHandler::MultiFilesSelected(
    const std::vector<FilePath>& files, void* params) {
  NOTREACHED();
}

void PackExtensionHandler::HandlePackMessage(const ListValue* args) {

  DCHECK_EQ(3U, args->GetSize());

  if (!args->GetString(0, &extension_path_) ||
      !args->GetString(1, &private_key_path_))
    NOTREACHED();

  double flags_double = 0.0;
  if (!args->GetDouble(2, &flags_double))
    NOTREACHED();

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

  pack_job_ = new extensions::PackExtensionJob(
      this, root_directory, key_file, run_flags);
  pack_job_->Start();
}

void PackExtensionHandler::HandleSelectFilePathMessage(
    const ListValue* args) {
  DCHECK_EQ(2U, args->GetSize());

  std::string select_type;
  if (!args->GetString(0, &select_type))
    NOTREACHED();

  std::string operation;
  if (!args->GetString(1, &operation))
    NOTREACHED();

  ui::SelectFileDialog::Type type = ui::SelectFileDialog::SELECT_FOLDER;
  ui::SelectFileDialog::FileTypeInfo info;
  int file_type_index = 0;
  if (select_type == "file")
    type = ui::SelectFileDialog::SELECT_OPEN_FILE;

  string16 select_title;
  if (operation == "load") {
    select_title = l10n_util::GetStringUTF16(IDS_EXTENSION_LOAD_FROM_DIRECTORY);
  } else if (operation == "pem") {
    select_title = l10n_util::GetStringUTF16(
        IDS_EXTENSION_PACK_DIALOG_SELECT_KEY);
    info.extensions.push_back(std::vector<FilePath::StringType>());
        info.extensions.front().push_back(FILE_PATH_LITERAL("pem"));
        info.extension_description_overrides.push_back(
            l10n_util::GetStringUTF16(
                IDS_EXTENSION_PACK_DIALOG_KEY_FILE_TYPE_DESCRIPTION));
        info.include_all_files = true;
    file_type_index = 1;
  } else {
    NOTREACHED();
  }

  load_extension_dialog_ = ui::SelectFileDialog::Create(
      this, new ChromeSelectFilePolicy(web_ui()->GetWebContents()));
  load_extension_dialog_->SelectFile(
      type, select_title, FilePath(), &info, file_type_index,
      FILE_PATH_LITERAL(""),
      web_ui()->GetWebContents()->GetView()->GetTopLevelNativeWindow(),
      NULL);
}

void PackExtensionHandler::ShowAlert(const std::string& message) {
  ListValue arguments;
  arguments.Append(Value::CreateStringValue(message));
  web_ui()->CallJavascriptFunction("PackExtensionOverlay.showError", arguments);
}
