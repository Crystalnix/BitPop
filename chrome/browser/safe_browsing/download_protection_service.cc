// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/download_protection_service.h"

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/format_macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram.h"
#include "base/sequenced_task_runner_helpers.h"
#include "base/stl_util.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/time.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/browser/safe_browsing/signature_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/safe_browsing/csd.pb.h"
#include "chrome/common/url_constants.h"
#include "chrome/common/zip_reader.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/page_navigator.h"
#include "google_apis/google_api_keys.h"
#include "net/base/escape.h"
#include "net/base/load_flags.h"
#include "net/base/x509_cert_types.h"
#include "net/base/x509_certificate.h"
#include "net/http/http_status_code.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_status.h"

using content::BrowserThread;

namespace {
static const int64 kDownloadRequestTimeoutMs = 3000;
}  // namespace

namespace safe_browsing {

const char DownloadProtectionService::kDownloadRequestUrl[] =
    "https://sb-ssl.google.com/safebrowsing/clientreport/download";

namespace {
bool IsArchiveFile(const FilePath& file) {
  return file.MatchesExtension(FILE_PATH_LITERAL(".zip"));
}

bool IsBinaryFile(const FilePath& file) {
  return (
      // Executable extensions for MS Windows.
      file.MatchesExtension(FILE_PATH_LITERAL(".bas")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".bat")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".cab")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".cmd")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".com")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".exe")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".hta")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".msi")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".pif")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".reg")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".scr")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".vb")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".vbs")) ||
      // Chrome extensions and android APKs are also reported.
      file.MatchesExtension(FILE_PATH_LITERAL(".crx")) ||
      file.MatchesExtension(FILE_PATH_LITERAL(".apk")) ||
      // Archives _may_ contain binaries, we'll check in ExtractFileFeatures.
      IsArchiveFile(file));
}

ClientDownloadRequest::DownloadType GetDownloadType(const FilePath& file) {
  DCHECK(IsBinaryFile(file));
  if (file.MatchesExtension(FILE_PATH_LITERAL(".apk")))
    return ClientDownloadRequest::ANDROID_APK;
  else if (file.MatchesExtension(FILE_PATH_LITERAL(".crx")))
    return ClientDownloadRequest::CHROME_EXTENSION;
  // For zip files, we use the ZIPPED_EXECUTABLE type since we will only send
  // the pingback if we find an executable inside the zip archive.
  else if (file.MatchesExtension(FILE_PATH_LITERAL(".zip")))
    return ClientDownloadRequest::ZIPPED_EXECUTABLE;
  return ClientDownloadRequest::WIN_EXECUTABLE;
}

// List of extensions for which we track some UMA stats.
enum MaliciousExtensionType {
  EXTENSION_EXE,
  EXTENSION_MSI,
  EXTENSION_CAB,
  EXTENSION_SYS,
  EXTENSION_SCR,
  EXTENSION_DRV,
  EXTENSION_BAT,
  EXTENSION_ZIP,
  EXTENSION_RAR,
  EXTENSION_DLL,
  EXTENSION_PIF,
  EXTENSION_COM,
  EXTENSION_JAR,
  EXTENSION_CLASS,
  EXTENSION_PDF,
  EXTENSION_VB,
  EXTENSION_REG,
  EXTENSION_GRP,
  EXTENSION_OTHER,  // Groups all other extensions into one bucket.
  EXTENSION_CRX,
  EXTENSION_APK,
  EXTENSION_MAX,
};

MaliciousExtensionType GetExtensionType(const FilePath& f) {
  if (f.MatchesExtension(FILE_PATH_LITERAL(".exe"))) return EXTENSION_EXE;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".msi"))) return EXTENSION_MSI;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".cab"))) return EXTENSION_CAB;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".sys"))) return EXTENSION_SYS;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".scr"))) return EXTENSION_SCR;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".drv"))) return EXTENSION_DRV;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".bat"))) return EXTENSION_BAT;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".zip"))) return EXTENSION_ZIP;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".rar"))) return EXTENSION_RAR;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".dll"))) return EXTENSION_DLL;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".pif"))) return EXTENSION_PIF;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".com"))) return EXTENSION_COM;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".jar"))) return EXTENSION_JAR;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".class"))) return EXTENSION_CLASS;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".pdf"))) return EXTENSION_PDF;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".vb"))) return EXTENSION_VB;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".reg"))) return EXTENSION_REG;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".grp"))) return EXTENSION_GRP;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".crx"))) return EXTENSION_CRX;
  if (f.MatchesExtension(FILE_PATH_LITERAL(".apk"))) return EXTENSION_APK;
  return EXTENSION_OTHER;
}

