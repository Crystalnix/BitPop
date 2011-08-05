// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_item.h"

#include "base/basictypes.h"
#include "base/file_util.h"
#include "base/format_macros.h"
#include "base/i18n/case_conversion.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/stringprintf.h"
#include "base/timer.h"
#include "base/utf_string_conversions.h"
#include "net/base/net_util.h"
#include "chrome/browser/download/download_create_info.h"
#include "chrome/browser/download/download_extensions.h"
#include "chrome/browser/download/download_file_manager.h"
#include "chrome/browser/download/download_history.h"
#include "chrome/browser/download/download_manager.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/download/download_state_info.h"
#include "chrome/browser/download/download_util.h"
#include "chrome/browser/extensions/crx_installer.h"
#include "chrome/browser/history/download_history_info.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/pref_names.h"
#include "content/browser/browser_thread.h"
#include "content/common/notification_source.h"
#include "ui/base/l10n/l10n_util.h"

// A DownloadItem normally goes through the following states:
//      * Created (when download starts)
//      * Made visible to consumers (e.g. Javascript) after the
//        destination file has been determined.
//      * Entered into the history database.
//      * Made visible in the download shelf.
//      * All data is saved.  Note that the actual data download occurs
//        in parallel with the above steps, but until those steps are
//        complete, completion of the data download will be ignored.
//      * Download file is renamed to its final name, and possibly
//        auto-opened.
// TODO(rdsmith): This progress should be reflected in
// DownloadItem::DownloadState and a state transition table/state diagram.
//
// TODO(rdsmith): This description should be updated to reflect the cancel
// pathways.

namespace {

// Update frequency (milliseconds).
const int kUpdateTimeMs = 1000;

void DeleteDownloadedFile(const FilePath& path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  // Make sure we only delete files.
  if (!file_util::DirectoryExists(path))
    file_util::Delete(path, false);
}

const char* DebugSafetyStateString(DownloadItem::SafetyState state) {
  switch (state) {
    case DownloadItem::SAFE:
      return "SAFE";
    case DownloadItem::DANGEROUS:
      return "DANGEROUS";
    case DownloadItem::DANGEROUS_BUT_VALIDATED:
      return "DANGEROUS_BUT_VALIDATED";
    default:
      NOTREACHED() << "Unknown safety state " << state;
      return "unknown";
  };
}

const char* DebugDownloadStateString(DownloadItem::DownloadState state) {
  switch (state) {
    case DownloadItem::IN_PROGRESS:
      return "IN_PROGRESS";
    case DownloadItem::COMPLETE:
      return "COMPLETE";
    case DownloadItem::CANCELLED:
      return "CANCELLED";
    case DownloadItem::REMOVING:
      return "REMOVING";
    case DownloadItem::INTERRUPTED:
      return "INTERRUPTED";
    default:
      NOTREACHED() << "Unknown download state " << state;
      return "unknown";
  };
}

DownloadItem::SafetyState GetSafetyState(bool dangerous_file,
                                         bool dangerous_url) {
  return (dangerous_url || dangerous_file) ?
      DownloadItem::DANGEROUS : DownloadItem::SAFE;
}

// Note: When a download has both |dangerous_file| and |dangerous_url| set,
// danger type is set to DANGEROUS_URL since the risk of dangerous URL
// overweights that of dangerous file type.
DownloadItem::DangerType GetDangerType(bool dangerous_file,
                                       bool dangerous_url) {
  if (dangerous_url) {
    // dangerous URL overweights dangerous file. We check dangerous URL first.
    return DownloadItem::DANGEROUS_URL;
  }
  return dangerous_file ?
      DownloadItem::DANGEROUS_FILE : DownloadItem::NOT_DANGEROUS;
}

}  // namespace

