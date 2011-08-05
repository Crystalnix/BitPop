// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/memory/ref_counted.h"
#include "base/path_service.h"
#include "base/scoped_temp_dir.h"
#include "base/test/test_file_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/download_file_manager.h"
#include "chrome/browser/download/download_item.h"
#include "chrome/browser/download/download_manager.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/download/download_shelf.h"
#include "chrome/browser/history/download_history_info.h"
#include "chrome/browser/history/history.h"
#include "chrome/browser/net/url_request_mock_http_job.h"
#include "chrome/browser/net/url_request_slow_download_job.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/webui/active_downloads_ui.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/in_process_browser_test.h"
#include "chrome/test/ui_test_utils.h"
#include "content/browser/cancelable_request.h"
#include "content/browser/renderer_host/resource_dispatcher_host.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/common/page_transition_types.h"
#include "net/base/net_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// Construction of this class defines a system state, based on some number
// of downloads being seen in a particular state + other events that
// may occur in the download system.  That state will be recorded if it
// occurs at any point after construction.  When that state occurs, the class
// is considered finished.  Callers may either probe for the finished state, or
// wait on it.
//
// TODO(rdsmith): Detect manager going down, remove pointer to
// DownloadManager, transition to finished.  (For right now we
// just use a scoped_refptr<> to keep it around, but that may cause
// timeouts on waiting if a DownloadManager::Shutdown() occurs which
// cancels our in-progress downloads.)
class DownloadsObserver : public DownloadManager::Observer,
                          public DownloadItem::Observer {
 public:
  // Create an object that will be considered finished when |wait_count|
  // download items have entered state |download_finished_state|.
  // If |finish_on_select_file| is true, the object will also be
  // considered finished if the DownloadManager raises a
  // SelectFileDialogDisplayed() notification.

  // TODO(rdsmith): Add option of "dangerous accept/reject dialog" as
  // a unblocking event; if that shows up when you aren't expecting it,
  // it'll result in a hang/timeout as we'll never get to final rename.
  // This probably means rewriting the interface to take a list of events
  // to treat as completion events.
  DownloadsObserver(DownloadManager* download_manager,
                    size_t wait_count,
                    DownloadItem::DownloadState download_finished_state,
                    bool finish_on_select_file)
      : download_manager_(download_manager),
        wait_count_(wait_count),
        finished_downloads_at_construction_(0),
        waiting_(false),
        download_finished_state_(download_finished_state),
        finish_on_select_file_(finish_on_select_file),
        select_file_dialog_seen_(false) {
    download_manager_->AddObserver(this);  // Will call initial ModelChanged().
    finished_downloads_at_construction_ = finished_downloads_.size();
  }

  ~DownloadsObserver() {
    std::set<DownloadItem*>::iterator it = downloads_observed_.begin();
    for (; it != downloads_observed_.end(); ++it) {
      (*it)->RemoveObserver(this);
    }
    download_manager_->RemoveObserver(this);
  }

  // State accessors.
  bool select_file_dialog_seen() { return select_file_dialog_seen_; }

  // Wait for whatever state was specified in the constructor.
  void WaitForFinished() {
    if (!IsFinished()) {
      waiting_ = true;
      ui_test_utils::RunMessageLoop();
      waiting_ = false;
    }
  }

  // Return true if everything's happened that we're configured for.
  bool IsFinished() {
    if (finished_downloads_.size() - finished_downloads_at_construction_
        >= wait_count_)
      return true;
    return (finish_on_select_file_ && select_file_dialog_seen_);
  }

  // DownloadItem::Observer
  virtual void OnDownloadUpdated(DownloadItem* download) {
    if (download->state() == download_finished_state_)
      DownloadInFinalState(download);
  }

  virtual void OnDownloadOpened(DownloadItem* download) {}

  // DownloadManager::Observer
  virtual void ModelChanged() {
    // Regenerate DownloadItem observers.  If there are any download items
    // in our final state, note them in |finished_downloads_|
    // (done by |OnDownloadUpdated()|).
    std::vector<DownloadItem*> downloads;
    download_manager_->SearchDownloads(string16(), &downloads);

    std::vector<DownloadItem*>::iterator it = downloads.begin();
    for (; it != downloads.end(); ++it) {
      OnDownloadUpdated(*it);  // Safe to call multiple times; checks state.

      std::set<DownloadItem*>::const_iterator
          finished_it(finished_downloads_.find(*it));
      std::set<DownloadItem*>::iterator
          observed_it(downloads_observed_.find(*it));

      // If it isn't finished and we're aren't observing it, start.
      if (finished_it == finished_downloads_.end() &&
          observed_it == downloads_observed_.end()) {
        (*it)->AddObserver(this);
        downloads_observed_.insert(*it);
        continue;
      }

      // If it is finished and we are observing it, stop.
      if (finished_it != finished_downloads_.end() &&
          observed_it != downloads_observed_.end()) {
        (*it)->RemoveObserver(this);
        downloads_observed_.erase(observed_it);
        continue;
      }
    }
  }

  virtual void SelectFileDialogDisplayed(int32 /* id */) {
    select_file_dialog_seen_ = true;
    SignalIfFinished();
  }

 private:
  // Called when we know that a download item is in a final state.
  // Note that this is not the same as it first transitioning in to the
  // final state; multiple notifications may occur once the item is in
  // that state.  So we keep our own track of transitions into final.
  void DownloadInFinalState(DownloadItem* download) {
    if (finished_downloads_.find(download) != finished_downloads_.end()) {
      // We've already seen terminal state on this download.
      return;
    }

    // Record the transition.
    finished_downloads_.insert(download);

    SignalIfFinished();
  }

  void SignalIfFinished() {
    if (waiting_ && IsFinished())
      MessageLoopForUI::current()->Quit();
  }

  // The observed download manager.
  scoped_refptr<DownloadManager> download_manager_;

  // The set of DownloadItem's that have transitioned to their finished state
  // since construction of this object.  When the size of this array
  // reaches wait_count_, we're done.
  std::set<DownloadItem*> finished_downloads_;

  // The set of DownloadItem's we are currently observing.  Generally there
  // won't be any overlap with the above; once we see the final state
  // on a DownloadItem, we'll stop observing it.
  std::set<DownloadItem*> downloads_observed_;

  // The number of downloads to wait on completing.
  size_t wait_count_;

  // The number of downloads entered in final state in initial
  // ModelChanged().  We use |finished_downloads_| to track the incoming
  // transitions to final state we should ignore, and to track the
  // number of final state transitions that occurred between
  // construction and return from wait.  But some downloads may be in our
  // final state (and thus be entered into |finished_downloads_|) when we
  // construct this class.  We don't want to count those in our transition
  // to finished.
  int finished_downloads_at_construction_;

  // Whether an internal message loop has been started and must be quit upon
  // all downloads completing.
  bool waiting_;

  // The state on which to consider the DownloadItem finished.
  DownloadItem::DownloadState download_finished_state_;

  // True if we should transition the DownloadsObserver to finished if
  // the select file dialog comes up.
  bool finish_on_select_file_;

  // True if we've seen the select file dialog.
  bool select_file_dialog_seen_;

  DISALLOW_COPY_AND_ASSIGN(DownloadsObserver);
};

