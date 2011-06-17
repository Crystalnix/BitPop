// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This test creates a fake safebrowsing service, where we can inject
// malware and phishing urls.  It then uses a real browser to go to
// these urls, and sends "goback" or "proceed" commands and verifies
// they work.

#include "chrome/browser/browser_process.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/safe_browsing/malware_details.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/browser/safe_browsing/safe_browsing_blocking_page.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/in_process_browser_test.h"
#include "chrome/test/ui_test_utils.h"
#include "content/browser/browser_thread.h"
#include "content/browser/renderer_host/render_process_host.h"
#include "content/browser/renderer_host/resource_dispatcher_host.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/browser/tab_contents/tab_contents_view.h"

// A SafeBrowingService class that allows us to inject the malicious URLs.
class FakeSafeBrowsingService :  public SafeBrowsingService {
 public:
  FakeSafeBrowsingService() {}

  virtual ~FakeSafeBrowsingService() {}

  // Called on the IO thread to check if the given url is safe or not.  If we
  // can synchronously determine that the url is safe, CheckUrl returns true.
  // Otherwise it returns false, and "client" is called asynchronously with the
  // result when it is ready.
  // Overrides SafeBrowsingService::CheckBrowseUrl.
  virtual bool CheckBrowseUrl(const GURL& gurl, Client* client) {
    if (badurls[gurl.spec()] == SAFE)
      return true;

    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        NewRunnableMethod(this, &FakeSafeBrowsingService::OnCheckBrowseURLDone,
                          gurl, client));
    return false;
  }

  void OnCheckBrowseURLDone(const GURL& gurl, Client* client) {
    SafeBrowsingService::SafeBrowsingCheck check;
    check.urls.push_back(gurl);
    check.client = client;
    check.result = badurls[gurl.spec()];
    client->OnSafeBrowsingResult(check);
  }

  void AddURLResult(const GURL& url, UrlCheckResult checkresult) {
    badurls[url.spec()] = checkresult;
  }

  // Overrides SafeBrowsingService.
  virtual void SendSerializedMalwareDetails(const std::string& serialized) {
    reports_.push_back(serialized);
    // Notify the UI thread that we got a report.
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this,
                          &FakeSafeBrowsingService::OnMalwareDetailsDone));
  }

  void OnMalwareDetailsDone() {
    EXPECT_TRUE(BrowserThread::CurrentlyOn(BrowserThread::UI));
    MessageLoopForUI::current()->Quit();
  }

  std::string GetReport() {
    EXPECT_TRUE(reports_.size() == 1);
    return reports_[0];
  }

  std::vector<std::string> reports_;

 private:
  base::hash_map<std::string, UrlCheckResult> badurls;
};

// Factory that creates FakeSafeBrowsingService instances.
class TestSafeBrowsingServiceFactory : public SafeBrowsingServiceFactory {
 public:
  TestSafeBrowsingServiceFactory() { }
  virtual ~TestSafeBrowsingServiceFactory() { }

  virtual SafeBrowsingService* CreateSafeBrowsingService() {
    return new FakeSafeBrowsingService();
  }
};

// A MalwareDetails class lets us intercept calls from the renderer.
class FakeMalwareDetails : public MalwareDetails {
 public:
  FakeMalwareDetails(SafeBrowsingService* sb_service,
                     TabContents* tab_contents,
                     const SafeBrowsingService::UnsafeResource& unsafe_resource)
      : MalwareDetails(sb_service, tab_contents, unsafe_resource) { }

  virtual ~FakeMalwareDetails() {}

