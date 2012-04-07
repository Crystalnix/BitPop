// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file defines a class that contains various method related to branding.
// It provides only default implementations of these methods. Usually to add
// specific branding, we will need to extend this class with a custom
// implementation.

#include "chrome/installer/util/browser_distribution.h"

#include "base/atomicops.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/path_service.h"
#include "base/logging.h"
#include "base/win/registry.h"
#include "chrome/common/env_vars.h"
#include "chrome/installer/util/chrome_frame_distribution.h"
#include "chrome/installer/util/chromium_binaries_distribution.h"
#include "chrome/installer/util/google_chrome_distribution.h"
#include "chrome/installer/util/google_chrome_binaries_distribution.h"
#include "chrome/installer/util/google_chrome_sxs_distribution.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/l10n_string_util.h"
#include "chrome/installer/util/master_preferences.h"

#include "installer_util_strings.h"  // NOLINT

using installer::MasterPreferences;

namespace {
// The BrowserDistribution objects are never freed.
BrowserDistribution* g_browser_distribution = NULL;
BrowserDistribution* g_chrome_frame_distribution = NULL;
BrowserDistribution* g_binaries_distribution = NULL;

// Returns true if currently running in npchrome_frame.dll
bool IsChromeFrameModule() {
  FilePath module_path;
  PathService::Get(base::FILE_MODULE, &module_path);
  return FilePath::CompareEqualIgnoreCase(module_path.BaseName().value(),
                                          installer::kChromeFrameDll);
}

BrowserDistribution::Type GetCurrentDistributionType() {
  static BrowserDistribution::Type type =
      (MasterPreferences::ForCurrentProcess().install_chrome_frame() ||
       IsChromeFrameModule()) ?
          BrowserDistribution::CHROME_FRAME :
          BrowserDistribution::CHROME_BROWSER;
  return type;
}

}  // end namespace

// CHROME_BINARIES represents the binaries shared by multi-install products and
// is not a product in and of itself, so it is not present in this collection.
const BrowserDistribution::Type BrowserDistribution::kProductTypes[] = {
  BrowserDistribution::CHROME_BROWSER,
  BrowserDistribution::CHROME_FRAME,
};

const size_t BrowserDistribution::kNumProductTypes =
    arraysize(BrowserDistribution::kProductTypes);

BrowserDistribution::BrowserDistribution()
    : type_(CHROME_BROWSER) {
}

BrowserDistribution::BrowserDistribution(Type type)
    : type_(type) {
}

template<class DistributionClass>
BrowserDistribution* BrowserDistribution::GetOrCreateBrowserDistribution(
    BrowserDistribution** dist) {
  if (!*dist) {
    DistributionClass* temp = new DistributionClass();
    if (base::subtle::NoBarrier_CompareAndSwap(
            reinterpret_cast<base::subtle::AtomicWord*>(dist), NULL,
            reinterpret_cast<base::subtle::AtomicWord>(temp)) != NULL)
      delete temp;
  }

  return *dist;
}

BrowserDistribution* BrowserDistribution::GetDistribution() {
  return GetSpecificDistribution(GetCurrentDistributionType());
}

// static
BrowserDistribution* BrowserDistribution::GetSpecificDistribution(
    BrowserDistribution::Type type) {
  BrowserDistribution* dist = NULL;

  switch (type) {
    case CHROME_BROWSER:
#if defined(GOOGLE_CHROME_BUILD)
      if (InstallUtil::IsChromeSxSProcess()) {
        dist = GetOrCreateBrowserDistribution<GoogleChromeSxSDistribution>(
            &g_browser_distribution);
      } else {
        dist = GetOrCreateBrowserDistribution<GoogleChromeDistribution>(
            &g_browser_distribution);
      }
#else
      dist = GetOrCreateBrowserDistribution<BrowserDistribution>(
          &g_browser_distribution);
#endif
      break;

    case CHROME_FRAME:
      dist = GetOrCreateBrowserDistribution<ChromeFrameDistribution>(
          &g_chrome_frame_distribution);
      break;

    default:
      DCHECK_EQ(CHROME_BINARIES, type);
#if defined(GOOGLE_CHROME_BUILD)
      dist = GetOrCreateBrowserDistribution<GoogleChromeBinariesDistribution>(
          &g_binaries_distribution);
#else
      dist = GetOrCreateBrowserDistribution<ChromiumBinariesDistribution>(
          &g_binaries_distribution);
#endif
  }

  return dist;
}

void BrowserDistribution::DoPostUninstallOperations(
    const Version& version, const FilePath& local_data_path,
    const std::wstring& distribution_data) {
}

std::wstring BrowserDistribution::GetAppGuid() {
  return L"";
}

std::wstring BrowserDistribution::GetApplicationName() {
  return L"Chromium";
}

std::wstring BrowserDistribution::GetAppShortCutName() {
  return GetApplicationName();
}

std::wstring BrowserDistribution::GetAlternateApplicationName() {
  return L"The Internet";
}

std::wstring BrowserDistribution::GetBrowserAppId() {
  return L"Chromium";
}

std::wstring BrowserDistribution::GetInstallSubDir() {
  return L"Chromium";
}

std::wstring BrowserDistribution::GetPublisherName() {
  return L"Chromium";
}

std::wstring BrowserDistribution::GetAppDescription() {
  return L"Browse the web";
}

std::wstring BrowserDistribution::GetLongAppDescription() {
  const std::wstring& app_description =
      installer::GetLocalizedString(IDS_PRODUCT_DESCRIPTION_BASE);
  return app_description;
}

std::string BrowserDistribution::GetSafeBrowsingName() {
  return "chromium";
}

std::wstring BrowserDistribution::GetStateKey() {
  return L"Software\\Chromium";
}

std::wstring BrowserDistribution::GetStateMediumKey() {
  return L"Software\\Chromium";
}

std::wstring BrowserDistribution::GetStatsServerURL() {
  return L"";
}

std::string BrowserDistribution::GetNetworkStatsServer() const {
  return "";
}

std::wstring BrowserDistribution::GetDistributionData(HKEY root_key) {
  return L"";
}

std::wstring BrowserDistribution::GetUninstallLinkName() {
  return L"Uninstall Chromium";
}

std::wstring BrowserDistribution::GetUninstallRegPath() {
  return L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Chromium";
}

std::wstring BrowserDistribution::GetVersionKey() {
  return L"Software\\Chromium";
}

bool BrowserDistribution::CanSetAsDefault() {
  return true;
}

bool BrowserDistribution::CanCreateDesktopShortcuts() {
  return true;
}

int BrowserDistribution::GetIconIndex() {
  return 0;
}

bool BrowserDistribution::GetChromeChannel(std::wstring* channel) {
  return false;
}

void BrowserDistribution::UpdateInstallStatus(bool system_install,
    installer::ArchiveType archive_type,
    installer::InstallStatus install_status) {
}

bool BrowserDistribution::GetExperimentDetails(
    UserExperiment* experiment, int flavor) {
  return false;
}

void BrowserDistribution::LaunchUserExperiment(
    const FilePath& setup_path, installer::InstallStatus status,
    const Version& version, const installer::Product& installation,
    bool system_level) {
}


void BrowserDistribution::InactiveUserToastExperiment(int flavor,
    const std::wstring& experiment_group,
    const installer::Product& installation,
    const FilePath& application_path) {
}