// WaitForFlush() returns after:
//      * There are no IN_PROGRESS download items remaining on the
//        DownloadManager.
//      * There have been two round trip messages through the file and
//        IO threads.
// This almost certainly means that a Download cancel has propagated through
// the system.
class DownloadsFlushObserver
    : public DownloadManager::Observer,
      public DownloadItem::Observer,
      public base::RefCountedThreadSafe<DownloadsFlushObserver> {
 public:
  explicit DownloadsFlushObserver(DownloadManager* download_manager)
      : download_manager_(download_manager),
        waiting_for_zero_inprogress_(true) { }

  void WaitForFlush() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    download_manager_->AddObserver(this);
    ui_test_utils::RunMessageLoop();
  }

  // DownloadsManager observer methods.
  virtual void ModelChanged() {
    // Model has changed, so there may be more DownloadItems to observe.
    CheckDownloadsInProgress(true);
  }

  // DownloadItem observer methods.
  virtual void OnDownloadUpdated(DownloadItem* download) {
    // No change in DownloadItem set on manager.
    CheckDownloadsInProgress(false);
  }
  virtual void OnDownloadOpened(DownloadItem* download) {}

 protected:
  friend class base::RefCountedThreadSafe<DownloadsFlushObserver>;

  virtual ~DownloadsFlushObserver() {
    download_manager_->RemoveObserver(this);
    for (std::set<DownloadItem*>::iterator it = downloads_observed_.begin();
         it != downloads_observed_.end(); ++it) {
      (*it)->RemoveObserver(this);
    }
  }

 private:
  // If we're waiting for that flush point, check the number
  // of downloads in the IN_PROGRESS state and take appropriate
  // action.  If requested, also observes all downloads while iterating.
  void CheckDownloadsInProgress(bool observe_downloads) {
    if (waiting_for_zero_inprogress_) {
      int count = 0;

      std::vector<DownloadItem*> downloads;
      download_manager_->SearchDownloads(string16(), &downloads);
      std::vector<DownloadItem*>::iterator it = downloads.begin();
      for (; it != downloads.end(); ++it) {
        if ((*it)->state() == DownloadItem::IN_PROGRESS)
          count++;
        if (observe_downloads) {
          if (downloads_observed_.find(*it) == downloads_observed_.end()) {
            (*it)->AddObserver(this);
          }
          // Download items are forever, and we don't want to make
          // assumptions about future state transitions, so once we
          // start observing them, we don't stop until destruction.
        }
      }

      if (count == 0) {
        waiting_for_zero_inprogress_ = false;
        // Stop observing DownloadItems.  We maintain the observation
        // of DownloadManager so that we don't have to independently track
        // whether we are observing it for conditional destruction.
        for (std::set<DownloadItem*>::iterator it = downloads_observed_.begin();
             it != downloads_observed_.end(); ++it) {
          (*it)->RemoveObserver(this);
        }
        downloads_observed_.clear();

        // Trigger next step.  We need to go past the IO thread twice, as
        // there's a self-task posting in the IO thread cancel path.
        BrowserThread::PostTask(
            BrowserThread::FILE, FROM_HERE,
            NewRunnableMethod(this,
                              &DownloadsFlushObserver::PingFileThread, 2));
      }
    }
  }

  void PingFileThread(int cycle) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        NewRunnableMethod(this, &DownloadsFlushObserver::PingIOThread,
                          cycle));
  }

  void PingIOThread(int cycle) {
    if (--cycle) {
      BrowserThread::PostTask(
          BrowserThread::UI, FROM_HERE,
          NewRunnableMethod(this, &DownloadsFlushObserver::PingFileThread,
                            cycle));
    } else {
      BrowserThread::PostTask(
          BrowserThread::UI, FROM_HERE, new MessageLoop::QuitTask());
    }
  }

  DownloadManager* download_manager_;
  std::set<DownloadItem*> downloads_observed_;
  bool waiting_for_zero_inprogress_;

  DISALLOW_COPY_AND_ASSIGN(DownloadsFlushObserver);
};

// Collect the information from FILE and IO threads needed for the Cancel
// Test, specifically the number of outstanding requests on the
// ResourceDispatcherHost and the number of pending downloads on the
// DownloadFileManager.
class CancelTestDataCollector
    : public base::RefCountedThreadSafe<CancelTestDataCollector> {
 public:
  CancelTestDataCollector()
      : resource_dispatcher_host_(
          g_browser_process->resource_dispatcher_host()),
        rdh_pending_requests_(0),
        dfm_pending_downloads_(0) { }

  void WaitForDataCollected() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        NewRunnableMethod(this, &CancelTestDataCollector::IOInfoCollector));
    ui_test_utils::RunMessageLoop();
  }

  int rdh_pending_requests() { return rdh_pending_requests_; }
  int dfm_pending_downloads() { return dfm_pending_downloads_; }

 protected:
  friend class base::RefCountedThreadSafe<CancelTestDataCollector>;

  virtual ~CancelTestDataCollector() {}

 private:

  void IOInfoCollector() {
    download_file_manager_ = resource_dispatcher_host_->download_file_manager();
    rdh_pending_requests_ = resource_dispatcher_host_->pending_requests();
    BrowserThread::PostTask(
        BrowserThread::FILE, FROM_HERE,
        NewRunnableMethod(this, &CancelTestDataCollector::FileInfoCollector));
  }

  void FileInfoCollector() {
    dfm_pending_downloads_ = download_file_manager_->NumberOfActiveDownloads();
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE, new MessageLoop::QuitTask());
  }

  ResourceDispatcherHost* resource_dispatcher_host_;
  DownloadFileManager* download_file_manager_;
  int rdh_pending_requests_;
  int dfm_pending_downloads_;

  DISALLOW_COPY_AND_ASSIGN(CancelTestDataCollector);
};