// Constructor for reading from the history service.
DownloadItem::DownloadItem(DownloadManager* download_manager,
                           const DownloadHistoryInfo& info)
    : download_id_(-1),
      full_path_(info.path),
      url_chain_(1, info.url),
      referrer_url_(info.referrer_url),
      total_bytes_(info.total_bytes),
      received_bytes_(info.received_bytes),
      start_tick_(base::TimeTicks()),
      state_(static_cast<DownloadState>(info.state)),
      start_time_(info.start_time),
      db_handle_(info.db_handle),
      download_manager_(download_manager),
      is_paused_(false),
      open_when_complete_(false),
      safety_state_(SAFE),
      auto_opened_(false),
      is_otr_(false),
      is_temporary_(false),
      all_data_saved_(false),
      opened_(false),
      open_enabled_(true) {
  if (IsInProgress())
    state_ = CANCELLED;
  if (IsComplete())
    all_data_saved_ = true;
  Init(false /* don't start progress timer */);
}

// Constructing for a regular download:
DownloadItem::DownloadItem(DownloadManager* download_manager,
                           const DownloadCreateInfo& info,
                           bool is_otr)
    : state_info_(info.original_name, info.save_info.file_path,
                  info.has_user_gesture, info.prompt_user_for_save_location,
                  info.path_uniquifier, false, false,
                  info.is_extension_install),
      process_handle_(info.process_handle),
      download_id_(info.download_id),
      full_path_(info.path),
      url_chain_(info.url_chain),
      referrer_url_(info.referrer_url),
      content_disposition_(info.content_disposition),
      mime_type_(info.mime_type),
      original_mime_type_(info.original_mime_type),
      referrer_charset_(info.referrer_charset),
      total_bytes_(info.total_bytes),
      received_bytes_(0),
      last_os_error_(0),
      start_tick_(base::TimeTicks::Now()),
      state_(IN_PROGRESS),
      start_time_(info.start_time),
      db_handle_(DownloadHistory::kUninitializedHandle),
      download_manager_(download_manager),
      is_paused_(false),
      open_when_complete_(false),
      safety_state_(SAFE),
      auto_opened_(false),
      is_otr_(is_otr),
      is_temporary_(!info.save_info.file_path.empty()),
      all_data_saved_(false),
      opened_(false),
      open_enabled_(true) {
  Init(true /* start progress timer */);
}

// Constructing for the "Save Page As..." feature:
DownloadItem::DownloadItem(DownloadManager* download_manager,
                           const FilePath& path,
                           const GURL& url,
                           bool is_otr)
    : download_id_(1),
      full_path_(path),
      url_chain_(1, url),
      referrer_url_(GURL()),
      total_bytes_(0),
      received_bytes_(0),
      last_os_error_(0),
      start_tick_(base::TimeTicks::Now()),
      state_(IN_PROGRESS),
      start_time_(base::Time::Now()),
      db_handle_(DownloadHistory::kUninitializedHandle),
      download_manager_(download_manager),
      is_paused_(false),
      open_when_complete_(false),
      safety_state_(SAFE),
      auto_opened_(false),
      is_otr_(is_otr),
      is_temporary_(false),
      all_data_saved_(false),
      opened_(false),
      open_enabled_(true) {
  Init(true /* start progress timer */);
}

DownloadItem::~DownloadItem() {
  state_ = REMOVING;
  UpdateObservers();
}

void DownloadItem::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void DownloadItem::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void DownloadItem::UpdateObservers() {
  FOR_EACH_OBSERVER(Observer, observers_, OnDownloadUpdated(this));
}

bool DownloadItem::CanOpenDownload() {
  return !Extension::IsExtension(state_info_.target_name);
}

bool DownloadItem::ShouldOpenFileBasedOnExtension() {
  return download_manager_->ShouldOpenFileBasedOnExtension(
      GetUserVerifiedFilePath());
}

void DownloadItem::OpenFilesBasedOnExtension(bool open) {
  DownloadPrefs* prefs = download_manager_->download_prefs();
  if (open)
    prefs->EnableAutoOpenBasedOnExtension(GetUserVerifiedFilePath());
  else
    prefs->DisableAutoOpenBasedOnExtension(GetUserVerifiedFilePath());
}