  virtual void AddDOMDetails(
      const std::vector<SafeBrowsingHostMsg_MalwareDOMDetails_Node>& params) {
    EXPECT_TRUE(BrowserThread::CurrentlyOn(BrowserThread::IO));
    MalwareDetails::AddDOMDetails(params);

    // Notify the UI thread that we got the dom details.
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            NewRunnableMethod(this,
                            &FakeMalwareDetails::OnDOMDetailsDone));
  }

  void OnDOMDetailsDone() {
    got_dom_ = true;
    if (waiting_) {
      MessageLoopForUI::current()->Quit();
    }
  }

  bool got_dom() const {
    return got_dom_;
  }

  bool waiting() const {
    return waiting_;
  }

  void set_got_dom(bool got_dom) {
    got_dom_ = got_dom;
  }

  void set_waiting(bool waiting) {
    waiting_ = waiting;
  }

  safe_browsing::ClientMalwareReportRequest* get_report() {
    return report_.get();
  }

 private:
  // Some logic to figure out if we should wait for the dom details or not.
  // These variables should only be accessed in the UI thread.
  bool got_dom_;
  bool waiting_;

};

class TestMalwareDetailsFactory : public MalwareDetailsFactory {
 public:
  TestMalwareDetailsFactory() { }
  virtual ~TestMalwareDetailsFactory() { }

  virtual MalwareDetails* CreateMalwareDetails(
      SafeBrowsingService* sb_service,
      TabContents* tab_contents,
      const SafeBrowsingService::UnsafeResource& unsafe_resource) {
    details_ = new FakeMalwareDetails(sb_service, tab_contents,
                                      unsafe_resource);
    return details_;
  }

  FakeMalwareDetails* get_details() {
    return details_;
  }

 private:
  FakeMalwareDetails* details_;
};

// A SafeBrowingBlockingPage class that lets us wait until it's hidden.
class TestSafeBrowsingBlockingPage :  public SafeBrowsingBlockingPage {
 public:
  TestSafeBrowsingBlockingPage(SafeBrowsingService* service,
                               TabContents* tab_contents,
                               const UnsafeResourceList& unsafe_resources)
      : SafeBrowsingBlockingPage(service, tab_contents, unsafe_resources) {
    wait_for_delete_ = false;
  }

  ~TestSafeBrowsingBlockingPage() {
    if (wait_for_delete_) {
      // Notify that we are gone
      MessageLoopForUI::current()->Quit();
    }
  }

  void set_wait_for_delete() {
    wait_for_delete_ = true;
  }

 private:
  bool wait_for_delete_;
};

class TestSafeBrowsingBlockingPageFactory
    : public SafeBrowsingBlockingPageFactory {
 public:
  TestSafeBrowsingBlockingPageFactory() { }
  ~TestSafeBrowsingBlockingPageFactory() { }

  virtual SafeBrowsingBlockingPage* CreateSafeBrowsingPage(
      SafeBrowsingService* service,
      TabContents* tab_contents,
      const SafeBrowsingBlockingPage::UnsafeResourceList& unsafe_resources) {
    return new TestSafeBrowsingBlockingPage(service, tab_contents,
                                            unsafe_resources);
  }
};

