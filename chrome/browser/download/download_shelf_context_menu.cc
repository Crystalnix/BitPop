// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_shelf_context_menu.h"

#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/page_navigator.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

using content::DownloadItem;
using content::OpenURLParams;

DownloadShelfContextMenu::~DownloadShelfContextMenu() {}

DownloadShelfContextMenu::DownloadShelfContextMenu(
    BaseDownloadItemModel* download_model)
    : download_model_(download_model),
      download_item_(download_model->download()) {
}

ui::SimpleMenuModel* DownloadShelfContextMenu::GetMenuModel() {
  ui::SimpleMenuModel* model = NULL;

  if (download_item_->GetSafetyState() == DownloadItem::DANGEROUS) {
    if (download_item_->GetDangerType() ==
            content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL ||
        download_item_->GetDangerType() ==
            content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT) {
      model = GetMaliciousMenuModel();
    } else {
      NOTREACHED();
    }
  } else if (download_item_->IsComplete()) {
    model = GetFinishedMenuModel();
  } else {
    model = GetInProgressMenuModel();
  }
  return model;
}

bool DownloadShelfContextMenu::IsCommandIdEnabled(int command_id) const {
  switch (command_id) {
    case SHOW_IN_FOLDER:
    case OPEN_WHEN_COMPLETE:
      return download_item_->CanShowInFolder();
    case ALWAYS_OPEN_TYPE:
      return download_item_->CanOpenDownload() &&
          !Extension::IsExtension(download_item_->GetTargetName());
    case CANCEL:
      return download_item_->IsPartialDownload();
    case TOGGLE_PAUSE:
      return download_item_->IsInProgress();
    default:
      return command_id > 0 && command_id < MENU_LAST;
  }
}

bool DownloadShelfContextMenu::IsCommandIdChecked(int command_id) const {
  switch (command_id) {
    case OPEN_WHEN_COMPLETE:
      return download_item_->GetOpenWhenComplete();
    case ALWAYS_OPEN_TYPE:
      return download_item_->ShouldOpenFileBasedOnExtension();
    case TOGGLE_PAUSE:
      return download_item_->IsPaused();
  }
  return false;
}

void DownloadShelfContextMenu::ExecuteCommand(int command_id) {
  switch (command_id) {
    case SHOW_IN_FOLDER:
      download_item_->ShowDownloadInShell();
      break;
    case OPEN_WHEN_COMPLETE:
      download_item_->OpenDownload();
      break;
    case ALWAYS_OPEN_TYPE: {
      DownloadPrefs* prefs = DownloadPrefs::FromBrowserContext(
          download_item_->GetBrowserContext());
      FilePath path = download_item_->GetUserVerifiedFilePath();
      if (!IsCommandIdChecked(ALWAYS_OPEN_TYPE))
        prefs->EnableAutoOpenBasedOnExtension(path);
      else
        prefs->DisableAutoOpenBasedOnExtension(path);
      break;
    }
    case CANCEL:
      download_model_->CancelTask();
      break;
    case TOGGLE_PAUSE:
      // It is possible for the download to complete before the user clicks the
      // menu item, recheck if the download is in progress state before toggling
      // pause.
      if (download_item_->IsPartialDownload())
        download_item_->TogglePause();
      break;
    case DISCARD:
      download_item_->Delete(DownloadItem::DELETE_DUE_TO_USER_DISCARD);
      break;
    case KEEP:
      download_item_->DangerousDownloadValidated();
      break;
    case LEARN_MORE: {
      Browser* browser = BrowserList::GetLastActive();
      DCHECK(browser && browser->is_type_tabbed());
      OpenURLParams params(GURL(chrome::kDownloadScanningLearnMoreURL),
                           content::Referrer(), NEW_FOREGROUND_TAB,
                           content::PAGE_TRANSITION_TYPED, false);
      browser->OpenURL(params);
      break;
    }
    default:
      NOTREACHED();
  }
}

bool DownloadShelfContextMenu::GetAcceleratorForCommandId(
    int command_id, ui::Accelerator* accelerator) {
  return false;
}

bool DownloadShelfContextMenu::IsItemForCommandIdDynamic(int command_id) const {
  return command_id == TOGGLE_PAUSE;
}