void DownloadItem::OpenDownload() {
  if (IsPartialDownload()) {
    open_when_complete_ = !open_when_complete_;
  } else if (IsComplete()) {
    opened_ = true;
    FOR_EACH_OBSERVER(Observer, observers_, OnDownloadOpened(this));

    // For testing: If download opening is disabled on this item,
    // make the rest of the routine a no-op.
    if (!open_enabled_)
      return;

    if (is_extension_install()) {
      download_util::OpenChromeExtension(download_manager_->profile(), *this);
      return;
    }
#if defined(OS_MACOSX)
    // Mac OS X requires opening downloads on the UI thread.
    platform_util::OpenItem(full_path());
#else
    BrowserThread::PostTask(
        BrowserThread::FILE, FROM_HERE,
        NewRunnableFunction(&platform_util::OpenItem, full_path()));
#endif
  }
}

void DownloadItem::ShowDownloadInShell() {
#if defined(OS_MACOSX)
  // Mac needs to run this operation on the UI thread.
  platform_util::ShowItemInFolder(full_path());
#else
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableFunction(&platform_util::ShowItemInFolder,
                          full_path()));
#endif
}

void DownloadItem::DangerousDownloadValidated() {
  UMA_HISTOGRAM_ENUMERATION("Download.DangerousDownloadValidated",
                            GetDangerType(),
                            DANGEROUS_TYPE_MAX);
  download_manager_->DangerousDownloadValidated(this);
}

void DownloadItem::UpdateSize(int64 bytes_so_far) {
  received_bytes_ = bytes_so_far;

  // If we've received more data than we were expecting (bad server info?),
  // revert to 'unknown size mode'.
  if (received_bytes_ > total_bytes_)
    total_bytes_ = 0;
}

void DownloadItem::StartProgressTimer() {
  update_timer_.Start(base::TimeDelta::FromMilliseconds(kUpdateTimeMs), this,
                      &DownloadItem::UpdateObservers);
}

void DownloadItem::StopProgressTimer() {
  update_timer_.Stop();
}

// Updates from the download thread may have been posted while this download
// was being cancelled in the UI thread, so we'll accept them unless we're
// complete.
void DownloadItem::Update(int64 bytes_so_far) {
  if (!IsInProgress()) {
    NOTREACHED();
    return;
  }
  UpdateSize(bytes_so_far);
  UpdateObservers();
}

// Triggered by a user action.
void DownloadItem::Cancel(bool update_history) {
  VLOG(20) << __FUNCTION__ << "() download = " << DebugString(true);
  if (!IsPartialDownload()) {
    // Small downloads might be complete before this method has
    // a chance to run.
    return;
  }

  download_util::RecordDownloadCount(download_util::CANCELLED_COUNT);

  state_ = CANCELLED;
  UpdateObservers();
  StopProgressTimer();
  if (update_history)
    download_manager_->DownloadCancelled(download_id_);
}

void DownloadItem::MarkAsComplete() {
  DCHECK(all_data_saved_);
  state_ = COMPLETE;
  UpdateObservers();
}

void DownloadItem::OnAllDataSaved(int64 size) {
  DCHECK(!all_data_saved_);
  all_data_saved_ = true;
  UpdateSize(size);
  StopProgressTimer();
}

void DownloadItem::Completed() {
  VLOG(20) << __FUNCTION__ << "() " << DebugString(false);

  DCHECK(all_data_saved_);
  state_ = COMPLETE;
  UpdateObservers();
  download_manager_->DownloadCompleted(id());
  download_util::RecordDownloadCount(download_util::COMPLETED_COUNT);

  if (is_extension_install()) {
    // Extensions should already have been unpacked and opened.
    auto_opened_ = true;
  } else if (open_when_complete() ||
             download_manager_->ShouldOpenFileBasedOnExtension(
                 GetUserVerifiedFilePath()) ||
             is_temporary()) {
    // If the download is temporary, like in drag-and-drop, do not open it but
    // we still need to set it auto-opened so that it can be removed from the
    // download shelf.
    if (!is_temporary())
      OpenDownload();

    auto_opened_ = true;
    UpdateObservers();
  }
}