class DownloadTest : public InProcessBrowserTest {
 public:
  enum SelectExpectation {
    EXPECT_NO_SELECT_DIALOG = -1,
    EXPECT_NOTHING,
    EXPECT_SELECT_DIALOG
  };

  DownloadTest() {
    EnableDOMAutomation();
  }

  // Returning false indicates a failure of the setup, and should be asserted
  // in the caller.
  virtual bool InitialSetup(bool prompt_for_download) {
    bool have_test_dir = PathService::Get(chrome::DIR_TEST_DATA, &test_dir_);
    EXPECT_TRUE(have_test_dir);
    if (!have_test_dir)
      return false;

    // Sanity check default values for window / tab count and shelf visibility.
    int window_count = BrowserList::size();
    EXPECT_EQ(1, window_count);
    EXPECT_EQ(1, browser()->tab_count());
    EXPECT_FALSE(browser()->window()->IsDownloadShelfVisible());

    // Set up the temporary download folder.
    bool created_downloads_dir = CreateAndSetDownloadsDirectory(browser());
    EXPECT_TRUE(created_downloads_dir);
    if (!created_downloads_dir)
      return false;
    browser()->profile()->GetPrefs()->SetBoolean(prefs::kPromptForDownload,
                                                 prompt_for_download);

    DownloadManager* manager = browser()->profile()->GetDownloadManager();
    manager->download_prefs()->ResetAutoOpen();
    manager->RemoveAllDownloads();

    return true;
  }

 protected:

  enum SizeTestType {
    SIZE_TEST_TYPE_KNOWN,
    SIZE_TEST_TYPE_UNKNOWN,
  };

  // Location of the file source (the place from which it is downloaded).
  FilePath OriginFile(FilePath file) {
    return test_dir_.Append(file);
  }

  // Location of the file destination (place to which it is downloaded).
  FilePath DestinationFile(Browser* browser, FilePath file) {
    return GetDownloadDirectory(browser).Append(file);
  }

  // Must be called after browser creation.  Creates a temporary
  // directory for downloads that is auto-deleted on destruction.
  // Returning false indicates a failure of the function, and should be asserted
  // in the caller.
  bool CreateAndSetDownloadsDirectory(Browser* browser) {
    if (!browser)
      return false;

    if (!downloads_directory_.CreateUniqueTempDir())
      return false;

    browser->profile()->GetPrefs()->SetFilePath(
        prefs::kDownloadDefaultDirectory,
        downloads_directory_.path());

    return true;
  }

  DownloadPrefs* GetDownloadPrefs(Browser* browser) {
    return browser->profile()->GetDownloadManager()->download_prefs();
  }

  FilePath GetDownloadDirectory(Browser* browser) {
    DownloadManager* download_mananger =
        browser->profile()->GetDownloadManager();
    return download_mananger->download_prefs()->download_path();
  }

  // Create a DownloadsObserver that will wait for the
  // specified number of downloads to finish.
  DownloadsObserver* CreateWaiter(Browser* browser, int num_downloads) {
    DownloadManager* download_manager =
        browser->profile()->GetDownloadManager();
    return new DownloadsObserver(
        download_manager, num_downloads,
        DownloadItem::COMPLETE,          // Really done
        true);                           // Bail on select file
  }

  // Create a DownloadsObserver that will wait for the
  // specified number of downloads to start.
  DownloadsObserver* CreateInProgressWaiter(Browser* browser,
                                            int num_downloads) {
    DownloadManager* download_manager =
        browser->profile()->GetDownloadManager();
    return new DownloadsObserver(
        download_manager, num_downloads,
        DownloadItem::IN_PROGRESS,      // Has started
        true);                          // Bail on select file
  }

  // Download |url|, then wait for the download to finish.
  // |disposition| indicates where the navigation occurs (current tab, new
  // foreground tab, etc).
  // |expectation| indicates whether or not a Select File dialog should be
  // open when the download is finished, or if we don't care.
  // If the dialog appears, the routine exits.  The only effect |expectation|
  // has is whether or not the test succeeds.
  // |browser_test_flags| indicate what to wait for, and is an OR of 0 or more
  // values in the ui_test_utils::BrowserTestWaitFlags enum.
  void DownloadAndWaitWithDisposition(Browser* browser,
                                      const GURL& url,
                                      WindowOpenDisposition disposition,
                                      SelectExpectation expectation,
                                      int browser_test_flags) {
    // Setup notification, navigate, and block.
    scoped_ptr<DownloadsObserver> observer(CreateWaiter(browser, 1));
    // This call will block until the condition specified by
    // |browser_test_flags|, but will not wait for the download to finish.
    ui_test_utils::NavigateToURLWithDisposition(browser,
                                                url,
                                                disposition,
                                                browser_test_flags);
    // Waits for the download to complete.
    observer->WaitForFinished();

    // If specified, check the state of the select file dialog.
    if (expectation != EXPECT_NOTHING) {
      EXPECT_EQ(expectation == EXPECT_SELECT_DIALOG,
                observer->select_file_dialog_seen());
    }
  }

