// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/webstore_installer.h"

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/rand_util.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/download_crx_util.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/download/download_util.h"
#include "chrome/browser/extensions/crx_installer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/download_save_info.h"
#include "content/public/browser/download_url_parameters.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "googleurl/src/gurl.h"
#include "net/base/escape.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/gdata/gdata_util.h"
#endif

using content::BrowserContext;
using content::BrowserThread;
using content::DownloadId;
using content::DownloadItem;
using content::DownloadManager;
using content::NavigationController;
using content::DownloadUrlParameters;

namespace {

// Key used to attach the Approval to the DownloadItem.
const char kApprovalKey[] = "extensions.webstore_installer";

const char kInvalidIdError[] = "Invalid id";
const char kNoBrowserError[] = "No browser found";
const char kDownloadDirectoryError[] = "Could not create download directory";
const char kDownloadCanceledError[] = "Download canceled";
const char kInstallCanceledError[] = "Install canceled";
const char kDownloadInterruptedError[] = "Download interrupted";
const char kInvalidDownloadError[] = "Download was not a CRX";
const char kInlineInstallSource[] = "inline";
const char kDefaultInstallSource[] = "";

FilePath* g_download_directory_for_tests = NULL;

GURL GetWebstoreInstallURL(
    const std::string& extension_id, const std::string& install_source) {
  CommandLine* cmd_line = CommandLine::ForCurrentProcess();
  if (cmd_line->HasSwitch(switches::kAppsGalleryDownloadURL)) {
    std::string download_url =
        cmd_line->GetSwitchValueASCII(switches::kAppsGalleryDownloadURL);
    return GURL(base::StringPrintf(download_url.c_str(),
                                   extension_id.c_str()));
  }
  std::vector<std::string> params;
  params.push_back("id=" + extension_id);
  if (!install_source.empty())
    params.push_back("installsource=" + install_source);
  params.push_back("lang=" + g_browser_process->GetApplicationLocale());
  params.push_back("uc");
  std::string url_string = extension_urls::GetWebstoreUpdateUrl().spec();

  GURL url(url_string + "?response=redirect&x=" +
      net::EscapeQueryParamValue(JoinString(params, '&'), true));
  DCHECK(url.is_valid());

  return url;
}

// Must be executed on the FILE thread.
void GetDownloadFilePath(
    const FilePath& download_directory, const std::string& id,
    const base::Callback<void(const FilePath&)>& callback) {
  FilePath directory(g_download_directory_for_tests ?
                     *g_download_directory_for_tests : download_directory);

#if defined (OS_CHROMEOS)
  // Do not use drive for extension downloads.
  if (gdata::util::IsUnderGDataMountPoint(directory))
    directory = download_util::GetDefaultDownloadDirectory();
#endif

  // Ensure the download directory exists. TODO(asargent) - make this use
  // common code from the downloads system.
  if (!file_util::DirectoryExists(directory)) {
    if (!file_util::CreateDirectory(directory)) {
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                              base::Bind(callback, FilePath()));
      return;
    }
  }

  // This is to help avoid a race condition between when we generate this
  // filename and when the download starts writing to it (think concurrently
  // running sharded browser tests installing the same test file, for
  // instance).
  std::string random_number =
      base::Uint64ToString(base::RandGenerator(kuint16max));

  FilePath file = directory.AppendASCII(id + "_" + random_number + ".crx");

  int uniquifier = file_util::GetUniquePathNumber(file, FILE_PATH_LITERAL(""));
  if (uniquifier > 0)
    file = file.InsertBeforeExtensionASCII(StringPrintf(" (%d)", uniquifier));

  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                          base::Bind(callback, file));
}

}  // namespace

namespace extensions {

WebstoreInstaller::Approval::Approval()
    : profile(NULL),
      use_app_installed_bubble(false),
      skip_post_install_ui(false),
      skip_install_dialog(false),
      record_oauth2_grant(false) {
}

scoped_ptr<WebstoreInstaller::Approval>
WebstoreInstaller::Approval::CreateWithInstallPrompt(Profile* profile) {
  scoped_ptr<Approval> result(new Approval());
  result->profile = profile;
  return result.Pass();
}

scoped_ptr<WebstoreInstaller::Approval>
WebstoreInstaller::Approval::CreateWithNoInstallPrompt(
    Profile* profile,
    const std::string& extension_id,
    scoped_ptr<base::DictionaryValue> parsed_manifest) {
  scoped_ptr<Approval> result(new Approval());
  result->extension_id = extension_id;
  result->profile = profile;
  result->parsed_manifest = parsed_manifest.Pass();
  result->skip_install_dialog = true;
  return result.Pass();
}

WebstoreInstaller::Approval::~Approval() {}

const WebstoreInstaller::Approval* WebstoreInstaller::GetAssociatedApproval(
    const DownloadItem& download) {
  return static_cast<const Approval*>(download.GetExternalData(kApprovalKey));
}

WebstoreInstaller::WebstoreInstaller(Profile* profile,
                                     Delegate* delegate,
                                     NavigationController* controller,
                                     const std::string& id,
                                     scoped_ptr<Approval> approval,
                                     int flags)
    : profile_(profile),
      delegate_(delegate),
      controller_(controller),
      id_(id),
      download_item_(NULL),
      flags_(flags),
      approval_(approval.release()) {
  // TODO(benjhayden): Change this CHECK to DCHECK after http://crbug.com/126013
  CHECK(controller_);
  download_url_ = GetWebstoreInstallURL(id, flags & FLAG_INLINE_INSTALL ?
      kInlineInstallSource : kDefaultInstallSource);

  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  registrar_.Add(this, chrome::NOTIFICATION_CRX_INSTALLER_DONE,
                 content::NotificationService::AllSources());
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_INSTALLED,
                 content::Source<Profile>(profile->GetOriginalProfile()));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_INSTALL_ERROR,
                 content::Source<CrxInstaller>(NULL));
}

