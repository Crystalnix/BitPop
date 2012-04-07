// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_disabled_infobar_delegate.h"

#include <string>

#include "base/compiler_specific.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_install_ui.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "chrome/browser/tab_contents/confirm_infobar_delegate.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/extension_file_util.h"
#include "chrome/common/extensions/extension_resource.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/web_contents.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

// ExtensionDisabledDialogDelegate --------------------------------------------

class ExtensionDisabledDialogDelegate
    : public ExtensionInstallUI::Delegate,
      public base::RefCountedThreadSafe<ExtensionDisabledDialogDelegate> {
 public:
  ExtensionDisabledDialogDelegate(Profile* profile,
                                  ExtensionService* service,
                                  const Extension* extension);

 private:
  friend class base::RefCountedThreadSafe<ExtensionDisabledDialogDelegate>;

  virtual ~ExtensionDisabledDialogDelegate();

  // ExtensionInstallUI::Delegate:
  virtual void InstallUIProceed() OVERRIDE;
  virtual void InstallUIAbort(bool user_initiated) OVERRIDE;

  // The UI for showing the install dialog when enabling.
  scoped_ptr<ExtensionInstallUI> install_ui_;

  ExtensionService* service_;
  const Extension* extension_;
};

ExtensionDisabledDialogDelegate::ExtensionDisabledDialogDelegate(
    Profile* profile,
    ExtensionService* service,
    const Extension* extension)
    : service_(service), extension_(extension) {
  AddRef();  // Balanced in Proceed or Abort.

  install_ui_.reset(new ExtensionInstallUI(profile));
  install_ui_->ConfirmReEnable(this, extension_);
}

ExtensionDisabledDialogDelegate::~ExtensionDisabledDialogDelegate() {
}

void ExtensionDisabledDialogDelegate::InstallUIProceed() {
  service_->GrantPermissionsAndEnableExtension(extension_);
  Release();
}

void ExtensionDisabledDialogDelegate::InstallUIAbort(bool user_initiated) {
  std::string histogram_name = user_initiated ?
      "Extensions.Permissions_ReEnableCancel" :
      "Extensions.Permissions_ReEnableAbort";
  ExtensionService::RecordPermissionMessagesHistogram(
      extension_, histogram_name.c_str());

  // Do nothing. The extension will remain disabled.
  Release();
}


// ExtensionDisabledInfobarDelegate -------------------------------------------

class ExtensionDisabledInfobarDelegate : public ConfirmInfoBarDelegate,
                                         public content::NotificationObserver {
 public:
  ExtensionDisabledInfobarDelegate(InfoBarTabHelper* infobar_helper,
                                   ExtensionService* service,
                                   const Extension* extension);

 private:
  virtual ~ExtensionDisabledInfobarDelegate();

  // ConfirmInfoBarDelegate:
  virtual string16 GetMessageText() const OVERRIDE;
  virtual int GetButtons() const OVERRIDE;
  virtual string16 GetButtonLabel(InfoBarButton button) const OVERRIDE;
  virtual bool Accept() OVERRIDE;

  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  content::NotificationRegistrar registrar_;
  ExtensionService* service_;
  const Extension* extension_;
};

ExtensionDisabledInfobarDelegate::ExtensionDisabledInfobarDelegate(
    InfoBarTabHelper* infobar_helper,
    ExtensionService* service,
    const Extension* extension)
    : ConfirmInfoBarDelegate(infobar_helper),
      service_(service),
      extension_(extension) {
  // The user might re-enable the extension in other ways, so watch for that.
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_LOADED,
                 content::Source<Profile>(service->profile()));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_UNLOADED,
                 content::Source<Profile>(service->profile()));
}

ExtensionDisabledInfobarDelegate::~ExtensionDisabledInfobarDelegate() {
}

string16 ExtensionDisabledInfobarDelegate::GetMessageText() const {
  return l10n_util::GetStringFUTF16(extension_->is_app() ?
      IDS_APP_DISABLED_INFOBAR_LABEL : IDS_EXTENSION_DISABLED_INFOBAR_LABEL,
      UTF8ToUTF16(extension_->name()));
}

int ExtensionDisabledInfobarDelegate::GetButtons() const {
  return BUTTON_OK;
}

string16 ExtensionDisabledInfobarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  DCHECK_EQ(BUTTON_OK, button);
  return l10n_util::GetStringUTF16(
      IDS_EXTENSION_DISABLED_INFOBAR_ENABLE_BUTTON);
}

bool ExtensionDisabledInfobarDelegate::Accept() {
  // This object manages its own lifetime.
  new ExtensionDisabledDialogDelegate(service_->profile(), service_,
                                      extension_);
  return true;
}

void ExtensionDisabledInfobarDelegate::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  // TODO(mpcomplete): RemoveInfoBar doesn't seem to always result in us getting
  // deleted.
  const Extension* extension = NULL;
  if (type == chrome::NOTIFICATION_EXTENSION_LOADED) {
    extension = content::Details<const Extension>(details).ptr();
  } else {
    DCHECK_EQ(chrome::NOTIFICATION_EXTENSION_UNLOADED, type);
    UnloadedExtensionInfo* info =
        content::Details<UnloadedExtensionInfo>(details).ptr();
    extension = info->extension;
  }
  if (extension == extension_)
    RemoveSelf();
}


// Globals --------------------------------------------------------------------

void ShowExtensionDisabledUI(ExtensionService* service,
                             Profile* profile,
                             const Extension* extension) {
  Browser* browser = BrowserList::GetLastActiveWithProfile(profile);
  if (!browser)
    return;

  TabContentsWrapper* tab_contents = browser->GetSelectedTabContentsWrapper();
  if (!tab_contents)
    return;

  InfoBarTabHelper* infobar_helper = tab_contents->infobar_tab_helper();
  infobar_helper->AddInfoBar(
      new ExtensionDisabledInfobarDelegate(infobar_helper, service, extension));
}

void ShowExtensionDisabledDialog(ExtensionService* service, Profile* profile,
                                 const Extension* extension) {
  // This object manages its own lifetime.
  new ExtensionDisabledDialogDelegate(profile, service, extension);
}