void DownloadItem::StartCrxInstall() {
  DCHECK(is_extension_install());
  DCHECK(all_data_saved_);

  scoped_refptr<CrxInstaller> crx_installer =
      download_util::OpenChromeExtension(
          download_manager_->profile(),
          *this);

  // CRX_INSTALLER_DONE will fire when the install completes.  Observe()
  // will call Completed() on this item.  If this DownloadItem is not
  // around when CRX_INSTALLER_DONE fires, Complete() will not be called.
  registrar_.Add(this,
                 NotificationType::CRX_INSTALLER_DONE,
                 Source<CrxInstaller>(crx_installer.get()));

  // The status text and percent complete indicator will change now
  // that we are installing a CRX.  Update observers so that they pick
  // up the change.
  UpdateObservers();
}

// NotificationObserver implementation.
void DownloadItem::Observe(NotificationType type,
                           const NotificationSource& source,
                           const NotificationDetails& details) {
  DCHECK(type == NotificationType::CRX_INSTALLER_DONE);

  // No need to listen for CRX_INSTALLER_DONE anymore.
  registrar_.Remove(this,
                    NotificationType::CRX_INSTALLER_DONE,
                    source);

  auto_opened_ = true;
  DCHECK(all_data_saved_);

  Completed();
}

void DownloadItem::Interrupted(int64 size, int os_error) {
  if (!IsInProgress())
    return;
  state_ = INTERRUPTED;
  last_os_error_ = os_error;
  UpdateSize(size);
  StopProgressTimer();
  UpdateObservers();
}

void DownloadItem::Delete(DeleteReason reason) {
  switch (reason) {
    case DELETE_DUE_TO_USER_DISCARD:
      UMA_HISTOGRAM_ENUMERATION("Download.UserDiscard", GetDangerType(),
                                DANGEROUS_TYPE_MAX);
      break;
    case DELETE_DUE_TO_BROWSER_SHUTDOWN:
      UMA_HISTOGRAM_ENUMERATION("Download.Discard", GetDangerType(),
                                DANGEROUS_TYPE_MAX);
      break;
    default:
      NOTREACHED();
  }

  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
      NewRunnableFunction(&DeleteDownloadedFile, full_path_));
  Remove();
  // We have now been deleted.
}

void DownloadItem::Remove() {
  Cancel(true);
  state_ = REMOVING;
  download_manager_->RemoveDownload(db_handle_);
  // We have now been deleted.
}

bool DownloadItem::TimeRemaining(base::TimeDelta* remaining) const {
  if (total_bytes_ <= 0)
    return false;  // We never received the content_length for this download.

  int64 speed = CurrentSpeed();
  if (speed == 0)
    return false;

  *remaining = base::TimeDelta::FromSeconds(
      (total_bytes_ - received_bytes_) / speed);
  return true;
}

int64 DownloadItem::CurrentSpeed() const {
  if (is_paused_)
    return 0;
  base::TimeDelta diff = base::TimeTicks::Now() - start_tick_;
  int64 diff_ms = diff.InMilliseconds();
  return diff_ms == 0 ? 0 : received_bytes_ * 1000 / diff_ms;
}

int DownloadItem::PercentComplete() const {
  // We don't have an accurate way to estimate the time to unpack a CRX.
  // The slowest part is re-encoding images, and time to do this depends on
  // the contents of the image.  If a CRX is being unpacked, indicate that
  // we do not know how close to completion we are.
  if (IsCrxInstallRuning() || total_bytes_ <= 0)
    return -1;

  return static_cast<int>(received_bytes_ * 100.0 / total_bytes_);
}