  // Download a file in the current tab, then wait for the download to finish.
  void DownloadAndWait(Browser* browser,
                       const GURL& url,
                       SelectExpectation expectation) {
    DownloadAndWaitWithDisposition(
        browser,
        url,
        CURRENT_TAB,
        expectation,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  }

  // Should only be called when the download is known to have finished
  // (in error or not).
  // Returning false indicates a failure of the function, and should be asserted
  // in the caller.
  bool CheckDownload(Browser* browser,
                     const FilePath& downloaded_filename,
                     const FilePath& origin_filename) {
    // Find the path to which the data will be downloaded.
    FilePath downloaded_file(DestinationFile(browser, downloaded_filename));

    // Find the origin path (from which the data comes).
    FilePath origin_file(OriginFile(origin_filename));
    bool origin_file_exists = file_util::PathExists(origin_file);
    EXPECT_TRUE(origin_file_exists);
    if (!origin_file_exists)
      return false;

    // Confirm the downloaded data file exists.
    bool downloaded_file_exists = file_util::PathExists(downloaded_file);
    EXPECT_TRUE(downloaded_file_exists);
    if (!downloaded_file_exists)
      return false;

    int64 origin_file_size = 0;
    int64 downloaded_file_size = 0;
    EXPECT_TRUE(file_util::GetFileSize(origin_file, &origin_file_size));
    EXPECT_TRUE(file_util::GetFileSize(downloaded_file, &downloaded_file_size));
    EXPECT_EQ(origin_file_size, downloaded_file_size);
    EXPECT_TRUE(file_util::ContentsEqual(downloaded_file, origin_file));

    // Delete the downloaded copy of the file.
    bool downloaded_file_deleted =
        file_util::DieFileDie(downloaded_file, false);
    EXPECT_TRUE(downloaded_file_deleted);
    return downloaded_file_deleted;
  }

  bool RunSizeTest(Browser* browser,
                   SizeTestType type,
                   const std::string& partial_indication,
                   const std::string& total_indication) {
    if (!InitialSetup(false))
      return false;

    EXPECT_TRUE(type == SIZE_TEST_TYPE_UNKNOWN || type == SIZE_TEST_TYPE_KNOWN);
    if (type != SIZE_TEST_TYPE_KNOWN && type != SIZE_TEST_TYPE_UNKNOWN)
      return false;
    GURL url(type == SIZE_TEST_TYPE_KNOWN ?
             URLRequestSlowDownloadJob::kKnownSizeUrl :
             URLRequestSlowDownloadJob::kUnknownSizeUrl);

  // TODO(ahendrickson) -- |expected_title_in_progress| and
  // |expected_title_finished| need to be checked.
    FilePath filename;
    net::FileURLToFilePath(url, &filename);
    string16 expected_title_in_progress(
        ASCIIToUTF16(partial_indication) + filename.LossyDisplayName());
    string16 expected_title_finished(
        ASCIIToUTF16(total_indication) + filename.LossyDisplayName());

    // Download a partial web page in a background tab and wait.
    // The mock system will not complete until it gets a special URL.
    scoped_ptr<DownloadsObserver> observer(CreateWaiter(browser, 1));
    ui_test_utils::NavigateToURL(browser, url);

    // TODO(ahendrickson): check download status text before downloading.
    // Need to:
    //  - Add a member function to the |DownloadShelf| interface class, that
    //    indicates how many members it has.
    //  - Add a member function to |DownloadShelf| to get the status text
    //    of a given member (for example, via the name in |DownloadItemView|'s
    //    GetAccessibleState() member function), by index.
    //  - Iterate over browser->window()->GetDownloadShelf()'s members
    //    to see if any match the status text we want.  Start with the last one.

    // Allow the request to finish.  We do this by loading a second URL in a
    // separate tab.
    GURL finish_url(URLRequestSlowDownloadJob::kFinishDownloadUrl);
    ui_test_utils::NavigateToURLWithDisposition(
        browser,
        finish_url,
        NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
    observer->WaitForFinished();

    EXPECT_EQ(2, browser->tab_count());

    // TODO(ahendrickson): check download status text after downloading.

    // Make sure the download shelf is showing.
    CheckDownloadUIVisible(browser, true, true);

    FilePath basefilename(filename.BaseName());
    net::FileURLToFilePath(url, &filename);
    FilePath download_path = downloads_directory_.path().Append(basefilename);

    bool downloaded_path_exists = file_util::PathExists(download_path);
    EXPECT_TRUE(downloaded_path_exists);
    if (!downloaded_path_exists)
      return false;

    // Delete the file we just downloaded.
    EXPECT_TRUE(file_util::DieFileDie(download_path, true));
    EXPECT_FALSE(file_util::PathExists(download_path));

    return true;
  }

  void GetDownloads(Browser* browser, std::vector<DownloadItem*>* downloads) {
    DCHECK(downloads);
    DownloadManager* manager = browser->profile()->GetDownloadManager();
    manager->SearchDownloads(string16(), downloads);
  }

  // Figure out if the appropriate download visibility was done.  A
  // utility function to support ChromeOS variations.
  static void CheckDownloadUIVisible(Browser* browser,
                                     bool expected_non_chromeos,
                                     bool expected_chromeos) {
#if defined(OS_CHROMEOS)
    EXPECT_EQ(expected_chromeos,
              NULL != ActiveDownloadsUI::GetPopup(browser->profile()));
#else
    EXPECT_EQ(expected_non_chromeos,
              browser->window()->IsDownloadShelfVisible());
#endif
  }

  static void ExpectWindowCountAfterDownload(size_t expected) {
#if defined(OS_CHROMEOS)
    // On ChromeOS, a download panel is created to display
    // download information, and this counts as a window.
    expected++;
#endif
    EXPECT_EQ(expected, BrowserList::size());
  }

 private:
  // Location of the test data.
  FilePath test_dir_;

  // Location of the downloads directory for these tests
  ScopedTempDir downloads_directory_;
};

// Get History Information.
class DownloadsHistoryDataCollector {
 public:
  explicit DownloadsHistoryDataCollector(int64 download_db_handle,
                                         DownloadManager* manager)
      : result_valid_(false),
        download_db_handle_(download_db_handle) {
    HistoryService* hs =
        manager->profile()->GetHistoryService(Profile::EXPLICIT_ACCESS);
    DCHECK(hs);
    hs->QueryDownloads(
        &callback_consumer_,
        NewCallback(this,
                    &DownloadsHistoryDataCollector::OnQueryDownloadsComplete));

    // Cannot complete immediately because the history backend runs on a
    // separate thread, so we can assume that the RunMessageLoop below will
    // be exited by the Quit in OnQueryDownloadsComplete.
    ui_test_utils::RunMessageLoop();
  }

  bool GetDownloadsHistoryEntry(DownloadHistoryInfo* result) {
    DCHECK(result);
    *result = result_;
    return result_valid_;
  }

 private:
  void OnQueryDownloadsComplete(
      std::vector<DownloadHistoryInfo>* entries) {
    result_valid_ = false;
    for (std::vector<DownloadHistoryInfo>::const_iterator it = entries->begin();
         it != entries->end(); ++it) {
      if (it->db_handle == download_db_handle_) {
        result_ = *it;
        result_valid_ = true;
      }
    }
    MessageLoopForUI::current()->Quit();
  }

  DownloadHistoryInfo result_;
  bool result_valid_;
  int64 download_db_handle_;
  CancelableRequestConsumer callback_consumer_;

  DISALLOW_COPY_AND_ASSIGN(DownloadsHistoryDataCollector);
};

}  // namespace

// While an object of this class exists, it will mock out download
// opening for all downloads created on the specified download manager.
class MockDownloadOpeningObserver : public DownloadManager::Observer {
 public:
  explicit MockDownloadOpeningObserver(DownloadManager* manager)
      : download_manager_(manager) {
    download_manager_->AddObserver(this);
  }