void RecordFileExtensionType(const FilePath& file) {
  UMA_HISTOGRAM_ENUMERATION("SBClientDownload.DownloadExtensions",
                            GetExtensionType(file),
                            EXTENSION_MAX);
}

// Enumerate for histogramming purposes.
// DO NOT CHANGE THE ORDERING OF THESE VALUES (different histogram data will
// be mixed together based on their values).
enum SBStatsType {
  DOWNLOAD_URL_CHECKS_TOTAL,
  DOWNLOAD_URL_CHECKS_CANCELED,
  DOWNLOAD_URL_CHECKS_MALWARE,

  DOWNLOAD_HASH_CHECKS_TOTAL,
  DOWNLOAD_HASH_CHECKS_MALWARE,

  // Memory space for histograms is determined by the max.
  // ALWAYS ADD NEW VALUES BEFORE THIS ONE.
  DOWNLOAD_CHECKS_MAX
};
}  // namespace

DownloadProtectionService::DownloadInfo::DownloadInfo()
    : total_bytes(0), user_initiated(false), zipped_executable(false) {}

DownloadProtectionService::DownloadInfo::~DownloadInfo() {}

std::string DownloadProtectionService::DownloadInfo::DebugString() const {
  std::string chain;
  for (size_t i = 0; i < download_url_chain.size(); ++i) {
    chain += download_url_chain[i].spec();
    if (i < download_url_chain.size() - 1) {
      chain += " -> ";
    }
  }
  return base::StringPrintf(
      "DownloadInfo {addr:0x%p, download_url_chain:[%s], local_file:%"
      PRFilePath ", target_file:%" PRFilePath ", referrer_url:%s, "
      "sha256_hash:%s, total_bytes:%" PRId64 ", user_initiated: %s, "
      "zipped_executable: %s}",
      reinterpret_cast<const void*>(this),
      chain.c_str(),
      local_file.value().c_str(),
      target_file.value().c_str(),
      referrer_url.spec().c_str(),
      base::HexEncode(sha256_hash.data(), sha256_hash.size()).c_str(),
      total_bytes,
      user_initiated ? "true" : "false",
      zipped_executable ? "true" : "false");
}

// static
DownloadProtectionService::DownloadInfo
DownloadProtectionService::DownloadInfo::FromDownloadItem(
    const content::DownloadItem& item) {
  DownloadInfo download_info;
  download_info.target_file = item.GetTargetFilePath();
  download_info.sha256_hash = item.GetHash();
  download_info.local_file = item.GetFullPath();
  download_info.download_url_chain = item.GetUrlChain();
  download_info.referrer_url = item.GetReferrerUrl();
  download_info.total_bytes = item.GetTotalBytes();
  download_info.remote_address = item.GetRemoteAddress();
  download_info.user_initiated = item.HasUserGesture();
  return download_info;
}

// Parent SafeBrowsing::Client class used to lookup the bad binary
// URL and digest list.  There are two sub-classes (one for each list).
class DownloadSBClient
    : public SafeBrowsingDatabaseManager::Client,
      public base::RefCountedThreadSafe<DownloadSBClient> {
 public:
  DownloadSBClient(
      const DownloadProtectionService::DownloadInfo& info,
      const DownloadProtectionService::CheckDownloadCallback& callback,
      const scoped_refptr<SafeBrowsingUIManager>& ui_manager,
      SBStatsType total_type,
      SBStatsType dangerous_type)
      : info_(info),
        callback_(callback),
        ui_manager_(ui_manager),
        start_time_(base::TimeTicks::Now()),
        total_type_(total_type),
        dangerous_type_(dangerous_type) {}

  virtual void StartCheck() = 0;
  virtual bool IsDangerous(SBThreatType threat_type) const = 0;

 protected:
  friend class base::RefCountedThreadSafe<DownloadSBClient>;
  virtual ~DownloadSBClient() {}

  void CheckDone(SBThreatType threat_type) {
    DownloadProtectionService::DownloadCheckResult result =
        IsDangerous(threat_type) ?
        DownloadProtectionService::DANGEROUS :
        DownloadProtectionService::SAFE;
    BrowserThread::PostTask(BrowserThread::UI,
                            FROM_HERE,
                            base::Bind(callback_, result));
    UpdateDownloadCheckStats(total_type_);
    if (threat_type != SB_THREAT_TYPE_SAFE) {
      UpdateDownloadCheckStats(dangerous_type_);
      BrowserThread::PostTask(
          BrowserThread::UI,
          FROM_HERE,
          base::Bind(&DownloadSBClient::ReportMalware,
                     this, threat_type));
    }
  }

  void ReportMalware(SBThreatType threat_type) {
    std::string post_data;
    if (!info_.sha256_hash.empty())
      post_data += base::HexEncode(info_.sha256_hash.data(),
                                   info_.sha256_hash.size()) + "\n";
    for (size_t i = 0; i < info_.download_url_chain.size(); ++i) {
      post_data += info_.download_url_chain[i].spec() + "\n";
    }
    ui_manager_->ReportSafeBrowsingHit(
        info_.download_url_chain.back(),  // malicious_url
        info_.download_url_chain.front(), // page_url
        info_.referrer_url,
        true,  // is_subresource
        threat_type,
        post_data);
  }

  void UpdateDownloadCheckStats(SBStatsType stat_type) {
    UMA_HISTOGRAM_ENUMERATION("SB2.DownloadChecks",
                              stat_type,
                              DOWNLOAD_CHECKS_MAX);
  }

  DownloadProtectionService::DownloadInfo info_;
  DownloadProtectionService::CheckDownloadCallback callback_;
  scoped_refptr<SafeBrowsingUIManager> ui_manager_;
  base::TimeTicks start_time_;

 private:
  const SBStatsType total_type_;
  const SBStatsType dangerous_type_;

  DISALLOW_COPY_AND_ASSIGN(DownloadSBClient);
};