// Tests the safe browsing blocking page in a browser.
class SafeBrowsingBlockingPageTest : public InProcessBrowserTest,
                                     public SafeBrowsingService::Client {
 public:
  SafeBrowsingBlockingPageTest() {
  }

  virtual void SetUp() {
    SafeBrowsingService::RegisterFactory(&factory_);
    SafeBrowsingBlockingPage::RegisterFactory(&blocking_page_factory_);
    MalwareDetails::RegisterFactory(&details_factory_);
    InProcessBrowserTest::SetUp();
  }

  virtual void TearDown() {
    InProcessBrowserTest::TearDown();
    SafeBrowsingBlockingPage::RegisterFactory(NULL);
    SafeBrowsingService::RegisterFactory(NULL);
    MalwareDetails::RegisterFactory(NULL);
  }

  virtual void SetUpInProcessBrowserTestFixture() {
    ASSERT_TRUE(test_server()->Start());
  }

  // SafeBrowsingService::Client implementation.
  virtual void OnSafeBrowsingResult(
      const SafeBrowsingService::SafeBrowsingCheck& check) {
  }

  virtual void OnBlockingPageComplete(bool proceed) {
  }

  void AddURLResult(const GURL& url,
                    SafeBrowsingService::UrlCheckResult checkresult) {
    FakeSafeBrowsingService* service =
        static_cast<FakeSafeBrowsingService*>(
            g_browser_process->resource_dispatcher_host()->
            safe_browsing_service());

    ASSERT_TRUE(service);
    service->AddURLResult(url, checkresult);
  }

  void SendCommand(const std::string& command) {
    TabContents* contents = browser()->GetSelectedTabContents();
    // We use InterstitialPage::GetInterstitialPage(tab) instead of
    // tab->interstitial_page() because the tab doesn't have a pointer
    // to its interstital page until it gets a command from the renderer
    // that it has indeed displayed it -- and this sometimes happens after
    // NavigateToURL returns.
    SafeBrowsingBlockingPage* interstitial_page =
        static_cast<SafeBrowsingBlockingPage*>(
            InterstitialPage::GetInterstitialPage(contents));
    ASSERT_TRUE(interstitial_page);
    interstitial_page->CommandReceived(command);
  }

  void DontProceedThroughInterstitial() {
    TabContents* contents = browser()->GetSelectedTabContents();
    InterstitialPage* interstitial_page = InterstitialPage::GetInterstitialPage(
        contents);
    ASSERT_TRUE(interstitial_page);
    interstitial_page->DontProceed();
  }

  void ProceedThroughInterstitial() {
    TabContents* contents = browser()->GetSelectedTabContents();
    InterstitialPage* interstitial_page = InterstitialPage::GetInterstitialPage(
        contents);
    ASSERT_TRUE(interstitial_page);
    interstitial_page->Proceed();
  }

  void AssertNoInterstitial(bool wait_for_delete) {
    TabContents* contents = browser()->GetSelectedTabContents();

    if (contents->showing_interstitial_page() && wait_for_delete) {
      // We'll get notified when the interstitial is deleted.
      static_cast<TestSafeBrowsingBlockingPage*>(
          contents->interstitial_page())->set_wait_for_delete();
      ui_test_utils::RunMessageLoop();
    }

    // Can't use InterstitialPage::GetInterstitialPage() because that
    // gets updated after the TestSafeBrowsingBlockingPage destructor
    ASSERT_FALSE(contents->showing_interstitial_page());
  }

  bool YesInterstitial() {
    TabContents* contents = browser()->GetSelectedTabContents();
    InterstitialPage* interstitial_page = InterstitialPage::GetInterstitialPage(
        contents);
    return interstitial_page != NULL;
  }

  void WaitForInterstitial() {
    TabContents* contents = browser()->GetSelectedTabContents();
    if (!InterstitialPage::GetInterstitialPage(contents))
      ui_test_utils::WaitForNotificationFrom(
          NotificationType::INTERSTITIAL_ATTACHED,
          Source<TabContents>(contents));
  }

  void WaitForNavigation() {
    NavigationController* controller =
        &browser()->GetSelectedTabContents()->controller();
    ui_test_utils::WaitForNavigation(controller);
  }

  void AssertReportSent() {
    // When a report is scheduled in the IO thread we should get notified.
    ui_test_utils::RunMessageLoop();

    FakeSafeBrowsingService* service =
        static_cast<FakeSafeBrowsingService*>(
            g_browser_process->resource_dispatcher_host()->
            safe_browsing_service());

    std::string serialized = service->GetReport();

    safe_browsing::ClientMalwareReportRequest report;
    ASSERT_TRUE(report.ParseFromString(serialized));

    // Verify the report is complete.
    EXPECT_TRUE(report.complete());
  }

  void MalwareRedirectCancelAndProceed(const std::string open_function);

 protected:
  TestMalwareDetailsFactory details_factory_;

 private:
  TestSafeBrowsingServiceFactory factory_;
  TestSafeBrowsingBlockingPageFactory blocking_page_factory_;

  DISALLOW_COPY_AND_ASSIGN(SafeBrowsingBlockingPageTest);
};