void WebstoreInstaller::Start() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  AddRef();  // Balanced in ReportSuccess and ReportFailure.

  if (!Extension::IdIsValid(id_)) {
    ReportFailure(kInvalidIdError);
    return;
  }

  FilePath download_path = DownloadPrefs::FromDownloadManager(
      BrowserContext::GetDownloadManager(profile_))->DownloadPath();
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&GetDownloadFilePath, download_path, id_,
                 base::Bind(&WebstoreInstaller::StartDownload, this)));
}

void WebstoreInstaller::Observe(int type,
                                const content::NotificationSource& source,
                                const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_CRX_INSTALLER_DONE: {
      const Extension* extension =
          content::Details<const Extension>(details).ptr();
      CrxInstaller* installer = content::Source<CrxInstaller>(source).ptr();
      if (extension == NULL && download_item_ != NULL &&
          installer->download_url() == download_item_->GetURL() &&
          installer->profile()->IsSameProfile(profile_)) {
        ReportFailure(kInstallCanceledError);
      }
      break;
    }

    case chrome::NOTIFICATION_EXTENSION_INSTALLED: {
      CHECK(profile_->IsSameProfile(content::Source<Profile>(source).ptr()));
      const Extension* extension =
          content::Details<const Extension>(details).ptr();
      if (id_ == extension->id())
        ReportSuccess();
      break;
    }

    case chrome::NOTIFICATION_EXTENSION_INSTALL_ERROR: {
      CrxInstaller* crx_installer = content::Source<CrxInstaller>(source).ptr();
      CHECK(crx_installer);
      if (!profile_->IsSameProfile(crx_installer->profile()))
        return;

      // TODO(rdevlin.cronin): Continue removing std::string errors and
      // replacing with string16
      const string16* error = content::Details<const string16>(details).ptr();
      const std::string utf8_error = UTF16ToUTF8(*error);
      if (download_url_ == crx_installer->original_download_url())
        ReportFailure(utf8_error);
      break;
    }

    default:
      NOTREACHED();
  }
}

void WebstoreInstaller::SetDownloadDirectoryForTests(FilePath* directory) {
  g_download_directory_for_tests = directory;
}

WebstoreInstaller::~WebstoreInstaller() {
  if (download_item_) {
    download_item_->RemoveObserver(this);
    download_item_ = NULL;
  }
}

void WebstoreInstaller::OnDownloadStarted(DownloadId id, net::Error error) {
  if (error != net::OK) {
    ReportFailure(net::ErrorToString(error));
    return;
  }

  CHECK(id.IsValid());

  DownloadManager* download_manager =
      BrowserContext::GetDownloadManager(profile_);
  download_item_ = download_manager->GetActiveDownloadItem(id.local());
  download_item_->AddObserver(this);
  if (approval_.get())
    download_item_->SetExternalData(kApprovalKey, approval_.release());
}

void WebstoreInstaller::OnDownloadUpdated(DownloadItem* download) {
  CHECK_EQ(download_item_, download);

  switch (download->GetState()) {
    case DownloadItem::CANCELLED:
      ReportFailure(kDownloadCanceledError);
      break;
    case DownloadItem::INTERRUPTED:
      ReportFailure(kDownloadInterruptedError);
      break;
    case DownloadItem::REMOVING:
      download_item_->RemoveObserver(this);
      download_item_ = NULL;
      break;
    case DownloadItem::COMPLETE:
      // Wait for other notifications if the download is really an extension.
      if (!download_crx_util::IsExtensionDownload(*download))
        ReportFailure(kInvalidDownloadError);
      break;
    default:
      // Continue listening if the download is not in one of the above states.
      break;
  }
}

void WebstoreInstaller::OnDownloadOpened(DownloadItem* download) {
  CHECK_EQ(download_item_, download);
}

void WebstoreInstaller::StartDownload(const FilePath& file) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (file.empty() || !controller_->GetWebContents()) {
    ReportFailure(kDownloadDirectoryError);
    return;
  }

  content::DownloadSaveInfo save_info;
  save_info.file_path = file;

  // The download url for the given extension is contained in |download_url_|.
  // We will navigate the current tab to this url to start the download. The
  // download system will then pass the crx to the CrxInstaller.
  download_util::RecordDownloadSource(
      download_util::INITIATED_BY_WEBSTORE_INSTALLER);
  scoped_ptr<DownloadUrlParameters> params(
      DownloadUrlParameters::FromWebContents(
          controller_->GetWebContents(), download_url_, save_info));
  if (controller_->GetActiveEntry())
    params->set_referrer(
        content::Referrer(controller_->GetActiveEntry()->GetURL(),
                          WebKit::WebReferrerPolicyDefault));
  params->set_callback(base::Bind(&WebstoreInstaller::OnDownloadStarted, this));
  BrowserContext::GetDownloadManager(profile_)->DownloadUrl(params.Pass());
}

void WebstoreInstaller::ReportFailure(const std::string& error) {
  if (delegate_) {
    delegate_->OnExtensionInstallFailure(id_, error);
    delegate_ = NULL;
  }

  Release();  // Balanced in Start().
}

void WebstoreInstaller::ReportSuccess() {
  if (delegate_) {
    delegate_->OnExtensionInstallSuccess(id_);
    delegate_ = NULL;
  }

  Release();  // Balanced in Start().
}

}  // namespace extensions
