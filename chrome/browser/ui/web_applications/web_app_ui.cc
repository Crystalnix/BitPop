// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/web_applications/web_app_ui.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/string16.h"
#include "base/utf_string_conversions.h"
#include "base/win/windows_version.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/favicon/favicon_tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_paths.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/web_contents.h"

#if defined(OS_POSIX) && !defined(OS_MACOSX)
#include "base/environment.h"
#endif

using content::BrowserThread;
using content::NavigationController;
using content::WebContents;

namespace {

#if defined(OS_WIN)
// UpdateShortcutWorker holds all context data needed for update shortcut.
// It schedules a pre-update check to find all shortcuts that needs to be
// updated. If there are such shortcuts, it schedules icon download and
// update them when icons are downloaded. It observes TAB_CLOSING notification
// and cancels all the work when the underlying tab is closing.
class UpdateShortcutWorker : public content::NotificationObserver {
 public:
  explicit UpdateShortcutWorker(TabContents* tab_contents);

  void Run();

 private:
  // Overridden from content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details);

  // Downloads icon via TabContents.
  void DownloadIcon();

  // Callback when icon downloaded.
  void OnIconDownloaded(int download_id, bool errored, const SkBitmap& image);

  // Checks if shortcuts exists on desktop, start menu and quick launch.
  void CheckExistingShortcuts();

  // Update shortcut files and icons.
  void UpdateShortcuts();
  void UpdateShortcutsOnFileThread();

  // Callback after shortcuts are updated.
  void OnShortcutsUpdated(bool);

  // Deletes the worker on UI thread where it gets created.
  void DeleteMe();
  void DeleteMeOnUIThread();

  content::NotificationRegistrar registrar_;

  // Underlying TabContents whose shortcuts will be updated.
  TabContents* tab_contents_;

  // Icons info from tab_contents_'s web app data.
  web_app::IconInfoList unprocessed_icons_;

  // Cached shortcut data from the tab_contents_.
  ShellIntegration::ShortcutInfo shortcut_info_;

  // Our copy of profile path.
  FilePath profile_path_;

  // File name of shortcut/ico file based on app title.
  FilePath file_name_;

  // Existing shortcuts.
  std::vector<FilePath> shortcut_files_;

  DISALLOW_COPY_AND_ASSIGN(UpdateShortcutWorker);
};

UpdateShortcutWorker::UpdateShortcutWorker(TabContents* tab_contents)
    : tab_contents_(tab_contents),
      profile_path_(tab_contents->profile()->GetPath()) {
  web_app::GetShortcutInfoForTab(tab_contents_, &shortcut_info_);
  web_app::GetIconsInfo(tab_contents_->extension_tab_helper()->web_app_info(),
                        &unprocessed_icons_);
  file_name_ = web_app::internals::GetSanitizedFileName(shortcut_info_.title);

  registrar_.Add(
      this,
      chrome::NOTIFICATION_TAB_CLOSING,
      content::Source<NavigationController>(
          &tab_contents_->web_contents()->GetController()));
}

void UpdateShortcutWorker::Run() {
  // Starting by downloading app icon.
  DownloadIcon();
}

void UpdateShortcutWorker::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_TAB_CLOSING &&
      content::Source<NavigationController>(source).ptr() ==
        &tab_contents_->web_contents()->GetController()) {
    // Underlying tab is closing.
    tab_contents_ = NULL;
  }
}

void UpdateShortcutWorker::DownloadIcon() {
  // FetchIcon must run on UI thread because it relies on WebContents
  // to download the icon.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (tab_contents_ == NULL) {
    DeleteMe();  // We are done if underlying WebContents is gone.
    return;
  }

  if (unprocessed_icons_.empty()) {
    // No app icon. Just use the favicon from WebContents.
    UpdateShortcuts();
    return;
  }

  tab_contents_->favicon_tab_helper()->DownloadImage(
      unprocessed_icons_.back().url,
      std::max(unprocessed_icons_.back().width,
               unprocessed_icons_.back().height),
      history::FAVICON,
      base::Bind(&UpdateShortcutWorker::OnIconDownloaded,
                 base::Unretained(this)));
  unprocessed_icons_.pop_back();
}

void UpdateShortcutWorker::OnIconDownloaded(int download_id,
                                            bool errored,
                                            const SkBitmap& image) {
  if (tab_contents_ == NULL) {
    DeleteMe();  // We are done if underlying WebContents is gone.
    return;
  }

  if (!errored && !image.isNull()) {
    // Update icon with download image and update shortcut.
    shortcut_info_.favicon = gfx::Image(image);
    tab_contents_->extension_tab_helper()->SetAppIcon(image);
    UpdateShortcuts();
  } else {
    // Try the next icon otherwise.
    DownloadIcon();
  }
}