void DownloadItem::Rename(const FilePath& full_path) {
  VLOG(20) << __FUNCTION__ << "()"
           << " full_path = \"" << full_path.value() << "\""
           << " " << DebugString(true);
  DCHECK(!full_path.empty());
  full_path_ = full_path;
}

void DownloadItem::TogglePause() {
  DCHECK(IsInProgress());
  download_manager_->PauseDownload(download_id_, !is_paused_);
  is_paused_ = !is_paused_;
  UpdateObservers();
}

void DownloadItem::OnDownloadCompleting(DownloadFileManager* file_manager) {
  VLOG(20) << __FUNCTION__ << "()"
           << " needs rename = " << NeedsRename()
           << " " << DebugString(true);
  DCHECK_NE(DANGEROUS, safety_state());
  DCHECK(file_manager);

  if (NeedsRename()) {
    BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
        NewRunnableMethod(file_manager,
            &DownloadFileManager::RenameCompletingDownloadFile, id(),
            GetTargetFilePath(), safety_state() == SAFE));
    return;
  }

  DCHECK(!is_extension_install());
  Completed();

  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(file_manager, &DownloadFileManager::CompleteDownload,
                        id()));
}

void DownloadItem::OnDownloadRenamedToFinalName(const FilePath& full_path) {
  VLOG(20) << __FUNCTION__ << "()"
           << " full_path = \"" << full_path.value() << "\""
           << " needed rename = " << NeedsRename()
           << " " << DebugString(false);
  DCHECK(NeedsRename());

  Rename(full_path);

  if (is_extension_install()) {
    StartCrxInstall();
    // Completed() will be called when the installer finishes.
    return;
  }

  Completed();
}

bool DownloadItem::MatchesQuery(const string16& query) const {
  if (query.empty())
    return true;

  DCHECK_EQ(query, base::i18n::ToLower(query));

  string16 url_raw(base::i18n::ToLower(UTF8ToUTF16(GetURL().spec())));
  if (url_raw.find(query) != string16::npos)
    return true;

  // TODO(phajdan.jr): write a test case for the following code.
  // A good test case would be:
  //   "/\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xbd\xa0\xe5\xa5\xbd",
  //   L"/\x4f60\x597d\x4f60\x597d",
  //   "/%E4%BD%A0%E5%A5%BD%E4%BD%A0%E5%A5%BD"
  PrefService* prefs = download_manager_->profile()->GetPrefs();
  std::string languages(prefs->GetString(prefs::kAcceptLanguages));
  string16 url_formatted(
      base::i18n::ToLower(net::FormatUrl(GetURL(), languages)));
  if (url_formatted.find(query) != string16::npos)
    return true;

  string16 path(base::i18n::ToLower(full_path().LossyDisplayName()));
  // This shouldn't just do a substring match; it is wrong for Unicode
  // due to normalization and we have a fancier search-query system
  // used elsewhere.
  // http://code.google.com/p/chromium/issues/detail?id=71982
  return (path.find(query) != string16::npos);
}

void DownloadItem::SetFileCheckResults(const DownloadStateInfo& state) {
  VLOG(20) << " " << __FUNCTION__ << "()" << " this = " << DebugString(true);
  state_info_ = state;
  VLOG(20) << " " << __FUNCTION__ << "()" << " this = " << DebugString(true);

  safety_state_ = GetSafetyState(state_info_.is_dangerous_file,
                                 state_info_.is_dangerous_url);
}

void DownloadItem::UpdateTarget() {
  if (state_info_.target_name.value().empty())
    state_info_.target_name = full_path_.BaseName();
}

DownloadItem::DangerType DownloadItem::GetDangerType() const {
  return ::GetDangerType(state_info_.is_dangerous_file,
                         state_info_.is_dangerous_url);
}

bool DownloadItem::IsDangerous() const {
  return GetDangerType() != DownloadItem::NOT_DANGEROUS;
}