  ~MockDownloadOpeningObserver() {
    download_manager_->RemoveObserver(this);
  }

  // DownloadManager::Observer
  virtual void ModelChanged() {
    std::vector<DownloadItem*> downloads;
    download_manager_->SearchDownloads(string16(), &downloads);

    for (std::vector<DownloadItem*>::iterator it = downloads.begin();
         it != downloads.end(); ++it) {
      (*it)->TestMockDownloadOpen();
    }
  }

 private:
  DownloadManager* download_manager_;

  DISALLOW_COPY_AND_ASSIGN(MockDownloadOpeningObserver);
};

// NOTES:
//
// Files for these tests are found in DIR_TEST_DATA (currently
// "chrome\test\data\", see chrome_paths.cc).
// Mock responses have extension .mock-http-headers appended to the file name.

// Download a file due to the associated MIME type.
IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadMimeType) {
  ASSERT_TRUE(InitialSetup(false));
  FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file));

  // Download the file and wait.  We do not expect the Select File dialog.
  DownloadAndWait(browser(), url, EXPECT_NO_SELECT_DIALOG);

  // Check state.
  EXPECT_EQ(1, browser()->tab_count());
  CheckDownload(browser(), file, file);
  CheckDownloadUIVisible(browser(), true, true);
}

#if defined(OS_WIN)
// Download a file and confirm that the zone identifier (on windows)
// is set to internet.
IN_PROC_BROWSER_TEST_F(DownloadTest, CheckInternetZone) {
  ASSERT_TRUE(InitialSetup(false));
  FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file));

  // Download the file and wait.  We do not expect the Select File dialog.
  DownloadAndWait(browser(), url, EXPECT_NO_SELECT_DIALOG);

  // Check state.  Special file state must be checked before CheckDownload,
  // as CheckDownload will delete the output file.
  EXPECT_EQ(1, browser()->tab_count());
  FilePath downloaded_file(DestinationFile(browser(), file));
  if (file_util::VolumeSupportsADS(downloaded_file))
    EXPECT_TRUE(file_util::HasInternetZoneIdentifier(downloaded_file));
  CheckDownload(browser(), file, file);
  CheckDownloadUIVisible(browser(), true, true);
}
#endif

// Put up a Select File dialog when the file is downloaded, due to its MIME
// type.
//
// This test runs correctly, but leaves behind turds in the test user's
// download directory because of http://crbug.com/62099.  No big loss; it
// was primarily confirming DownloadsObserver wait on select file dialog
// functionality anyway.
IN_PROC_BROWSER_TEST_F(DownloadTest, DISABLED_DownloadMimeTypeSelect) {
  ASSERT_TRUE(InitialSetup(true));
  FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file));

  // Download the file and wait.  We expect the Select File dialog to appear
  // due to the MIME type.
  DownloadAndWait(browser(), url, EXPECT_SELECT_DIALOG);

  // Check state.
  EXPECT_EQ(1, browser()->tab_count());
  // Since we exited while the Select File dialog was visible, there should not
  // be anything in the download shelf and so it should not be visible.
  CheckDownloadUIVisible(browser(), false, false);
}

// Access a file with a viewable mime-type, verify that a download
// did not initiate.
IN_PROC_BROWSER_TEST_F(DownloadTest, NoDownload) {
  ASSERT_TRUE(InitialSetup(false));
  FilePath file(FILE_PATH_LITERAL("download-test2.html"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file));
  FilePath file_path(DestinationFile(browser(), file));

  // Open a web page and wait.
  ui_test_utils::NavigateToURL(browser(), url);

  // Check that we did not download the web page.
  EXPECT_FALSE(file_util::PathExists(file_path));

  // Check state.
  EXPECT_EQ(1, browser()->tab_count());
  CheckDownloadUIVisible(browser(), false, false);
}

// Download a 0-size file with a content-disposition header, verify that the
// download tab opened and the file exists as the filename specified in the
// header.  This also ensures we properly handle empty file downloads.
// The download shelf should be visible in the current tab.
IN_PROC_BROWSER_TEST_F(DownloadTest, ContentDisposition) {
  ASSERT_TRUE(InitialSetup(false));
  FilePath file(FILE_PATH_LITERAL("download-test3.gif"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file));
  FilePath download_file(FILE_PATH_LITERAL("download-test3-attachment.gif"));

  // Download a file and wait.
  DownloadAndWait(browser(), url, EXPECT_NO_SELECT_DIALOG);

  CheckDownload(browser(), download_file, file);

  // Check state.
  EXPECT_EQ(1, browser()->tab_count());
  CheckDownloadUIVisible(browser(), true, true);
}

#if !defined(OS_CHROMEOS)  // Download shelf is not per-window on ChromeOS.
// Test that the download shelf is per-window by starting a download in one
// tab, opening a second tab, closing the shelf, going back to the first tab,
// and checking that the shelf is closed.
IN_PROC_BROWSER_TEST_F(DownloadTest, PerWindowShelf) {
  ASSERT_TRUE(InitialSetup(false));
  FilePath file(FILE_PATH_LITERAL("download-test3.gif"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file));
  FilePath download_file(FILE_PATH_LITERAL("download-test3-attachment.gif"));

  // Download a file and wait.
  DownloadAndWait(browser(), url, EXPECT_NO_SELECT_DIALOG);

  CheckDownload(browser(), download_file, file);

  // Check state.
  EXPECT_EQ(1, browser()->tab_count());
  CheckDownloadUIVisible(browser(), true, true);

  // Open a second tab and wait.
  EXPECT_NE(static_cast<TabContentsWrapper*>(NULL),
            browser()->AddSelectedTabWithURL(GURL(), PageTransition::TYPED));
  EXPECT_EQ(2, browser()->tab_count());
  CheckDownloadUIVisible(browser(),  true, true);

  // Hide the download shelf.
  browser()->window()->GetDownloadShelf()->Close();
  CheckDownloadUIVisible(browser(), false, false);

  // Go to the first tab.
  browser()->ActivateTabAt(0, true);
  EXPECT_EQ(2, browser()->tab_count());

  // The download shelf should not be visible.
  CheckDownloadUIVisible(browser(), false, false);
}
#endif  // !OS_CHROMEOS