class DownloadUrlSBClient : public DownloadSBClient {
 public:
  DownloadUrlSBClient(
      const DownloadProtectionService::DownloadInfo& info,
      const DownloadProtectionService::CheckDownloadCallback& callback,
      const scoped_refptr<SafeBrowsingUIManager>& ui_manager,
      const scoped_refptr<SafeBrowsingDatabaseManager>& database_manager)
      : DownloadSBClient(info, callback, ui_manager,
                         DOWNLOAD_URL_CHECKS_TOTAL,
                         DOWNLOAD_URL_CHECKS_MALWARE),
        database_manager_(database_manager) { }

  virtual void StartCheck() OVERRIDE {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    if (!database_manager_ || database_manager_->CheckDownloadUrl(
            info_.download_url_chain, this)) {
      CheckDone(SB_THREAT_TYPE_SAFE);
    } else {
      AddRef();  // SafeBrowsingService takes a pointer not a scoped_refptr.
    }
  }

  virtual bool IsDangerous(SBThreatType threat_type) const OVERRIDE {
    return threat_type == SB_THREAT_TYPE_BINARY_MALWARE_URL;
  }

  virtual void OnCheckDownloadUrlResult(const std::vector<GURL>& url_chain,
                                        SBThreatType threat_type) OVERRIDE {
    CheckDone(threat_type);
    UMA_HISTOGRAM_TIMES("SB2.DownloadUrlCheckDuration",
                        base::TimeTicks::Now() - start_time_);
    Release();
  }

 protected:
  virtual ~DownloadUrlSBClient() {}

 private:
  scoped_refptr<SafeBrowsingDatabaseManager> database_manager_;

  DISALLOW_COPY_AND_ASSIGN(DownloadUrlSBClient);
};