void SafeBrowsingBlockingPageTest::MalwareRedirectCancelAndProceed(
    const std::string open_function) {
  GURL load_url = test_server()->GetURL(
      "files/safe_browsing/interstitial_cancel.html");
  GURL malware_url("http://localhost/files/safe_browsing/malware.html");
  AddURLResult(malware_url, SafeBrowsingService::URL_MALWARE);

  // Load the test page.
  ui_test_utils::NavigateToURL(browser(), load_url);
  // Trigger the safe browsing interstitial page via a redirect in "openWin()".
  ui_test_utils::NavigateToURLWithDisposition(
      browser(),
      GURL("javascript:" + open_function + "()"),
      CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB);
  WaitForInterstitial();
  // Cancel the redirect request while interstitial page is open.
  browser()->ActivateTabAt(0, true);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(),
      GURL("javascript:stopWin()"),
      CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  browser()->ActivateTabAt(1, true);
  // Simulate the user clicking "proceed",  there should be no crash.
  SendCommand("\"proceed\"");
}

namespace {

const char kEmptyPage[] = "files/empty.html";
const char kMalwarePage[] = "files/safe_browsing/malware.html";
const char kMalwareIframe[] = "files/safe_browsing/malware_iframe.html";

IN_PROC_BROWSER_TEST_F(SafeBrowsingBlockingPageTest,
                       MalwareRedirectInIFrameCanceled) {
  // 1. Test the case that redirect is a subresource.
  MalwareRedirectCancelAndProceed("openWinIFrame");
  // If the redirect was from subresource but canceled, "proceed" will continue
  // with the rest of resources.
  AssertNoInterstitial(true);
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingBlockingPageTest, MalwareRedirectCanceled) {
  // 2. Test the case that redirect is the only resource.
  MalwareRedirectCancelAndProceed("openWin");
  // Clicking proceed won't do anything if the main request is cancelled
  // already.  See crbug.com/76460.
  EXPECT_TRUE(YesInterstitial());
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingBlockingPageTest, MalwareDontProceed) {
  GURL url = test_server()->GetURL(kEmptyPage);
  AddURLResult(url, SafeBrowsingService::URL_MALWARE);

  ui_test_utils::NavigateToURL(browser(), url);

  SendCommand("\"takeMeBack\"");   // Simulate the user clicking "back"
  AssertNoInterstitial(false);   // Assert the interstitial is gone
  EXPECT_EQ(GURL(chrome::kAboutBlankURL),   // Back to "about:blank"
            browser()->GetSelectedTabContents()->GetURL());
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingBlockingPageTest, MalwareProceed) {
  GURL url = test_server()->GetURL(kEmptyPage);
  AddURLResult(url, SafeBrowsingService::URL_MALWARE);

  ui_test_utils::NavigateToURL(browser(), url);
  SendCommand("\"proceed\"");    // Simulate the user clicking "proceed"
  WaitForNavigation();    // Wait until we finish the navigation.
  AssertNoInterstitial(true);    // Assert the interstitial is gone.
  EXPECT_EQ(url, browser()->GetSelectedTabContents()->GetURL());
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingBlockingPageTest, PhishingDontProceed) {
  GURL url = test_server()->GetURL(kEmptyPage);
  AddURLResult(url, SafeBrowsingService::URL_PHISHING);

  ui_test_utils::NavigateToURL(browser(), url);

  SendCommand("\"takeMeBack\"");   // Simulate the user clicking "proceed"
  AssertNoInterstitial(false);    // Assert the interstitial is gone
  EXPECT_EQ(GURL(chrome::kAboutBlankURL),  // We are back to "about:blank".
            browser()->GetSelectedTabContents()->GetURL());
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingBlockingPageTest, PhishingProceed) {
  GURL url = test_server()->GetURL(kEmptyPage);
  AddURLResult(url, SafeBrowsingService::URL_PHISHING);

  ui_test_utils::NavigateToURL(browser(), url);

  SendCommand("\"proceed\"");   // Simulate the user clicking "proceed".
  WaitForNavigation();    // Wait until we finish the navigation.
  AssertNoInterstitial(true);    // Assert the interstitial is gone
  EXPECT_EQ(url, browser()->GetSelectedTabContents()->GetURL());
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingBlockingPageTest, PhishingReportError) {
  GURL url = test_server()->GetURL(kEmptyPage);
  AddURLResult(url, SafeBrowsingService::URL_PHISHING);

  ui_test_utils::NavigateToURL(browser(), url);

  SendCommand("\"reportError\"");   // Simulate the user clicking "report error"
  WaitForNavigation();    // Wait until we finish the navigation.
  AssertNoInterstitial(false);    // Assert the interstitial is gone

  // We are in the error reporting page.
  EXPECT_EQ("/safebrowsing/report_error/",
            browser()->GetSelectedTabContents()->GetURL().path());
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingBlockingPageTest, PhishingLearnMore) {
  GURL url = test_server()->GetURL(kEmptyPage);
  AddURLResult(url, SafeBrowsingService::URL_PHISHING);

  ui_test_utils::NavigateToURL(browser(), url);

  SendCommand("\"learnMore\"");   // Simulate the user clicking "learn more"
  WaitForNavigation();    // Wait until we finish the navigation.
  AssertNoInterstitial(false);    // Assert the interstitial is gone

  // We are in the help page.
  EXPECT_EQ("/support/bin/answer.py",
            browser()->GetSelectedTabContents()->GetURL().path());
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingBlockingPageTest, MalwareIframeDontProceed) {
  GURL url = test_server()->GetURL(kMalwarePage);
  GURL iframe_url = test_server()->GetURL(kMalwareIframe);
  AddURLResult(iframe_url, SafeBrowsingService::URL_MALWARE);

  ui_test_utils::NavigateToURL(browser(), url);

  SendCommand("\"takeMeBack\"");    // Simulate the user clicking "back"
  AssertNoInterstitial(false);  // Assert the interstitial is gone

  EXPECT_EQ(GURL(chrome::kAboutBlankURL),    // Back to "about:blank"
            browser()->GetSelectedTabContents()->GetURL());
}

// Crashy, http://crbug.com/68834.
IN_PROC_BROWSER_TEST_F(SafeBrowsingBlockingPageTest,
                       DISABLED_MalwareIframeProceed) {
  GURL url = test_server()->GetURL(kMalwarePage);
  GURL iframe_url = test_server()->GetURL(kMalwareIframe);
  AddURLResult(iframe_url, SafeBrowsingService::URL_MALWARE);

  ui_test_utils::NavigateToURL(browser(), url);

  SendCommand("\"proceed\"");   // Simulate the user clicking "proceed"
  AssertNoInterstitial(true);    // Assert the interstitial is gone

  EXPECT_EQ(url, browser()->GetSelectedTabContents()->GetURL());
}

IN_PROC_BROWSER_TEST_F(SafeBrowsingBlockingPageTest,
                       MalwareIframeReportDetails) {
  GURL url = test_server()->GetURL(kMalwarePage);
  GURL iframe_url = test_server()->GetURL(kMalwareIframe);
  AddURLResult(iframe_url, SafeBrowsingService::URL_MALWARE);

  ui_test_utils::NavigateToURL(browser(), url);

  // If the DOM details from renderer did not already return, wait for them.
  if (!details_factory_.get_details()->got_dom()) {
    // This condition might not trigger normally, but if you add a
    // sleep(1) in malware_dom_details it triggers :).
    details_factory_.get_details()->set_waiting(true);
    LOG(INFO) << "Waiting for dom details.";
    ui_test_utils::RunMessageLoop();
  } else {
    LOG(INFO) << "Already got the dom details.";
  }

  SendCommand("\"doReport\"");  // Simulate the user checking the checkbox.
  EXPECT_TRUE(browser()->GetProfile()->GetPrefs()->GetBoolean(
      prefs::kSafeBrowsingReportingEnabled));

  SendCommand("\"proceed\"");  // Simulate the user clicking "back"
  AssertNoInterstitial(true);  // Assert the interstitial is gone

  EXPECT_EQ(url, browser()->GetSelectedTabContents()->GetURL());
  AssertReportSent();
}

}  // namespace
