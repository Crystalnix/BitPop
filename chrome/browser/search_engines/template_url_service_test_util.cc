// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/search_engines/template_url_service_test_util.h"

#include "base/bind.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/scoped_temp_dir.h"
#include "base/threading/thread.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/notification_service.h"
#include "content/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;

namespace {

// A callback used to coordinate when the database has finished processing
// requests. See note in BlockTillServiceProcessesRequests for details.
//
// Schedules a QuitClosure on the message loop it was created with.
void QuitCallback(MessageLoop* message_loop) {
  message_loop->PostTask(FROM_HERE, MessageLoop::QuitClosure());
}

// Blocks the caller until thread has finished servicing all pending
// requests.
static void WaitForThreadToProcessRequests(BrowserThread::ID identifier) {
  // Schedule a task on the thread that is processed after all
  // pending requests on the thread.
  BrowserThread::PostTask(identifier, FROM_HERE,
                          base::Bind(&QuitCallback, MessageLoop::current()));
  MessageLoop::current()->Run();
}

}  // namespace

// Subclass the TestingProfile so that it can return a WebDataService.
class TemplateURLServiceTestingProfile : public TestingProfile {
 public:
  TemplateURLServiceTestingProfile()
      : TestingProfile(),
        db_thread_(BrowserThread::DB),
        io_thread_(BrowserThread::IO) {
  }

  void SetUp();
  void TearDown();

  // Starts the I/O thread. This isn't done automatically because not every test
  // needs this.
  void StartIOThread() {
    io_thread_.StartIOThread();
  }

  virtual WebDataService* GetWebDataService(ServiceAccessType access) {
    return service_.get();
  }

 private:
  scoped_refptr<WebDataService> service_;
  ScopedTempDir temp_dir_;
  content::TestBrowserThread db_thread_;
  content::TestBrowserThread io_thread_;
};

// Trivial subclass of TemplateURLService that records the last invocation of
// SetKeywordSearchTermsForURL.
class TestingTemplateURLService : public TemplateURLService {
 public:
  static ProfileKeyedService* Build(Profile* profile) {
    return new TestingTemplateURLService(profile);
  }

  explicit TestingTemplateURLService(Profile* profile)
      : TemplateURLService(profile) {
  }

  string16 GetAndClearSearchTerm() {
    string16 search_term;
    search_term.swap(search_term_);
    return search_term;
  }

 protected:
  virtual void SetKeywordSearchTermsForURL(const TemplateURL* t_url,
                                           const GURL& url,
                                           const string16& term) {
    search_term_ = term;
  }

 private:
  string16 search_term_;

  DISALLOW_COPY_AND_ASSIGN(TestingTemplateURLService);
};

void TemplateURLServiceTestingProfile::SetUp() {
  db_thread_.Start();

  // Make unique temp directory.
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

  FilePath path = temp_dir_.path().AppendASCII("TestDataService.db");
  service_ = new WebDataService;
  ASSERT_TRUE(service_->InitWithPath(path));
}

void TemplateURLServiceTestingProfile::TearDown() {
  // Clear the request context so it will get deleted. This should be done
  // before shutting down the I/O thread to avoid memory leaks.
  ResetRequestContext();

  // Wait for the delete of the request context to happen.
  if (io_thread_.IsRunning())
    TemplateURLServiceTestUtil::BlockTillIOThreadProcessesRequests();

  // The I/O thread must be shutdown before the DB thread.
  io_thread_.Stop();

  // Clean up the test directory.
  if (service_.get())
    service_->Shutdown();
  // Note that we must ensure the DB thread is stopped after WDS
  // shutdown (so it can commit pending transactions) but before
  // deleting the test profile directory, otherwise we may not be
  // able to delete it due to an open transaction.
  db_thread_.Stop();
}

TemplateURLServiceTestUtil::TemplateURLServiceTestUtil()
    : ui_thread_(BrowserThread::UI, &message_loop_),
      changed_count_(0) {
}

TemplateURLServiceTestUtil::~TemplateURLServiceTestUtil() {
}

void TemplateURLServiceTestUtil::SetUp() {
  profile_.reset(new TemplateURLServiceTestingProfile());
  profile_->SetUp();
  TemplateURLService* service = static_cast<TemplateURLService*>(
      TemplateURLServiceFactory::GetInstance()->SetTestingFactoryAndUse(
          profile_.get(), TestingTemplateURLService::Build));
  service->AddObserver(this);
}

void TemplateURLServiceTestUtil::TearDown() {
  if (profile_.get()) {
    profile_->TearDown();
    profile_.reset();
  }
  TemplateURLRef::SetGoogleBaseURL(NULL);

  // Flush the message loop to make application verifiers happy.
  message_loop_.RunAllPending();
}

void TemplateURLServiceTestUtil::OnTemplateURLServiceChanged() {
  changed_count_++;
}

int TemplateURLServiceTestUtil::GetObserverCount() {
  return changed_count_;
}

void TemplateURLServiceTestUtil::ResetObserverCount() {
  changed_count_ = 0;
}

void TemplateURLServiceTestUtil::BlockTillServiceProcessesRequests() {
  WaitForThreadToProcessRequests(BrowserThread::DB);
}

void TemplateURLServiceTestUtil::BlockTillIOThreadProcessesRequests() {
  WaitForThreadToProcessRequests(BrowserThread::IO);
}

void TemplateURLServiceTestUtil::VerifyLoad() {
  ASSERT_FALSE(model()->loaded());
  model()->Load();
  BlockTillServiceProcessesRequests();
  EXPECT_EQ(1, GetObserverCount());
  ResetObserverCount();
}

void TemplateURLServiceTestUtil::ChangeModelToLoadState() {
  model()->ChangeToLoadedState();
  // Initialize the web data service so that the database gets updated with
  // any changes made.
  model()->service_ = profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
}

void TemplateURLServiceTestUtil::ClearModel() {
  TemplateURLServiceFactory::GetInstance()->SetTestingFactory(
      profile_.get(), NULL);
}

void TemplateURLServiceTestUtil::ResetModel(bool verify_load) {
  TemplateURLServiceFactory::GetInstance()->SetTestingFactoryAndUse(
      profile_.get(), TestingTemplateURLService::Build);
  model()->AddObserver(this);
  changed_count_ = 0;
  if (verify_load)
    VerifyLoad();
}

string16 TemplateURLServiceTestUtil::GetAndClearSearchTerm() {
  return
      static_cast<TestingTemplateURLService*>(model())->GetAndClearSearchTerm();
}

void TemplateURLServiceTestUtil::SetGoogleBaseURL(
    const std::string& base_url) const {
  TemplateURLRef::SetGoogleBaseURL(new std::string(base_url));
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_GOOGLE_URL_UPDATED,
      content::NotificationService::AllSources(),
      content::NotificationService::NoDetails());
}

WebDataService* TemplateURLServiceTestUtil::GetWebDataService() {
  return profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
}

TemplateURLService* TemplateURLServiceTestUtil::model() const {
  return TemplateURLServiceFactory::GetForProfile(profile());
}

TestingProfile* TemplateURLServiceTestUtil::profile() const {
  return profile_.get();
}

void TemplateURLServiceTestUtil::StartIOThread() {
  profile_->StartIOThread();
}