class DownloadProtectionService::CheckClientDownloadRequest
    : public base::RefCountedThreadSafe<
          DownloadProtectionService::CheckClientDownloadRequest,
          BrowserThread::DeleteOnUIThread>,
      public net::URLFetcherDelegate {
 public:
  CheckClientDownloadRequest(
      const DownloadInfo& info,
      const CheckDownloadCallback& callback,
      DownloadProtectionService* service,
      const scoped_refptr<SafeBrowsingDatabaseManager>& database_manager,
      SignatureUtil* signature_util)
      : info_(info),
        callback_(callback),
        service_(service),
        signature_util_(signature_util),
        database_manager_(database_manager),
        pingback_enabled_(service_->enabled()),
        finished_(false),
        type_(ClientDownloadRequest::WIN_EXECUTABLE),
        ALLOW_THIS_IN_INITIALIZER_LIST(timeout_weakptr_factory_(this)),
        start_time_(base::TimeTicks::Now()) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  }

  void Start() {
    VLOG(2) << "Starting SafeBrowsing download check for: "
            << info_.DebugString();
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    // TODO(noelutz): implement some cache to make sure we don't issue the same
    // request over and over again if a user downloads the same binary multiple
    // times.
    DownloadCheckResultReason reason = REASON_MAX;
    if (!IsSupportedDownload(info_, &reason, &type_)) {
      switch (reason) {
        case REASON_EMPTY_URL_CHAIN:
        case REASON_INVALID_URL:
          RecordImprovedProtectionStats(reason);
          PostFinishTask(SAFE);
          return;

        case REASON_NOT_BINARY_FILE:
          RecordFileExtensionType(info_.target_file);
          RecordImprovedProtectionStats(reason);
          PostFinishTask(SAFE);
          return;

        default:
          // We only expect the reasons explicitly handled above.
          NOTREACHED();
      }
    }
    RecordFileExtensionType(info_.target_file);

    // Compute features from the file contents. Note that we record histograms
    // based on the result, so this runs regardless of whether the pingbacks
    // are enabled.  Since we do blocking I/O, offload this to a worker thread.
    // The task does not need to block shutdown.
    BrowserThread::GetBlockingPool()->PostWorkerTaskWithShutdownBehavior(
        FROM_HERE,
        base::Bind(&CheckClientDownloadRequest::ExtractFileFeatures, this),
        base::SequencedWorkerPool::CONTINUE_ON_SHUTDOWN);
  }

  // Start a timeout to cancel the request if it takes too long.
  // This should only be called after we have finished accessing the file.
  void StartTimeout() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    if (!service_) {
      // Request has already been cancelled.
      return;
    }
    BrowserThread::PostDelayedTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&CheckClientDownloadRequest::Cancel,
                   timeout_weakptr_factory_.GetWeakPtr()),
        base::TimeDelta::FromMilliseconds(
            service_->download_request_timeout_ms()));
  }

  // Canceling a request will cause us to always report the result as SAFE
  // unless a pending request is about to call FinishRequest.
  void Cancel() {
    // Calling FinishRequest might delete this object if we don't keep a
    // reference around until Cancel() is finished running.
    scoped_refptr<CheckClientDownloadRequest> request(this);
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    FinishRequest(SAFE);
    if (fetcher_.get()) {
      // The DownloadProtectionService is going to release its reference, so we
      // might be destroyed before the URLFetcher completes.  Cancel the
      // fetcher so it does not try to invoke OnURLFetchComplete.
      fetcher_.reset();
    }
    // Note: If there is no fetcher, then some callback is still holding a
    // reference to this object.  We'll eventually wind up in some method on
    // the UI thread that will call FinishRequest() again.  If FinishRequest()
    // is called a second time, it will be a no-op.
  }

  // From the net::URLFetcherDelegate interface.
  virtual void OnURLFetchComplete(const net::URLFetcher* source) OVERRIDE {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    DCHECK_EQ(source, fetcher_.get());
    VLOG(2) << "Received a response for URL: "
            << info_.download_url_chain.back() << ": success="
            << source->GetStatus().is_success() << " response_code="
            << source->GetResponseCode();
    DownloadCheckResultReason reason = REASON_SERVER_PING_FAILED;
    DownloadCheckResult result = SAFE;
    if (source->GetStatus().is_success() &&
        net::HTTP_OK == source->GetResponseCode()) {
      ClientDownloadResponse response;
      std::string data;
      bool got_data = source->GetResponseAsString(&data);
      DCHECK(got_data);
      if (!response.ParseFromString(data)) {
        reason = REASON_INVALID_RESPONSE_PROTO;
      } else if (response.verdict() == ClientDownloadResponse::SAFE) {
        reason = REASON_DOWNLOAD_SAFE;
      } else if (service_ && !service_->IsSupportedDownload(info_)) {
        // The client of the download protection service assumes that we don't
        // support this download so we cannot return any other verdict than
        // SAFE even if the server says it's dangerous to download this file.
        // Note: if service_ is NULL we already cancelled the request and
        // returned SAFE.
        reason = REASON_DOWNLOAD_NOT_SUPPORTED;
      } else if (response.verdict() == ClientDownloadResponse::DANGEROUS) {
        reason = REASON_DOWNLOAD_DANGEROUS;
        result = DANGEROUS;
      } else if (response.verdict() == ClientDownloadResponse::UNCOMMON) {
        reason = REASON_DOWNLOAD_UNCOMMON;
        result = UNCOMMON;
      } else {
        LOG(DFATAL) << "Unknown download response verdict: "
                    << response.verdict();
        reason = REASON_INVALID_RESPONSE_VERDICT;
      }
    }
    // We don't need the fetcher anymore.
    fetcher_.reset();
    RecordImprovedProtectionStats(reason);
    UMA_HISTOGRAM_TIMES("SBClientDownload.DownloadRequestDuration",
                        base::TimeTicks::Now() - start_time_);
    FinishRequest(result);
  }

  static bool IsSupportedDownload(const DownloadInfo& info,
                                  DownloadCheckResultReason* reason,
                                  ClientDownloadRequest::DownloadType* type) {
    if (info.download_url_chain.empty()) {
      *reason = REASON_EMPTY_URL_CHAIN;
      return false;
    }
    const GURL& final_url = info.download_url_chain.back();
    if (!final_url.is_valid() || final_url.is_empty() ||
        !final_url.IsStandard() || final_url.SchemeIsFile()) {
      *reason = REASON_INVALID_URL;
      return false;
    }
    if (!IsBinaryFile(info.target_file)) {
      *reason = REASON_NOT_BINARY_FILE;
      return false;
    }
    *type = GetDownloadType(info.target_file);
    return true;
  }

 private:
  friend struct BrowserThread::DeleteOnThread<BrowserThread::UI>;
  friend class base::DeleteHelper<CheckClientDownloadRequest>;

  virtual ~CheckClientDownloadRequest() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  }

  void ExtractFileFeatures() {
    // If we're checking an archive file, look to see if there are any
    // executables inside.  If not, we will skip the pingback for this
    // download.
    if (info_.target_file.MatchesExtension(FILE_PATH_LITERAL(".zip"))) {
      ExtractZipFeatures();
      if (!info_.zipped_executable) {
        RecordImprovedProtectionStats(REASON_ARCHIVE_WITHOUT_BINARIES);
        PostFinishTask(SAFE);
        return;
      }
    } else {
      DCHECK(!IsArchiveFile(info_.target_file));
      ExtractSignatureFeatures();
    }

    // TODO(noelutz): DownloadInfo should also contain the IP address of
    // every URL in the redirect chain.  We also should check whether the
    // download URL is hosted on the internal network.
    BrowserThread::PostTask(
        BrowserThread::IO,
        FROM_HERE,
        base::Bind(&CheckClientDownloadRequest::CheckWhitelists, this));

    // We wait until after the file checks finish to start the timeout, as
    // windows can cause permissions errors if the timeout fired while we were
    // checking the file signature and we tried to complete the download.
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&CheckClientDownloadRequest::StartTimeout, this));
  }

  void ExtractSignatureFeatures() {
    base::TimeTicks start_time = base::TimeTicks::Now();
    signature_util_->CheckSignature(info_.local_file, &signature_info_);
    bool is_signed = (signature_info_.certificate_chain_size() > 0);
    if (is_signed) {
      VLOG(2) << "Downloaded a signed binary: " << info_.local_file.value();
    } else {
      VLOG(2) << "Downloaded an unsigned binary: " << info_.local_file.value();
    }
    UMA_HISTOGRAM_BOOLEAN("SBClientDownload.SignedBinaryDownload", is_signed);
    UMA_HISTOGRAM_TIMES("SBClientDownload.ExtractSignatureFeaturesTime",
                        base::TimeTicks::Now() - start_time);
  }

  void ExtractZipFeatures() {
    base::TimeTicks start_time = base::TimeTicks::Now();
    zip::ZipReader reader;
    bool zip_file_has_archive = false;
    if (reader.Open(info_.local_file)) {
      for (; reader.HasMore(); reader.AdvanceToNextEntry()) {
        if (!reader.OpenCurrentEntryInZip()) {
          VLOG(1) << "Failed to open current entry in zip file: "
                  << info_.local_file.value();
          continue;
        }
        const FilePath& file = reader.current_entry_info()->file_path();
        if (IsBinaryFile(file)) {
          // Don't consider an archived archive to be executable, but record
          // a histogram.
          if (IsArchiveFile(file)) {
            zip_file_has_archive = true;
          } else {
            VLOG(2) << "Downloaded a zipped executable: "
                    << info_.local_file.value();
            info_.zipped_executable = true;
            break;
          }
        } else {
          VLOG(3) << "Ignoring non-binary file: " << file.value();
        }
      }
    } else {
      VLOG(1) << "Failed to open zip file: " << info_.local_file.value();
    }
    UMA_HISTOGRAM_BOOLEAN("SBClientDownload.ZipFileHasExecutable",
                          info_.zipped_executable);
    UMA_HISTOGRAM_BOOLEAN("SBClientDownload.ZipFileHasArchiveButNoExecutable",
                          zip_file_has_archive && !info_.zipped_executable);
    UMA_HISTOGRAM_TIMES("SBClientDownload.ExtractZipFeaturesTime",
                        base::TimeTicks::Now() - start_time);
  }

  void CheckWhitelists() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    DownloadCheckResultReason reason = REASON_MAX;
    if (!database_manager_) {
      reason = REASON_SB_DISABLED;
    } else {
      for (size_t i = 0; i < info_.download_url_chain.size(); ++i) {
        const GURL& url = info_.download_url_chain[i];
        if (url.is_valid() &&
            database_manager_->MatchDownloadWhitelistUrl(url)) {
          VLOG(2) << url << " is on the download whitelist.";
          reason = REASON_WHITELISTED_URL;
          break;
        }
      }
      if (info_.referrer_url.is_valid() && reason == REASON_MAX &&
          database_manager_->MatchDownloadWhitelistUrl(
              info_.referrer_url)) {
        VLOG(2) << "Referrer url " << info_.referrer_url
                << " is on the download whitelist.";
        reason = REASON_WHITELISTED_REFERRER;
      }
      if (reason != REASON_MAX || signature_info_.trusted()) {
        UMA_HISTOGRAM_COUNTS("SBClientDownload.SignedOrWhitelistedDownload", 1);
      }
    }
    if (reason == REASON_MAX && signature_info_.trusted()) {
      for (int i = 0; i < signature_info_.certificate_chain_size(); ++i) {
        if (CertificateChainIsWhitelisted(
                signature_info_.certificate_chain(i))) {
          reason = REASON_TRUSTED_EXECUTABLE;
          break;
        }
      }
    }
    if (reason != REASON_MAX) {
      RecordImprovedProtectionStats(reason);
      PostFinishTask(SAFE);
    } else if (!pingback_enabled_) {
      RecordImprovedProtectionStats(REASON_PING_DISABLED);
      PostFinishTask(SAFE);
    } else {
      // Currently, the UI only works on Windows so we don't even bother
      // with pinging the server if we're not on Windows.  TODO(noelutz):
      // change this code once the UI is done for Linux and Mac.
#if defined(OS_WIN)
      // The URLFetcher is owned by the UI thread, so post a message to
      // start the pingback.
      BrowserThread::PostTask(
          BrowserThread::UI,
          FROM_HERE,
          base::Bind(&CheckClientDownloadRequest::SendRequest, this));
#else
      RecordImprovedProtectionStats(REASON_OS_NOT_SUPPORTED);
      PostFinishTask(SAFE);
#endif
    }
  }

  void SendRequest() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

    // This is our last chance to check whether the request has been canceled
    // before sending it.
    if (!service_) {
      RecordImprovedProtectionStats(REASON_REQUEST_CANCELED);
      FinishRequest(SAFE);
      return;
    }

    ClientDownloadRequest request;
    request.set_url(info_.download_url_chain.back().spec());
    request.mutable_digests()->set_sha256(info_.sha256_hash);
    request.set_length(info_.total_bytes);
    for (size_t i = 0; i < info_.download_url_chain.size(); ++i) {
      ClientDownloadRequest::Resource* resource = request.add_resources();
      resource->set_url(info_.download_url_chain[i].spec());
      if (i == info_.download_url_chain.size() - 1) {
        // The last URL in the chain is the download URL.
        resource->set_type(ClientDownloadRequest::DOWNLOAD_URL);
        resource->set_referrer(info_.referrer_url.spec());
        if (!info_.remote_address.empty()) {
          resource->set_remote_ip(info_.remote_address);
        }
      } else {
        resource->set_type(ClientDownloadRequest::DOWNLOAD_REDIRECT);
      }
      // TODO(noelutz): fill out the remote IP addresses.
    }
    request.set_user_initiated(info_.user_initiated);
    request.set_file_basename(info_.target_file.BaseName().AsUTF8Unsafe());
    request.set_download_type(type_);
    request.mutable_signature()->CopyFrom(signature_info_);
    std::string request_data;
    if (!request.SerializeToString(&request_data)) {
      RecordImprovedProtectionStats(REASON_INVALID_REQUEST_PROTO);
      FinishRequest(SAFE);
      return;
    }

    VLOG(2) << "Sending a request for URL: "
            << info_.download_url_chain.back();
    fetcher_.reset(net::URLFetcher::Create(0 /* ID used for testing */,
                                           GURL(GetDownloadRequestUrl()),
                                           net::URLFetcher::POST,
                                           this));
    fetcher_->SetLoadFlags(net::LOAD_DISABLE_CACHE);
    fetcher_->SetAutomaticallyRetryOn5xx(false);  // Don't retry on error.
    fetcher_->SetRequestContext(service_->request_context_getter_.get());
    fetcher_->SetUploadData("application/octet-stream", request_data);
    fetcher_->Start();
  }

  void PostFinishTask(DownloadCheckResult result) {
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&CheckClientDownloadRequest::FinishRequest, this, result));
  }

  void FinishRequest(DownloadCheckResult result) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    if (finished_) {
      return;
    }
    finished_ = true;
    if (service_) {
      callback_.Run(result);
      DownloadProtectionService* service = service_;
      service_ = NULL;
      service->RequestFinished(this);
      // DownloadProtectionService::RequestFinished will decrement our refcount,
      // so we may be deleted now.
    } else {
      callback_.Run(SAFE);
    }
  }

  void RecordImprovedProtectionStats(DownloadCheckResultReason reason) {
    VLOG(2) << "SafeBrowsing download verdict for: "
            << info_.DebugString() << " verdict:" << reason;
    UMA_HISTOGRAM_ENUMERATION("SBClientDownload.CheckDownloadStats",
                              reason,
                              REASON_MAX);
  }

  bool CertificateChainIsWhitelisted(
      const ClientDownloadRequest_CertificateChain& chain) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    if (chain.element_size() < 2) {
      // We need to have both a signing certificate and its issuer certificate
      // present to construct a whitelist entry.
      return false;
    }
    scoped_refptr<net::X509Certificate> cert =
        net::X509Certificate::CreateFromBytes(
            chain.element(0).certificate().data(),
            chain.element(0).certificate().size());
    if (!cert.get()) {
      return false;
    }

    for (int i = 1; i < chain.element_size(); ++i) {
      scoped_refptr<net::X509Certificate> issuer =
          net::X509Certificate::CreateFromBytes(
              chain.element(i).certificate().data(),
              chain.element(i).certificate().size());
      if (!issuer.get()) {
        return false;
      }
      std::vector<std::string> whitelist_strings;
      DownloadProtectionService::GetCertificateWhitelistStrings(
          *cert, *issuer, &whitelist_strings);
      for (size_t j = 0; j < whitelist_strings.size(); ++j) {
        if (database_manager_->MatchDownloadWhitelistString(
                whitelist_strings[j])) {
          VLOG(2) << "Certificate matched whitelist, cert="
                  << cert->subject().GetDisplayName()
                  << " issuer=" << issuer->subject().GetDisplayName();
          return true;
        }
      }
      cert = issuer;
    }
    return false;
  }

  DownloadInfo info_;
  ClientDownloadRequest_SignatureInfo signature_info_;
  CheckDownloadCallback callback_;
  // Will be NULL if the request has been canceled.
  DownloadProtectionService* service_;
  scoped_refptr<SignatureUtil> signature_util_;
  scoped_refptr<SafeBrowsingDatabaseManager> database_manager_;
  const bool pingback_enabled_;
  scoped_ptr<net::URLFetcher> fetcher_;
  bool finished_;
  ClientDownloadRequest::DownloadType type_;
  base::WeakPtrFactory<CheckClientDownloadRequest> timeout_weakptr_factory_;
  base::TimeTicks start_time_;  // Used for stats.

  DISALLOW_COPY_AND_ASSIGN(CheckClientDownloadRequest);
};