void UpdateShortcutWorker::CheckExistingShortcuts() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  // Locations to check to shortcut_paths.
  struct {
    bool& use_this_location;
    int location_id;
    const wchar_t* sub_dir;
  } locations[] = {
    {
      shortcut_info_.create_on_desktop,
      chrome::DIR_USER_DESKTOP,
      NULL
    }, {
      shortcut_info_.create_in_applications_menu,
      base::DIR_START_MENU,
      NULL
    }, {
      shortcut_info_.create_in_quick_launch_bar,
      // For Win7, create_in_quick_launch_bar means pinning to taskbar.
      base::DIR_APP_DATA,
      (base::win::GetVersion() >= base::win::VERSION_WIN7) ?
          L"Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar" :
          L"Microsoft\\Internet Explorer\\Quick Launch"
    }
  };

  for (int i = 0; i < arraysize(locations); ++i) {
    locations[i].use_this_location = false;

    FilePath path;
    if (!PathService::Get(locations[i].location_id, &path)) {
      NOTREACHED();
      continue;
    }

    if (locations[i].sub_dir != NULL)
      path = path.Append(locations[i].sub_dir);

    FilePath shortcut_file = path.Append(file_name_).
        ReplaceExtension(FILE_PATH_LITERAL(".lnk"));
    if (file_util::PathExists(shortcut_file)) {
      locations[i].use_this_location = true;
      shortcut_files_.push_back(shortcut_file);
    }
  }
}

void UpdateShortcutWorker::UpdateShortcuts() {
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
      base::Bind(&UpdateShortcutWorker::UpdateShortcutsOnFileThread,
                 base::Unretained(this)));
}

void UpdateShortcutWorker::UpdateShortcutsOnFileThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  FilePath web_app_path = web_app::GetWebAppDataDirectory(
      profile_path_, shortcut_info_.extension_id, shortcut_info_.url);

  // Ensure web_app_path exists. web_app_path could be missing for a legacy
  // shortcut created by Gears.
  if (!file_util::PathExists(web_app_path) &&
      !file_util::CreateDirectory(web_app_path)) {
    NOTREACHED();
    return;
  }

  FilePath icon_file = web_app_path.Append(file_name_).ReplaceExtension(
      FILE_PATH_LITERAL(".ico"));
  web_app::internals::CheckAndSaveIcon(icon_file,
                                       *shortcut_info_.favicon.ToSkBitmap());

  // Update existing shortcuts' description, icon and app id.
  CheckExistingShortcuts();
  if (!shortcut_files_.empty()) {
    // Generates app id from web app url and profile path.
    string16 app_id = ShellIntegration::GetAppModelIdForProfile(
        UTF8ToWide(web_app::GenerateApplicationNameFromURL(shortcut_info_.url)),
        profile_path_);

    // Sanitize description
    if (shortcut_info_.description.length() >= MAX_PATH)
      shortcut_info_.description.resize(MAX_PATH - 1);

    for (size_t i = 0; i < shortcut_files_.size(); ++i) {
      file_util::CreateOrUpdateShortcutLink(
          NULL,
          shortcut_files_[i].value().c_str(),
          NULL,
          NULL,
          shortcut_info_.description.c_str(),
          icon_file.value().c_str(),
          0,
          app_id.c_str(),
          file_util::SHORTCUT_NO_OPTIONS);
    }
  }

  OnShortcutsUpdated(true);
}

void UpdateShortcutWorker::OnShortcutsUpdated(bool) {
  DeleteMe();  // We are done.
}

void UpdateShortcutWorker::DeleteMe() {
  if (BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    DeleteMeOnUIThread();
  } else {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
      base::Bind(&UpdateShortcutWorker::DeleteMeOnUIThread,
                 base::Unretained(this)));
  }
}

void UpdateShortcutWorker::DeleteMeOnUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  delete this;
}
#endif  // defined(OS_WIN)

}  // namespace

namespace web_app {

void GetShortcutInfoForTab(TabContents* tab_contents,
                           ShellIntegration::ShortcutInfo* info) {
  DCHECK(info);  // Must provide a valid info.
  const WebContents* web_contents = tab_contents->web_contents();

  const WebApplicationInfo& app_info =
      tab_contents->extension_tab_helper()->web_app_info();

  info->url = app_info.app_url.is_empty() ? web_contents->GetURL() :
                                            app_info.app_url;
  info->title = app_info.title.empty() ?
      (web_contents->GetTitle().empty() ? UTF8ToUTF16(info->url.spec()) :
                                          web_contents->GetTitle()) :
      app_info.title;
  info->description = app_info.description;
  info->favicon =
      gfx::Image(tab_contents->favicon_tab_helper()->GetFavicon());
}

void UpdateShortcutForTabContents(TabContents* tab_contents) {
#if defined(OS_WIN)
  // UpdateShortcutWorker will delete itself when it's done.
  UpdateShortcutWorker* worker = new UpdateShortcutWorker(tab_contents);
  worker->Run();
#endif  // defined(OS_WIN)
}

}  // namespace web_app