// UnknownSize and KnownSize are tests which depend on
// URLRequestSlowDownloadJob to serve content in a certain way. Data will be
// sent in two chunks where the first chunk is 35K and the second chunk is 10K.
// The test will first attempt to download a file; but the server will "pause"
// in the middle until the server receives a second request for
// "download-finish".  At that time, the download will finish.
// These tests don't currently test much due to holes in |RunSizeTest()|.  See
// comments in that routine for details.
IN_PROC_BROWSER_TEST_F(DownloadTest, UnknownSize) {
  ASSERT_TRUE(RunSizeTest(browser(), SIZE_TEST_TYPE_UNKNOWN,
                          "32.0 KB - ", "100% - "));
}

IN_PROC_BROWSER_TEST_F(DownloadTest, KnownSize) {
  ASSERT_TRUE(RunSizeTest(browser(), SIZE_TEST_TYPE_KNOWN,
                          "71% - ", "100% - "));
}

// Test that when downloading an item in Incognito mode, we don't crash when
// closing the last Incognito window (http://crbug.com/13983).
// Also check that the download shelf is not visible after closing the
// Incognito window.
IN_PROC_BROWSER_TEST_F(DownloadTest, IncognitoDownload) {
  ASSERT_TRUE(InitialSetup(false));

  // Open an Incognito window.
  Browser* incognito = CreateIncognitoBrowser();  // Waits.
  ASSERT_TRUE(incognito);
  int window_count = BrowserList::size();
  EXPECT_EQ(2, window_count);

  // Download a file in the Incognito window and wait.
  CreateAndSetDownloadsDirectory(incognito);
  FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file));
  // Since |incognito| is a separate browser, we have to set it up explicitly.
  incognito->profile()->GetPrefs()->SetBoolean(prefs::kPromptForDownload,
                                               false);
  DownloadAndWait(incognito, url, EXPECT_NO_SELECT_DIALOG);

  // We should still have 2 windows.
  ExpectWindowCountAfterDownload(2);

  // Verify that the download shelf is showing for the Incognito window.
  CheckDownloadUIVisible(incognito, true, true);

#if !defined(OS_MACOSX)
  // On Mac OS X, the UI window close is delayed until the outermost
  // message loop runs.  So it isn't possible to get a BROWSER_CLOSED
  // notification inside of a test.
  ui_test_utils::WindowedNotificationObserver signal(
      NotificationType::BROWSER_CLOSED,
      Source<Browser>(incognito));
#endif

  // Close the Incognito window and don't crash.
  incognito->CloseWindow();

#if !defined(OS_MACOSX)
  signal.Wait();
  ExpectWindowCountAfterDownload(1);
#endif

  // Verify that the regular window does not have a download shelf.
  // On ChromeOS, the download panel is common to both profiles, so
  // it is still visible.
  CheckDownloadUIVisible(browser(), false, true);

  CheckDownload(browser(), file, file);
}

// Navigate to a new background page, but don't download.  Confirm that the
// download shelf is not visible and that we have two tabs.
IN_PROC_BROWSER_TEST_F(DownloadTest, DontCloseNewTab1) {
  ASSERT_TRUE(InitialSetup(false));
  // Because it's an HTML link, it should open a web page rather than
  // downloading.
  FilePath file1(FILE_PATH_LITERAL("download-test2.html"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file1));

  // Open a web page and wait.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(),
      url,
      NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  // We should have two tabs now.
  EXPECT_EQ(2, browser()->tab_count());
  CheckDownloadUIVisible(browser(), false, false);
}

// Download a file in a background tab. Verify that the tab is closed
// automatically, and that the download shelf is visible in the current tab.
IN_PROC_BROWSER_TEST_F(DownloadTest, CloseNewTab1) {
  ASSERT_TRUE(InitialSetup(false));

  // Download a file in a new background tab and wait.  The tab is automatically
  // closed when the download begins.
  FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file));
  DownloadAndWaitWithDisposition(
      browser(),
      url,
      NEW_BACKGROUND_TAB,
      EXPECT_NO_SELECT_DIALOG,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  // When the download finishes, we should still have one tab.
  CheckDownloadUIVisible(browser(), true, true);
  EXPECT_EQ(1, browser()->tab_count());

  CheckDownload(browser(), file, file);
}

// Open a web page in the current tab, then download a file in another tab via
// a Javascript call.
// Verify that we have 2 tabs, and the download shelf is visible in the current
// tab.
//
// The download_page1.html page contains an openNew() function that opens a
// tab and then downloads download-test1.lib.
IN_PROC_BROWSER_TEST_F(DownloadTest, DontCloseNewTab2) {
  ASSERT_TRUE(InitialSetup(false));
  // Because it's an HTML link, it should open a web page rather than
  // downloading.
  FilePath file1(FILE_PATH_LITERAL("download_page1.html"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file1));

  // Open a web page and wait.
  ui_test_utils::NavigateToURL(browser(), url);

  // Download a file in a new tab and wait (via Javascript).
  FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  DownloadAndWaitWithDisposition(browser(),
                                 GURL("javascript:openNew()"),
                                 CURRENT_TAB,
                                 EXPECT_NO_SELECT_DIALOG,
                                 ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB);

  // When the download finishes, we should have two tabs.
  CheckDownloadUIVisible(browser(), true, true);
  EXPECT_EQ(2, browser()->tab_count());

  CheckDownload(browser(), file, file);
}

// Open a web page in the current tab, open another tab via a Javascript call,
// then download a file in the new tab.
// Verify that we have 2 tabs, and the download shelf is visible in the current
// tab.
//
// The download_page2.html page contains an openNew() function that opens a
// tab.
IN_PROC_BROWSER_TEST_F(DownloadTest, DontCloseNewTab3) {
  ASSERT_TRUE(InitialSetup(false));
  // Because it's an HTML link, it should open a web page rather than
  // downloading.
  FilePath file1(FILE_PATH_LITERAL("download_page2.html"));
  GURL url1(URLRequestMockHTTPJob::GetMockUrl(file1));

  // Open a web page and wait.
  ui_test_utils::NavigateToURL(browser(), url1);

  // Open a new tab and wait.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(),
      GURL("javascript:openNew()"),
      CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB);

  EXPECT_EQ(2, browser()->tab_count());

  // Download a file and wait.
  FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file));
  DownloadAndWaitWithDisposition(browser(),
                                 url,
                                 CURRENT_TAB,
                                 EXPECT_NO_SELECT_DIALOG,
                                 ui_test_utils::BROWSER_TEST_NONE);

  // When the download finishes, we should have two tabs.
  CheckDownloadUIVisible(browser(), true, true);
  EXPECT_EQ(2, browser()->tab_count());

  CheckDownload(browser(), file, file);
}