DownloadProtectionService::DownloadProtectionService(
    SafeBrowsingService* sb_service,
    net::URLRequestContextGetter* request_context_getter)
    : request_context_getter_(request_context_getter),
      enabled_(false),
      signature_util_(new SignatureUtil()),
      download_request_timeout_ms_(kDownloadRequestTimeoutMs) {

  if (sb_service) {
    ui_manager_ = sb_service->ui_manager();
    database_manager_ = sb_service->database_manager();
  }
}

DownloadProtectionService::~DownloadProtectionService() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  CancelPendingRequests();
}

void DownloadProtectionService::SetEnabled(bool enabled) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (enabled == enabled_) {
    return;
  }
  enabled_ = enabled;
  if (!enabled_) {
    CancelPendingRequests();
  }
}

void DownloadProtectionService::CheckClientDownload(
    const DownloadProtectionService::DownloadInfo& info,
    const CheckDownloadCallback& callback) {
  scoped_refptr<CheckClientDownloadRequest> request(
      new CheckClientDownloadRequest(info, callback, this,
                                     database_manager_, signature_util_.get()));
  download_requests_.insert(request);
  request->Start();
}

void DownloadProtectionService::CheckDownloadUrl(
    const DownloadProtectionService::DownloadInfo& info,
    const CheckDownloadCallback& callback) {
  DCHECK(!info.download_url_chain.empty());
  scoped_refptr<DownloadUrlSBClient> client(
      new DownloadUrlSBClient(info, callback, ui_manager_, database_manager_));
  // The client will release itself once it is done.
  BrowserThread::PostTask(
        BrowserThread::IO,
        FROM_HERE,
        base::Bind(&DownloadUrlSBClient::StartCheck, client));
}

