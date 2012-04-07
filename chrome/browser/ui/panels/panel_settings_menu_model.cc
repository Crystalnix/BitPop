// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/panels/panel_settings_menu_model.h"

#include "base/logging.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/panels/panel.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/url_constants.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

using content::OpenURLParams;
using content::Referrer;

PanelSettingsMenuModel::PanelSettingsMenuModel(Panel* panel)
    : ALLOW_THIS_IN_INITIALIZER_LIST(ui::SimpleMenuModel(this)),
      panel_(panel) {
  const Extension* extension = panel_->GetExtension();
  DCHECK(extension);

  AddItem(COMMAND_NAME, UTF8ToUTF16(extension->name()));
  AddSeparator();
  AddItem(COMMAND_CONFIGURE,
          l10n_util::GetStringUTF16(IDS_EXTENSIONS_OPTIONS_MENU_ITEM));
  AddItem(COMMAND_DISABLE, l10n_util::GetStringUTF16(IDS_EXTENSIONS_DISABLE));
  AddItem(COMMAND_UNINSTALL,
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_UNINSTALL));
  AddSeparator();
  AddItem(COMMAND_MANAGE, l10n_util::GetStringUTF16(IDS_MANAGE_EXTENSIONS));
}

PanelSettingsMenuModel::~PanelSettingsMenuModel() {
}

bool PanelSettingsMenuModel::IsCommandIdChecked(int command_id) const {
  // Nothing in the menu is checked.
  return false;
}

bool PanelSettingsMenuModel::IsCommandIdEnabled(int command_id) const {
  const Extension* extension = panel_->GetExtension();
  DCHECK(extension);

  switch (command_id) {
    case COMMAND_NAME:
      // The NAME links to the Homepage URL. If the extension doesn't have a
      // homepage, we just disable this menu item.
      return extension->GetHomepageURL().is_valid();
    case COMMAND_CONFIGURE:
      return extension->options_url().spec().length() > 0;
    case COMMAND_DISABLE:
    case COMMAND_UNINSTALL:
      // Some extension types can not be disabled or uninstalled.
      return Extension::UserMayDisable(extension->location());
    case COMMAND_MANAGE:
      return true;
    default:
      NOTREACHED();
      return false;
  }
}

bool PanelSettingsMenuModel::GetAcceleratorForCommandId(
    int command_id, ui::Accelerator* accelerator) {
  return false;
}

void PanelSettingsMenuModel::ExecuteCommand(int command_id) {
  const Extension* extension = panel_->GetExtension();
  DCHECK(extension);

  Browser* browser = panel_->browser();
  switch (command_id) {
    case COMMAND_NAME: {
      OpenURLParams params(
          extension->GetHomepageURL(), Referrer(), NEW_FOREGROUND_TAB,
          content::PAGE_TRANSITION_LINK, false);
      browser->OpenURL(params);
      break;
    }
    case COMMAND_CONFIGURE:
      DCHECK(!extension->options_url().is_empty());
      browser->GetProfile()->GetExtensionProcessManager()->OpenOptionsPage(
          extension, browser);
      break;
    case COMMAND_DISABLE:
      browser->GetProfile()->GetExtensionService()->DisableExtension(
          extension->id());
      break;
    case COMMAND_UNINSTALL:
      // When the owning panel is being closed by the extension API, the
      // currently showing uninstall dialog will also be dismissed.
      extension_uninstall_dialog_.reset(
          ExtensionUninstallDialog::Create(browser->GetProfile(), this));
      extension_uninstall_dialog_->ConfirmUninstall(extension);
      break;
    case COMMAND_MANAGE:
      browser->ShowOptionsTab(chrome::kExtensionsSubPage);
      break;
    default:
      NOTREACHED();
      break;
  }
}

void PanelSettingsMenuModel::ExtensionUninstallAccepted() {
  const Extension* extension = panel_->GetExtension();
  DCHECK(extension);

  panel_->browser()->GetProfile()->GetExtensionService()->
      UninstallExtension(extension->id(), false, NULL);
}

void PanelSettingsMenuModel::ExtensionUninstallCanceled() {
}