// Open a web page in the current tab, then download a file via Javascript,
// which will do so in a temporary tab.
// Verify that we have 1 tab, and the download shelf is visible.
//
// The download_page3.html page contains an openNew() function that opens a
// tab with download-test1.lib in the URL.  When the URL is determined to be
// a download, the tab is closed automatically.
IN_PROC_BROWSER_TEST_F(DownloadTest, CloseNewTab2) {
  ASSERT_TRUE(InitialSetup(false));
  // Because it's an HTML link, it should open a web page rather than
  // downloading.
  FilePath file1(FILE_PATH_LITERAL("download_page3.html"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file1));

  // Open a web page and wait.
  ui_test_utils::NavigateToURL(browser(), url);

  // Download a file and wait.
  // The file to download is "download-test1.lib".
  FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  DownloadAndWaitWithDisposition(browser(),
                                 GURL("javascript:openNew()"),
                                 CURRENT_TAB,
                                 EXPECT_NO_SELECT_DIALOG,
                                 ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB);

  // When the download finishes, we should still have one tab.
  CheckDownloadUIVisible(browser(), true, true);
  EXPECT_EQ(1, browser()->tab_count());

  CheckDownload(browser(), file, file);
}

// Open a web page in the current tab, then call Javascript via a button to
// download a file in a new tab, which is closed automatically when the
// download begins.
// Verify that we have 1 tab, and the download shelf is visible.
//
// The download_page4.html page contains a form with download-test1.lib as the
// action.
IN_PROC_BROWSER_TEST_F(DownloadTest, CloseNewTab3) {
  ASSERT_TRUE(InitialSetup(false));
  // Because it's an HTML link, it should open a web page rather than
  // downloading.
  FilePath file1(FILE_PATH_LITERAL("download_page4.html"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file1));

  // Open a web page and wait.
  ui_test_utils::NavigateToURL(browser(), url);

  // Download a file in a new tab and wait.  The tab will automatically close
  // when the download begins.
  // The file to download is "download-test1.lib".
  FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  DownloadAndWaitWithDisposition(
      browser(),
      GURL("javascript:document.getElementById('form').submit()"),
      CURRENT_TAB,
      EXPECT_NO_SELECT_DIALOG,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB);

  // When the download finishes, we should still have one tab.
  CheckDownloadUIVisible(browser(), true, true);
  EXPECT_EQ(1, browser()->tab_count());

  CheckDownload(browser(), file, file);
}

// Download a file in a new window.
// Verify that we have 2 windows, and the download shelf is not visible in the
// first window, but is visible in the second window.
// Close the new window.
// Verify that we have 1 window, and the download shelf is not visible.
//
// Regression test for http://crbug.com/44454
IN_PROC_BROWSER_TEST_F(DownloadTest, NewWindow) {
  ASSERT_TRUE(InitialSetup(false));
  FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file));
#if !defined(OS_MACOSX)
  // See below.
  Browser* first_browser = browser();
#endif

  // Download a file in a new window and wait.
  DownloadAndWaitWithDisposition(browser(),
                                 url,
                                 NEW_WINDOW,
                                 EXPECT_NO_SELECT_DIALOG,
                                 ui_test_utils::BROWSER_TEST_NONE);

  // When the download finishes, the download shelf SHOULD NOT be visible in
  // the first window.
  ExpectWindowCountAfterDownload(2);
  EXPECT_EQ(1, browser()->tab_count());
  // Except on Chrome OS, where the download window sticks around.
  CheckDownloadUIVisible(browser(), false, true);

  // The download shelf SHOULD be visible in the second window.
  std::set<Browser*> original_browsers;
  original_browsers.insert(browser());
  Browser* download_browser =
      ui_test_utils::GetBrowserNotInSet(original_browsers);
  ASSERT_TRUE(download_browser != NULL);
  EXPECT_NE(download_browser, browser());
  EXPECT_EQ(1, download_browser->tab_count());
  CheckDownloadUIVisible(download_browser, true, true);

#if !defined(OS_MACOSX)
  // On Mac OS X, the UI window close is delayed until the outermost
  // message loop runs.  So it isn't possible to get a BROWSER_CLOSED
  // notification inside of a test.
  ui_test_utils::WindowedNotificationObserver signal(
      NotificationType::BROWSER_CLOSED,
      Source<Browser>(download_browser));
#endif

  // Close the new window.
  download_browser->CloseWindow();

#if !defined(OS_MACOSX)
  signal.Wait();
  EXPECT_EQ(first_browser, browser());
  ExpectWindowCountAfterDownload(1);
#endif

  EXPECT_EQ(1, browser()->tab_count());
  // On ChromeOS, the popup sticks around.
  CheckDownloadUIVisible(browser(), false, true);

  CheckDownload(browser(), file, file);
}

IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadCancelled) {
  ASSERT_TRUE(InitialSetup(false));
  EXPECT_EQ(1, browser()->tab_count());

  // TODO(rdsmith): Fragile code warning!  The code below relies on the
  // DownloadsObserver only finishing when the new download has reached
  // the state of being entered into the history and being user-visible
  // (that's what's required for the Remove to be valid and for the
  // download shelf to be visible).  By the pure semantics of
  // DownloadsObserver, that's not guaranteed; DownloadItems are created
  // in the IN_PROGRESS state and made known to the DownloadManager
  // immediately, so any ModelChanged event on the DownloadManager after
  // navigation would allow the observer to return.  However, the only
  // ModelChanged() event the code will currently fire is in
  // OnCreateDownloadEntryComplete, at which point the download item will
  // be in the state we need.
  // The right way to fix this is to create finer grained states on the
  // DownloadItem, and wait for the state that indicates the item has been
  // entered in the history and made visible in the UI.

  // Create a download, wait until it's started, and confirm
  // we're in the expected state.
  scoped_ptr<DownloadsObserver> observer(CreateInProgressWaiter(browser(), 1));
  ui_test_utils::NavigateToURL(
      browser(), GURL(URLRequestSlowDownloadJob::kUnknownSizeUrl));
  observer->WaitForFinished();

  std::vector<DownloadItem*> downloads;
  browser()->profile()->GetDownloadManager()->SearchDownloads(
      string16(), &downloads);
  ASSERT_EQ(1u, downloads.size());
  ASSERT_EQ(DownloadItem::IN_PROGRESS, downloads[0]->state());
  CheckDownloadUIVisible(browser(), true, true);

  // Cancel the download and wait for download system quiesce.
  downloads[0]->Delete(DownloadItem::DELETE_DUE_TO_USER_DISCARD);
  scoped_refptr<DownloadsFlushObserver> flush_observer(
      new DownloadsFlushObserver(browser()->profile()->GetDownloadManager()));
  flush_observer->WaitForFlush();

  // Get the important info from other threads and check it.
  scoped_refptr<CancelTestDataCollector> info(new CancelTestDataCollector());
  info->WaitForDataCollected();
  EXPECT_EQ(0, info->rdh_pending_requests());
  EXPECT_EQ(0, info->dfm_pending_downloads());

  // Using "DownloadItem::Remove" follows the discard dangerous download path,
  // which completely removes the browser from the shelf and closes the shelf
  // if it was there.  Chrome OS is an exception to this, where if we
  // bring up the downloads panel, it stays there.
  CheckDownloadUIVisible(browser(), false, true);
}