bool DownloadProtectionService::IsSupportedDownload(
    const DownloadInfo& info) const {
  // Currently, the UI only works on Windows.  On Linux and Mac we still
  // want to show the dangerous file type warning if the file is possibly
  // dangerous which means we have to always return false here.
#if defined(OS_WIN)
  DownloadCheckResultReason reason = REASON_MAX;
  ClientDownloadRequest::DownloadType type =
      ClientDownloadRequest::WIN_EXECUTABLE;
  return (CheckClientDownloadRequest::IsSupportedDownload(info,
                                                          &reason,
                                                          &type) &&
          (ClientDownloadRequest::ANDROID_APK == type ||
           ClientDownloadRequest::WIN_EXECUTABLE == type ||
           ClientDownloadRequest::ZIPPED_EXECUTABLE == type));
#else
  return false;
#endif
}

void DownloadProtectionService::CancelPendingRequests() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  for (std::set<scoped_refptr<CheckClientDownloadRequest> >::iterator it =
           download_requests_.begin();
       it != download_requests_.end();) {
    // We need to advance the iterator before we cancel because canceling
    // the request will invalidate it when RequestFinished is called below.
    scoped_refptr<CheckClientDownloadRequest> tmp = *it++;
    tmp->Cancel();
  }
  DCHECK(download_requests_.empty());
}

