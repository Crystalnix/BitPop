// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_BROWSERTEST_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_BROWSERTEST_H_

#include <string>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/feature_switch.h"
#include "chrome/common/extensions/features/feature.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents.h"

class ExtensionService;
class ExtensionProcessManager;
class Profile;

// Base class for extension browser tests. Provides utilities for loading,
// unloading, and installing extensions.
class ExtensionBrowserTest : virtual public InProcessBrowserTest,
                             public content::NotificationObserver {
 protected:
  // Flags used to configure how the tests are run.
  enum Flags {
    kFlagNone = 0,

    // Allow the extension to run in incognito mode.
    kFlagEnableIncognito = 1 << 0,

    // Allow file access for the extension.
    kFlagEnableFileAccess = 1 << 1,

    // Don't fail when the loaded manifest has warnings (should only be used
    // when testing deprecated features).
    kFlagIgnoreManifestWarnings = 1 << 2,

    // Allow older manifest versions (typically these can't be loaded - we allow
    // them for testing).
    kFlagAllowOldManifestVersions = 1 << 3,
  };

  ExtensionBrowserTest();
  virtual ~ExtensionBrowserTest();

  // Useful accessors.
  Profile* profile() { return browser()->profile(); }
  ExtensionService* extension_service() {
    return extensions::ExtensionSystem::Get(profile())->extension_service();
  }

  // InProcessBrowserTest
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE;

  const extensions::Extension* LoadExtension(const FilePath& path);

  // Same as above, but enables the extension in incognito mode first.
  const extensions::Extension* LoadExtensionIncognito(const FilePath& path);

  const extensions::Extension* LoadExtensionWithFlags(
      const FilePath& path, int flags);

  // Loads extension and imitates that it is a component extension.
  const extensions::Extension* LoadExtensionAsComponent(const FilePath& path);

  // Pack the extension in |dir_path| into a crx file and return its path.
  // Return an empty FilePath if there were errors.
  FilePath PackExtension(const FilePath& dir_path);

  // Pack the extension in |dir_path| into a crx file at |crx_path|, using the
  // key |pem_path|. If |pem_path| does not exist, create a new key at
  // |pem_out_path|.
  // Return the path to the crx file, or an empty FilePath if there were errors.
  FilePath PackExtensionWithOptions(const FilePath& dir_path,
                                    const FilePath& crx_path,
                                    const FilePath& pem_path,
                                    const FilePath& pem_out_path);

  // |expected_change| indicates how many extensions should be installed (or
  // disabled, if negative).
  // 1 means you expect a new install, 0 means you expect an upgrade, -1 means
  // you expect a failed upgrade.
  const extensions::Extension* InstallExtension(const FilePath& path,
                                                int expected_change) {
    return InstallOrUpdateExtension("", path, INSTALL_UI_TYPE_NONE,
                                    expected_change);
  }

  // Same as above, but an install source other than Extension::INTERNAL can be
  // specified.
  const extensions::Extension* InstallExtension(
      const FilePath& path,
      int expected_change,
      extensions::Extension::Location install_source) {
    return InstallOrUpdateExtension("", path, INSTALL_UI_TYPE_NONE,
                                    expected_change, install_source);
  }

  // Installs extension as if it came from the Chrome Webstore.
  const extensions::Extension* InstallExtensionFromWebstore(
      const FilePath& path, int expected_change);

  // Same as above but passes an id to CrxInstaller and does not allow a
  // privilege increase.
  const extensions::Extension* UpdateExtension(const std::string& id,
                                               const FilePath& path,
                                               int expected_change) {
    return InstallOrUpdateExtension(id, path, INSTALL_UI_TYPE_NONE,
                                    expected_change);
  }

  // Same as |InstallExtension| but with the normal extension UI showing up
  // (for e.g. info bar on success).
  const extensions::Extension* InstallExtensionWithUI(const FilePath& path,
                                                      int expected_change) {
    return InstallOrUpdateExtension("", path, INSTALL_UI_TYPE_NORMAL,
                                    expected_change);
  }

  const extensions::Extension* InstallExtensionWithUIAutoConfirm(
      const FilePath& path,
      int expected_change,
      Browser* browser) {
    return InstallOrUpdateExtension("", path, INSTALL_UI_TYPE_AUTO_CONFIRM,
                                    expected_change, browser, false);
  }

  // Begins install process but simulates a user cancel.
  const extensions::Extension* StartInstallButCancel(const FilePath& path) {
    return InstallOrUpdateExtension("", path, INSTALL_UI_TYPE_CANCEL, 0);
  }

  void ReloadExtension(const std::string& extension_id);

  void UnloadExtension(const std::string& extension_id);

  void UninstallExtension(const std::string& extension_id);

  void DisableExtension(const std::string& extension_id);

  void EnableExtension(const std::string& extension_id);

  // Wait for the total number of page actions to change to |count|.
  bool WaitForPageActionCountChangeTo(int count);

  // Wait for the number of visible page actions to change to |count|.
  bool WaitForPageActionVisibilityChangeTo(int count);

  // Waits until an extension is installed and loaded. Returns true if an
  // install happened before timeout.
  bool WaitForExtensionInstall();

  // Wait for an extension install error to be raised. Returns true if an
  // error was raised.
  bool WaitForExtensionInstallError();

  // Waits until an extension is loaded.
  void WaitForExtensionLoad();

  // Waits for an extension load error. Returns true if the error really
  // happened.
  bool WaitForExtensionLoadError();

  // Wait for the specified extension to crash. Returns true if it really
  // crashed.
  bool WaitForExtensionCrash(const std::string& extension_id);

  // Wait for the crx installer to be done. Returns true if it really is done.
  bool WaitForCrxInstallerDone();

  // Simulates a page calling window.open on an URL and waits for the
  // navigation.
  void OpenWindow(content::WebContents* contents,
                  const GURL& url,
                  bool newtab_process_should_equal_opener,
                  content::WebContents** newtab_result);

  // Simulates a page navigating itself to an URL and waits for the
  // navigation.
  void NavigateInRenderer(content::WebContents* contents, const GURL& url);

  // Looks for an ExtensionHost whose URL has the given path component
  // (including leading slash).  Also verifies that the expected number of hosts
  // are loaded.
  extensions::ExtensionHost* FindHostWithPath(ExtensionProcessManager* manager,
                                              const std::string& path,
                                              int expected_hosts);

  // content::NotificationObserver
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  bool loaded_;
  bool installed_;

  // test_data/extensions.
  FilePath test_data_dir_;
  std::string last_loaded_extension_id_;
  int extension_installs_observed_;
  int extension_load_errors_observed_;
  int crx_installers_done_observed_;

 private:
  // Temporary directory for testing.
  base::ScopedTempDir temp_dir_;

  // Specifies the type of UI (if any) to show during installation and what
  // user action to simulate.
  enum InstallUIType {
    INSTALL_UI_TYPE_NONE,
    INSTALL_UI_TYPE_CANCEL,
    INSTALL_UI_TYPE_NORMAL,
    INSTALL_UI_TYPE_AUTO_CONFIRM,
  };

  const extensions::Extension* InstallOrUpdateExtension(const std::string& id,
                                                        const FilePath& path,
                                                        InstallUIType ui_type,
                                                        int expected_change);
  const extensions::Extension* InstallOrUpdateExtension(const std::string& id,
                                                        const FilePath& path,
                                                        InstallUIType ui_type,
                                                        int expected_change,
                                                        Browser* browser,
                                                        bool from_webstore);
  const extensions::Extension* InstallOrUpdateExtension(
      const std::string& id,
      const FilePath& path,
      InstallUIType ui_type,
      int expected_change,
      extensions::Extension::Location install_source);
  const extensions::Extension* InstallOrUpdateExtension(
      const std::string& id,
      const FilePath& path,
      InstallUIType ui_type,
      int expected_change,
      extensions::Extension::Location install_source,
      Browser* browser,
      bool from_webstore);

  bool WaitForExtensionViewsToLoad();

  // When waiting for page action count to change, we wait until it reaches this
  // value.
  int target_page_action_count_;

  // When waiting for visible page action count to change, we wait until it
  // reaches this value.
  int target_visible_page_action_count_;

  // Make the current channel "dev" for the duration of the test.
  extensions::Feature::ScopedCurrentChannel current_channel_;

  // Disable external install UI.
  extensions::FeatureSwitch::ScopedOverride
      override_prompt_for_external_extensions_;

  // Disable the sideload wipeout UI.
  extensions::FeatureSwitch::ScopedOverride
      override_sideload_wipeout_;
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_BROWSERTEST_H_