string16 DownloadShelfContextMenu::GetLabelForCommandId(int command_id) const {
  switch (command_id) {
    case SHOW_IN_FOLDER:
      return l10n_util::GetStringUTF16(IDS_DOWNLOAD_MENU_SHOW);
    case OPEN_WHEN_COMPLETE:
      if (download_item_->IsInProgress())
        return l10n_util::GetStringUTF16(IDS_DOWNLOAD_MENU_OPEN_WHEN_COMPLETE);
      return l10n_util::GetStringUTF16(IDS_DOWNLOAD_MENU_OPEN);
    case ALWAYS_OPEN_TYPE:
      return l10n_util::GetStringUTF16(IDS_DOWNLOAD_MENU_ALWAYS_OPEN_TYPE);
    case CANCEL:
      return l10n_util::GetStringUTF16(IDS_DOWNLOAD_MENU_CANCEL);
    case TOGGLE_PAUSE: {
      if (download_item_->IsPaused())
        return l10n_util::GetStringUTF16(IDS_DOWNLOAD_MENU_RESUME_ITEM);
      return l10n_util::GetStringUTF16(IDS_DOWNLOAD_MENU_PAUSE_ITEM);
    }
    case DISCARD:
      return l10n_util::GetStringUTF16(IDS_DOWNLOAD_MENU_DISCARD);
    case KEEP:
      return l10n_util::GetStringUTF16(IDS_DOWNLOAD_MENU_KEEP);
    case LEARN_MORE:
      return l10n_util::GetStringUTF16(IDS_DOWNLOAD_MENU_LEARN_MORE);
    default:
      NOTREACHED();
      break;
  }
  return string16();
}

ui::SimpleMenuModel* DownloadShelfContextMenu::GetInProgressMenuModel() {
  if (in_progress_download_menu_model_.get())
    return in_progress_download_menu_model_.get();

  in_progress_download_menu_model_.reset(new ui::SimpleMenuModel(this));

  in_progress_download_menu_model_->AddCheckItemWithStringId(
      OPEN_WHEN_COMPLETE, IDS_DOWNLOAD_MENU_OPEN_WHEN_COMPLETE);
  in_progress_download_menu_model_->AddCheckItemWithStringId(
      ALWAYS_OPEN_TYPE, IDS_DOWNLOAD_MENU_ALWAYS_OPEN_TYPE);
  in_progress_download_menu_model_->AddSeparator();
  in_progress_download_menu_model_->AddItemWithStringId(
      TOGGLE_PAUSE, IDS_DOWNLOAD_MENU_PAUSE_ITEM);
  in_progress_download_menu_model_->AddItemWithStringId(
      SHOW_IN_FOLDER, IDS_DOWNLOAD_MENU_SHOW);
  in_progress_download_menu_model_->AddSeparator();
  in_progress_download_menu_model_->AddItemWithStringId(
      CANCEL, IDS_DOWNLOAD_MENU_CANCEL);

  return in_progress_download_menu_model_.get();
}

ui::SimpleMenuModel* DownloadShelfContextMenu::GetFinishedMenuModel() {
  if (finished_download_menu_model_.get())
    return finished_download_menu_model_.get();

  finished_download_menu_model_.reset(new ui::SimpleMenuModel(this));

  finished_download_menu_model_->AddItemWithStringId(
      OPEN_WHEN_COMPLETE, IDS_DOWNLOAD_MENU_OPEN);
  finished_download_menu_model_->AddCheckItemWithStringId(
      ALWAYS_OPEN_TYPE, IDS_DOWNLOAD_MENU_ALWAYS_OPEN_TYPE);
  finished_download_menu_model_->AddSeparator();
  finished_download_menu_model_->AddItemWithStringId(
      SHOW_IN_FOLDER, IDS_DOWNLOAD_MENU_SHOW);
  finished_download_menu_model_->AddSeparator();
  finished_download_menu_model_->AddItemWithStringId(
      CANCEL, IDS_DOWNLOAD_MENU_CANCEL);

  return finished_download_menu_model_.get();
}

ui::SimpleMenuModel* DownloadShelfContextMenu::GetMaliciousMenuModel() {
  if (malicious_download_menu_model_.get())
    return malicious_download_menu_model_.get();

  malicious_download_menu_model_.reset(new ui::SimpleMenuModel(this));

  malicious_download_menu_model_->AddItemWithStringId(
      DISCARD, IDS_DOWNLOAD_MENU_DISCARD);
  malicious_download_menu_model_->AddItemWithStringId(
      KEEP, IDS_DOWNLOAD_MENU_KEEP);
  malicious_download_menu_model_->AddSeparator();
  malicious_download_menu_model_->AddItemWithStringId(
      LEARN_MORE, IDS_DOWNLOAD_MENU_LEARN_MORE);

  return malicious_download_menu_model_.get();
}