void DownloadProtectionService::RequestFinished(
    CheckClientDownloadRequest* request) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  std::set<scoped_refptr<CheckClientDownloadRequest> >::iterator it =
      download_requests_.find(request);
  DCHECK(it != download_requests_.end());
  download_requests_.erase(*it);
}

void DownloadProtectionService::ShowDetailsForDownload(
    const DownloadProtectionService::DownloadInfo& info,
    content::PageNavigator* navigator) {
  navigator->OpenURL(
      content::OpenURLParams(GURL(chrome::kDownloadScanningLearnMoreURL),
                             content::Referrer(),
                             NEW_FOREGROUND_TAB,
                             content::PAGE_TRANSITION_LINK,
                             false));
}

namespace {
// Escapes a certificate attribute so that it can be used in a whitelist
// entry.  Currently, we only escape slashes, since they are used as a
// separator between attributes.
std::string EscapeCertAttribute(const std::string& attribute) {
  std::string escaped;
  for (size_t i = 0; i < attribute.size(); ++i) {
    if (attribute[i] == '%') {
      escaped.append("%25");
    } else if (attribute[i] == '/') {
      escaped.append("%2F");
    } else {
      escaped.push_back(attribute[i]);
    }
  }
  return escaped;
}
}  // namespace

// static
void DownloadProtectionService::GetCertificateWhitelistStrings(
    const net::X509Certificate& certificate,
    const net::X509Certificate& issuer,
    std::vector<std::string>* whitelist_strings) {
  // The whitelist paths are in the format:
  // cert/<ascii issuer fingerprint>[/CN=common_name][/O=org][/OU=unit]
  //
  // Any of CN, O, or OU may be omitted from the whitelist entry, in which
  // case they match anything.  However, the attributes that do appear will
  // always be in the order shown above.  At least one attribute will always
  // be present.

  const net::CertPrincipal& subject = certificate.subject();
  std::vector<std::string> ou_tokens;
  for (size_t i = 0; i < subject.organization_unit_names.size(); ++i) {
    ou_tokens.push_back(
        "/OU=" + EscapeCertAttribute(subject.organization_unit_names[i]));
  }

  std::vector<std::string> o_tokens;
  for (size_t i = 0; i < subject.organization_names.size(); ++i) {
    o_tokens.push_back(
        "/O=" + EscapeCertAttribute(subject.organization_names[i]));
  }

  std::string cn_token;
  if (!subject.common_name.empty()) {
    cn_token = "/CN=" + EscapeCertAttribute(subject.common_name);
  }

  std::set<std::string> paths_to_check;
  if (!cn_token.empty()) {
    paths_to_check.insert(cn_token);
  }
  for (size_t i = 0; i < o_tokens.size(); ++i) {
    paths_to_check.insert(cn_token + o_tokens[i]);
    paths_to_check.insert(o_tokens[i]);
    for (size_t j = 0; j < ou_tokens.size(); ++j) {
      paths_to_check.insert(cn_token + o_tokens[i] + ou_tokens[j]);
      paths_to_check.insert(o_tokens[i] + ou_tokens[j]);
    }
  }
  for (size_t i = 0; i < ou_tokens.size(); ++i) {
    paths_to_check.insert(cn_token + ou_tokens[i]);
    paths_to_check.insert(ou_tokens[i]);
  }

  std::string issuer_fp = base::HexEncode(issuer.fingerprint().data,
                                          sizeof(issuer.fingerprint().data));
  for (std::set<std::string>::iterator it = paths_to_check.begin();
       it != paths_to_check.end(); ++it) {
    whitelist_strings->push_back("cert/" + issuer_fp + *it);
  }
}

// static
std::string DownloadProtectionService::GetDownloadRequestUrl() {
  std::string url = kDownloadRequestUrl;
  std::string api_key = google_apis::GetAPIKey();
  if (!api_key.empty()) {
    base::StringAppendF(&url, "?key=%s",
                        net::EscapeQueryParamValue(api_key, true).c_str());
  }
  return url;
}

}  // namespace safe_browsing
