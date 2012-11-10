// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/unpacked_installer.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/file_util.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
#include "chrome/browser/extensions/extension_install_ui.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/permissions_updater.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_file_util.h"
#include "chrome/common/string_ordinal.h"

using content::BrowserThread;
using extensions::Extension;

namespace {

const char kUnpackedExtensionsBlacklistedError[] =
    "Loading of unpacked extensions is disabled by the administrator.";

// Manages an ExtensionInstallPrompt for a particular extension.
class SimpleExtensionLoadPrompt : public ExtensionInstallPrompt::Delegate {
 public:
  SimpleExtensionLoadPrompt(Profile* profile,
                            base::WeakPtr<ExtensionService> extension_service,
                            const Extension* extension);
  ~SimpleExtensionLoadPrompt();

  void ShowPrompt();

  // ExtensionInstallUI::Delegate
  virtual void InstallUIProceed() OVERRIDE;
  virtual void InstallUIAbort(bool user_initiated) OVERRIDE;

 private:
  base::WeakPtr<ExtensionService> service_weak_;
  scoped_ptr<ExtensionInstallPrompt> install_ui_;
  scoped_refptr<const Extension> extension_;
};

SimpleExtensionLoadPrompt::SimpleExtensionLoadPrompt(
    Profile* profile,
    base::WeakPtr<ExtensionService> extension_service,
    const Extension* extension)
    : service_weak_(extension_service),
      extension_(extension) {
  install_ui_.reset(
      ExtensionInstallUI::CreateInstallPromptWithProfile(profile));
}

SimpleExtensionLoadPrompt::~SimpleExtensionLoadPrompt() {
}

void SimpleExtensionLoadPrompt::ShowPrompt() {
  install_ui_->ConfirmInstall(this, extension_);
}

void SimpleExtensionLoadPrompt::InstallUIProceed() {
  if (service_weak_.get()) {
    extensions::PermissionsUpdater perms_updater(service_weak_->profile());
    perms_updater.GrantActivePermissions(extension_, false);
    service_weak_->OnExtensionInstalled(
        extension_, false, StringOrdinal());  // Not from web store.
  }
  delete this;
}

void SimpleExtensionLoadPrompt::InstallUIAbort(bool user_initiated) {
  delete this;
}

}  // namespace

namespace extensions {

// static
scoped_refptr<UnpackedInstaller> UnpackedInstaller::Create(
    ExtensionService* extension_service) {
  return scoped_refptr<UnpackedInstaller>(
      new UnpackedInstaller(extension_service));
}

UnpackedInstaller::UnpackedInstaller(ExtensionService* extension_service)
    : service_weak_(extension_service->AsWeakPtr()),
      prompt_for_plugins_(true) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

UnpackedInstaller::~UnpackedInstaller() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
        BrowserThread::CurrentlyOn(BrowserThread::FILE));
}

void UnpackedInstaller::Load(const FilePath& path_in) {
  extension_path_ = path_in;
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
      base::Bind(&UnpackedInstaller::GetAbsolutePath, this));
}

void UnpackedInstaller::LoadFromCommandLine(const FilePath& path_in) {
  if (!service_weak_.get())
    return;
  // Load extensions from the command line synchronously to avoid a race
  // between extension loading and loading an URL from the command line.
  base::ThreadRestrictions::ScopedAllowIO allow_io;

  extension_path_ = path_in;
  file_util::AbsolutePath(&extension_path_);

  if (!IsLoadingUnpackedAllowed()) {
    ReportExtensionLoadError(kUnpackedExtensionsBlacklistedError);
    return;
  }

  std::string id = Extension::GenerateIdForPath(extension_path_);
  bool allow_file_access =
      Extension::ShouldAlwaysAllowFileAccess(Extension::LOAD);
  if (service_weak_->extension_prefs()->HasAllowFileAccessSetting(id))
    allow_file_access = service_weak_->extension_prefs()->AllowFileAccess(id);

  int flags = Extension::REQUIRE_MODERN_MANIFEST_VERSION;
  if (allow_file_access)
    flags |= Extension::ALLOW_FILE_ACCESS;

  std::string error;
  scoped_refptr<const Extension> extension(extension_file_util::LoadExtension(
      extension_path_,
      Extension::LOAD,
      flags | Extension::FOLLOW_SYMLINKS_ANYWHERE,
      &error));

  if (!extension) {
    ReportExtensionLoadError(error);
    return;
  }

  OnLoaded(extension);
}

bool UnpackedInstaller::IsLoadingUnpackedAllowed() const {
  if (!service_weak_)
    return true;
  // If there is a "*" in the extension blacklist, then no extensions should be
  // allowed at all (except explicitly whitelisted extensions).
  return !service_weak_->extension_prefs()->ExtensionsBlacklistedByDefault();
}

void UnpackedInstaller::GetAbsolutePath() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  file_util::AbsolutePath(&extension_path_);

  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
      base::Bind(&UnpackedInstaller::CheckExtensionFileAccess, this));
}

void UnpackedInstaller::CheckExtensionFileAccess() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!service_weak_)
    return;

  if (!IsLoadingUnpackedAllowed()) {
    ReportExtensionLoadError(kUnpackedExtensionsBlacklistedError);
    return;
  }

  std::string id = Extension::GenerateIdForPath(extension_path_);
  // Unpacked extensions default to allowing file access, but if that has been
  // overridden, don't reset the value.
  bool allow_file_access =
      Extension::ShouldAlwaysAllowFileAccess(Extension::LOAD);
  if (service_weak_->extension_prefs()->HasAllowFileAccessSetting(id))
    allow_file_access = service_weak_->extension_prefs()->AllowFileAccess(id);

  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
      base::Bind(
          &UnpackedInstaller::LoadWithFileAccess,
          this, allow_file_access));
}

void UnpackedInstaller::LoadWithFileAccess(bool allow_file_access) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  int flags = Extension::REQUIRE_MODERN_MANIFEST_VERSION;
  if (allow_file_access)
    flags |= Extension::ALLOW_FILE_ACCESS;
  std::string error;
  scoped_refptr<const Extension> extension(extension_file_util::LoadExtension(
      extension_path_,
      Extension::LOAD,
      flags | Extension::FOLLOW_SYMLINKS_ANYWHERE,
      &error));

  if (!extension) {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
        base::Bind(
            &UnpackedInstaller::ReportExtensionLoadError,
            this, error));
    return;
  }

  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
      base::Bind(
          &UnpackedInstaller::OnLoaded,
          this, extension));
}

void UnpackedInstaller::ReportExtensionLoadError(const std::string &error) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!service_weak_.get())
    return;
  service_weak_->ReportExtensionLoadError(extension_path_, error, true);
}

void UnpackedInstaller::OnLoaded(
    const scoped_refptr<const Extension>& extension) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!service_weak_.get())
    return;
  const ExtensionSet* disabled_extensions =
      service_weak_->disabled_extensions();
  if (service_weak_->show_extensions_prompts() &&
      prompt_for_plugins_ &&
      !extension->plugins().empty() &&
      !disabled_extensions->Contains(extension->id())) {
    SimpleExtensionLoadPrompt* prompt = new SimpleExtensionLoadPrompt(
        service_weak_->profile(),
        service_weak_,
        extension);
    prompt->ShowPrompt();
    return;  // continues in SimpleExtensionLoadPrompt::InstallPrompt*
  }

  PermissionsUpdater perms_updater(service_weak_->profile());
  perms_updater.GrantActivePermissions(extension, false);
  service_weak_->OnExtensionInstalled(extension,
                                      false,  // Not from web store.
                                      StringOrdinal());
}

}  // namespace extensions