// Confirm a download makes it into the history properly.
IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadHistoryCheck) {
  ASSERT_TRUE(InitialSetup(false));
  FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file));
  FilePath origin_file(OriginFile(file));
  int64 origin_size;
  file_util::GetFileSize(origin_file, &origin_size);

  // Download the file and wait.  We do not expect the Select File dialog.
  DownloadAndWait(browser(), url, EXPECT_NO_SELECT_DIALOG);

  // Get details of what downloads have just happened.
  std::vector<DownloadItem*> downloads;
  GetDownloads(browser(), &downloads);
  ASSERT_EQ(1u, downloads.size());
  int64 db_handle = downloads[0]->db_handle();

  // Check state.
  EXPECT_EQ(1, browser()->tab_count());
  CheckDownload(browser(), file, file);
  CheckDownloadUIVisible(browser(), true, true);

  // Check history results.
  DownloadsHistoryDataCollector history_collector(
      db_handle,
      browser()->profile()->GetDownloadManager());
  DownloadHistoryInfo info;
  EXPECT_TRUE(history_collector.GetDownloadsHistoryEntry(&info)) << db_handle;
  EXPECT_EQ(file, info.path.BaseName());
  EXPECT_EQ(url, info.url);
  // Ignore start_time.
  EXPECT_EQ(origin_size, info.received_bytes);
  EXPECT_EQ(origin_size, info.total_bytes);
  EXPECT_EQ(DownloadItem::COMPLETE, info.state);
}

// Test for crbug.com/14505. This tests that chrome:// urls are still functional
// after download of a file while viewing another chrome://.
IN_PROC_BROWSER_TEST_F(DownloadTest, ChromeURLAfterDownload) {
  ASSERT_TRUE(InitialSetup(false));
  FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  GURL download_url(URLRequestMockHTTPJob::GetMockUrl(file));
  GURL flags_url(chrome::kAboutFlagsURL);
  GURL extensions_url(chrome::kChromeUIExtensionsURL);

  ui_test_utils::NavigateToURL(browser(), flags_url);
  DownloadAndWait(browser(), download_url, EXPECT_NO_SELECT_DIALOG);
  ui_test_utils::NavigateToURL(browser(), extensions_url);
  TabContents* contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(contents);
  bool webui_responded = false;
  EXPECT_TRUE(ui_test_utils::ExecuteJavaScriptAndExtractBool(
      contents->render_view_host(),
      L"",
      L"window.domAutomationController.send(window.webui_responded_);",
      &webui_responded));
  EXPECT_TRUE(webui_responded);
}

// Test for crbug.com/12745. This tests that if a download is initiated from
// a chrome:// page that has registered and onunload handler, the browser
// will be able to close.
// After several correct executions, this test starts failing on the build
// bots and then continues to fail consistently.
// As of 2011/05/22, it's crashing, so it is getting disabled.
// http://crbug.com/82278
IN_PROC_BROWSER_TEST_F(DownloadTest, DISABLED_BrowserCloseAfterDownload) {
  GURL downloads_url(chrome::kAboutFlagsURL);
  FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  GURL download_url(URLRequestMockHTTPJob::GetMockUrl(file));

  ui_test_utils::NavigateToURL(browser(), downloads_url);
  TabContents* contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(contents);
  bool result = false;
  EXPECT_TRUE(ui_test_utils::ExecuteJavaScriptAndExtractBool(
      contents->render_view_host(),
      L"",
      L"window.onunload = function() { var do_nothing = 0; }; "
      L"window.domAutomationController.send(true);",
      &result));
  EXPECT_TRUE(result);

  DownloadAndWait(browser(), download_url, EXPECT_NO_SELECT_DIALOG);

  ui_test_utils::WindowedNotificationObserver signal(
      NotificationType::BROWSER_CLOSED,
      Source<Browser>(browser()));
  browser()->CloseWindow();
  signal.Wait();
}

// Test to make sure auto-open works.
IN_PROC_BROWSER_TEST_F(DownloadTest, AutoOpen) {
  ASSERT_TRUE(InitialSetup(false));
  FilePath file(FILE_PATH_LITERAL("download-autoopen.txt"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl(file));

  ASSERT_TRUE(
      GetDownloadPrefs(browser())->EnableAutoOpenBasedOnExtension(file));

  // Mokc out external opening on all downloads until end of test.
  MockDownloadOpeningObserver observer(
      browser()->profile()->GetDownloadManager());

  DownloadAndWait(browser(), url, EXPECT_NO_SELECT_DIALOG);

  // Find the download and confirm it was opened.
  std::vector<DownloadItem*> downloads;
  browser()->profile()->GetDownloadManager()->SearchDownloads(
      string16(), &downloads);
  ASSERT_EQ(1u, downloads.size());
  EXPECT_EQ(DownloadItem::COMPLETE, downloads[0]->state());
  EXPECT_TRUE(downloads[0]->opened());

  // As long as we're here, confirmed everything else is good.
  EXPECT_EQ(1, browser()->tab_count());
  CheckDownload(browser(), file, file);
  // Dissapears on most UIs, but the download panel sticks around for
  // chrome os.
  CheckDownloadUIVisible(browser(), false, true);
}