void DownloadItem::MarkFileDangerous() {
  state_info_.is_dangerous_file = true;
  safety_state_ = GetSafetyState(state_info_.is_dangerous_file,
                                 state_info_.is_dangerous_url);
}

void DownloadItem::MarkUrlDangerous() {
  state_info_.is_dangerous_url = true;
  safety_state_ = GetSafetyState(state_info_.is_dangerous_file,
                                 state_info_.is_dangerous_url);
}

DownloadHistoryInfo DownloadItem::GetHistoryInfo() const {
  return DownloadHistoryInfo(full_path(),
                             GetURL(),
                             referrer_url(),
                             start_time(),
                             received_bytes(),
                             total_bytes(),
                             state(),
                             db_handle());
}

FilePath DownloadItem::GetTargetFilePath() const {
  return full_path_.DirName().Append(state_info_.target_name);
}

FilePath DownloadItem::GetFileNameToReportUser() const {
  if (state_info_.path_uniquifier > 0) {
    FilePath name(state_info_.target_name);
    download_util::AppendNumberToPath(&name, state_info_.path_uniquifier);
    return name;
  }
  return state_info_.target_name;
}

FilePath DownloadItem::GetUserVerifiedFilePath() const {
  return (safety_state_ == DownloadItem::SAFE) ?
      GetTargetFilePath() : full_path_;
}

void DownloadItem::Init(bool start_timer) {
  UpdateTarget();
  if (start_timer)
    StartProgressTimer();
  VLOG(20) << __FUNCTION__ << "() " << DebugString(true);
}

// TODO(ahendrickson) -- Move |INTERRUPTED| from |IsCancelled()| to
// |IsPartialDownload()|, when resuming interrupted downloads is implemented.
bool DownloadItem::IsPartialDownload() const {
  return (state_ == IN_PROGRESS);
}

bool DownloadItem::IsInProgress() const {
  return (state_ == IN_PROGRESS);
}

bool DownloadItem::IsCancelled() const {
  return (state_ == CANCELLED) ||
         (state_ == INTERRUPTED);
}

bool DownloadItem::IsInterrupted() const {
  return (state_ == INTERRUPTED);
}

bool DownloadItem::IsComplete() const {
  return (state_ == COMPLETE);
}

const GURL& DownloadItem::GetURL() const {
  return url_chain_.empty() ?
             GURL::EmptyGURL() : url_chain_.back();
}

std::string DownloadItem::DebugString(bool verbose) const {
  std::string description =
      base::StringPrintf("{ id = %d"
                         " state = %s",
                         download_id_,
                         DebugDownloadStateString(state()));

  // Construct a string of the URL chain.
  std::string url_list("<none>");
  if (!url_chain_.empty()) {
    std::vector<GURL>::const_iterator iter = url_chain_.begin();
    std::vector<GURL>::const_iterator last = url_chain_.end();
    url_list = (*iter).spec();
    ++iter;
    for ( ; verbose && (iter != last); ++iter) {
      url_list += " ->\n\t";
      const GURL& next_url = *iter;
      url_list += next_url.spec();
    }
  }

  if (verbose) {
    description += base::StringPrintf(
        " db_handle = %" PRId64
        " total_bytes = %" PRId64
        " received_bytes = %" PRId64
        " is_paused = %c"
        " is_extension_install = %c"
        " is_otr = %c"
        " safety_state = %s"
        " url_chain = \n\t\"%s\"\n\t"
        " target_name = \"%" PRFilePath "\""
        " full_path = \"%" PRFilePath "\"",
        db_handle(),
        total_bytes(),
        received_bytes(),
        is_paused() ? 'T' : 'F',
        is_extension_install() ? 'T' : 'F',
        is_otr() ? 'T' : 'F',
        DebugSafetyStateString(safety_state()),
        url_list.c_str(),
        state_info_.target_name.value().c_str(),
        full_path().value().c_str());
  } else {
    description += base::StringPrintf(" url = \"%s\"", url_list.c_str());
  }

  description += " }";

  return description;
}
