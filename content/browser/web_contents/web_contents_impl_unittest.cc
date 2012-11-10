// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"
#include "base/utf_string_conversions.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/test_render_view_host.h"
#include "content/browser/site_instance_impl.h"
#include "content/browser/web_contents/interstitial_page_impl.h"
#include "content/browser/web_contents/navigation_entry_impl.h"
#include "content/browser/web_contents/test_web_contents.h"
#include "content/common/view_messages.h"
#include "content/public/browser/interstitial_page_delegate.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_controller_factory.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/test_browser_thread.h"
#include "content/test/test_content_browser_client.h"
#include "content/test/test_content_client.h"
#include "googleurl/src/url_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/glue/webkit_glue.h"

using content::BrowserContext;
using content::BrowserThread;
using content::InterstitialPage;
using content::MockRenderProcessHost;
using content::NavigationEntry;
using content::NavigationEntryImpl;
using content::SiteInstance;
using content::RenderViewHost;
using content::RenderViewHostImplTestHarness;
using content::TestBrowserThread;
using content::TestRenderViewHost;
using content::TestWebContents;
using content::WebContents;
using content::WebUI;
using content::WebUIController;
using webkit::forms::PasswordForm;

namespace {

class WebContentsImplTestWebUIControllerFactory
    : public content::WebUIControllerFactory {
 public:
  virtual WebUIController* CreateWebUIControllerForURL(
      content::WebUI* web_ui, const GURL& url) const OVERRIDE {
   if (!content::GetContentClient()->HasWebUIScheme(url))
     return NULL;

   return new WebUIController(web_ui);
  }

  virtual WebUI::TypeID GetWebUIType(BrowserContext* browser_context,
      const GURL& url) const OVERRIDE {
    return WebUI::kNoWebUI;
  }

  virtual bool UseWebUIForURL(BrowserContext* browser_context,
                              const GURL& url) const OVERRIDE {
    return content::GetContentClient()->HasWebUIScheme(url);
  }

  virtual bool UseWebUIBindingsForURL(BrowserContext* browser_context,
                                      const GURL& url) const OVERRIDE {
    return content::GetContentClient()->HasWebUIScheme(url);
  }

  virtual bool IsURLAcceptableForWebUI(
      BrowserContext* browser_context,
      const GURL& url,
      bool data_urls_allowed) const {
    return content::GetContentClient()->HasWebUIScheme(url);
  }
};

class WebContentsImplTestContentClient : public TestContentClient {
 public:
  WebContentsImplTestContentClient() {
  }

  virtual bool HasWebUIScheme(const GURL& url) const OVERRIDE {
    return url.SchemeIs("webcontentsimpltest");
  }
};

class WebContentsImplTestBrowserClient
    : public content::TestContentBrowserClient {
 public:
  WebContentsImplTestBrowserClient() {
  }

  virtual content::WebUIControllerFactory*
      GetWebUIControllerFactory() OVERRIDE {
    return &factory_;
  }

 private:
  WebContentsImplTestWebUIControllerFactory factory_;
};

class TestInterstitialPage;

class TestInterstitialPageDelegate : public content::InterstitialPageDelegate {
 public:
  TestInterstitialPageDelegate(TestInterstitialPage* interstitial_page)
      : interstitial_page_(interstitial_page) {}
  virtual void CommandReceived(const std::string& command) OVERRIDE;
  virtual std::string GetHTMLContents() OVERRIDE { return std::string(); }
  virtual void OnDontProceed() OVERRIDE;
  virtual void OnProceed() OVERRIDE;
 private:
  TestInterstitialPage* interstitial_page_;
};

class TestInterstitialPage : public InterstitialPageImpl {
 public:
  enum InterstitialState {
    UNDECIDED = 0,  // No decision taken yet.
    OKED,           // Proceed was called.
    CANCELED        // DontProceed was called.
  };

  class Delegate {
   public:
    virtual void TestInterstitialPageDeleted(
        TestInterstitialPage* interstitial) = 0;

   protected:
    virtual ~Delegate() {}
  };

  // IMPORTANT NOTE: if you pass stack allocated values for |state| and
  // |deleted| (like all interstitial related tests do at this point), make sure
  // to create an instance of the TestInterstitialPageStateGuard class on the
  // stack in your test.  This will ensure that the TestInterstitialPage states
  // are cleared when the test finishes.
  // Not doing so will cause stack trashing if your test does not hide the
  // interstitial, as in such a case it will be destroyed in the test TearDown
  // method and will dereference the |deleted| local variable which by then is
  // out of scope.
  TestInterstitialPage(WebContentsImpl* contents,
                       bool new_navigation,
                       const GURL& url,
                       InterstitialState* state,
                       bool* deleted)
      : InterstitialPageImpl(
            contents, new_navigation, url,
            new TestInterstitialPageDelegate(this)),
        state_(state),
        deleted_(deleted),
        command_received_count_(0),
        delegate_(NULL) {
    *state_ = UNDECIDED;
    *deleted_ = false;
  }

  virtual ~TestInterstitialPage() {
    if (deleted_)
      *deleted_ = true;
    if (delegate_)
      delegate_->TestInterstitialPageDeleted(this);
  }

  void OnDontProceed() {
    if (state_)
      *state_ = CANCELED;
  }
  void OnProceed() {
    if (state_)
      *state_ = OKED;
  }

  int command_received_count() const {
    return command_received_count_;
  }

  void TestDomOperationResponse(const std::string& json_string) {
    if (enabled())
      CommandReceived();
  }

  void TestDidNavigate(int page_id, const GURL& url) {
    ViewHostMsg_FrameNavigate_Params params;
    InitNavigateParams(&params, page_id, url, content::PAGE_TRANSITION_TYPED);
    DidNavigate(GetRenderViewHostForTesting(), params);
  }

  void TestRenderViewGone(base::TerminationStatus status, int error_code) {
    RenderViewGone(GetRenderViewHostForTesting(), status, error_code);
  }

  bool is_showing() const {
    return static_cast<content::TestRenderWidgetHostView*>(
        GetRenderViewHostForTesting()->GetView())->is_showing();
  }

  void ClearStates() {
    state_ = NULL;
    deleted_ = NULL;
    delegate_ = NULL;
  }

  void CommandReceived() {
    command_received_count_++;
  }

  void set_delegate(Delegate* delegate) {
    delegate_ = delegate;
  }

 protected:
  virtual content::RenderViewHost* CreateRenderViewHost() OVERRIDE {
    return new TestRenderViewHost(
        SiteInstance::Create(web_contents()->GetBrowserContext()),
        this, this, MSG_ROUTING_NONE, false);
  }

  virtual content::WebContentsView* CreateWebContentsView() OVERRIDE {
    return NULL;
  }

 private:
  InterstitialState* state_;
  bool* deleted_;
  int command_received_count_;
  Delegate* delegate_;
};

void TestInterstitialPageDelegate::CommandReceived(const std::string& command) {
  interstitial_page_->CommandReceived();
}

void TestInterstitialPageDelegate::OnDontProceed() {
  interstitial_page_->OnDontProceed();
}

void TestInterstitialPageDelegate::OnProceed() {
  interstitial_page_->OnProceed();
}

class TestInterstitialPageStateGuard : public TestInterstitialPage::Delegate {
 public:
  explicit TestInterstitialPageStateGuard(
      TestInterstitialPage* interstitial_page)
      : interstitial_page_(interstitial_page) {
    DCHECK(interstitial_page_);
    interstitial_page_->set_delegate(this);
  }
  ~TestInterstitialPageStateGuard() {
    if (interstitial_page_)
      interstitial_page_->ClearStates();
  }

  virtual void TestInterstitialPageDeleted(TestInterstitialPage* interstitial) {
    DCHECK(interstitial_page_ == interstitial);
    interstitial_page_ = NULL;
  }

 private:
  TestInterstitialPage* interstitial_page_;
};

class WebContentsImplTest : public RenderViewHostImplTestHarness {
 public:
  WebContentsImplTest()
      : old_client_(NULL),
        old_browser_client_(NULL),
        ui_thread_(BrowserThread::UI, &message_loop_),
        file_user_blocking_thread_(
            BrowserThread::FILE_USER_BLOCKING, &message_loop_),
        io_thread_(BrowserThread::IO, &message_loop_) {
  }

  virtual void SetUp() {
    // These tests treat webcontentsimpltest as a privileged WebUI scheme.
    // We must register it similarly to kChromeUIScheme.
    url_util::AddStandardScheme("webcontentsimpltest");

    old_client_ = content::GetContentClient();
    old_browser_client_ = content::GetContentClient()->browser();
    content::SetContentClient(&client_);
    content::GetContentClient()->set_browser_for_testing(&browser_client_);
    RenderViewHostImplTestHarness::SetUp();
  }

  virtual void TearDown() {
    content::GetContentClient()->set_browser_for_testing(old_browser_client_);
    content::SetContentClient(old_client_);
    RenderViewHostImplTestHarness::TearDown();
  }

 private:
  WebContentsImplTestContentClient client_;
  WebContentsImplTestBrowserClient browser_client_;
  content::ContentClient* old_client_;
  content::ContentBrowserClient* old_browser_client_;
  TestBrowserThread ui_thread_;
  TestBrowserThread file_user_blocking_thread_;
  TestBrowserThread io_thread_;
};

}  // namespace

// Test to make sure that title updates get stripped of whitespace.
TEST_F(WebContentsImplTest, UpdateTitle) {
  NavigationControllerImpl& cont =
      static_cast<NavigationControllerImpl&>(controller());
  ViewHostMsg_FrameNavigate_Params params;
  InitNavigateParams(&params, 0, GURL(chrome::kAboutBlankURL),
                     content::PAGE_TRANSITION_TYPED);

  content::LoadCommittedDetails details;
  cont.RendererDidNavigate(params, &details);

  contents()->UpdateTitle(rvh(), 0, ASCIIToUTF16("    Lots O' Whitespace\n"),
                          base::i18n::LEFT_TO_RIGHT);
  EXPECT_EQ(ASCIIToUTF16("Lots O' Whitespace"), contents()->GetTitle());
}

// Test view source mode for a webui page.
TEST_F(WebContentsImplTest, NTPViewSource) {
  NavigationControllerImpl& cont =
      static_cast<NavigationControllerImpl&>(controller());
  const char kUrl[] = "view-source:webcontentsimpltest://blah";
  const GURL kGURL(kUrl);

  process()->sink().ClearMessages();

  cont.LoadURL(
      kGURL, content::Referrer(), content::PAGE_TRANSITION_TYPED,
      std::string());
  rvh()->GetDelegate()->RenderViewCreated(rvh());
  // Did we get the expected message?
  EXPECT_TRUE(process()->sink().GetFirstMessageMatching(
      ViewMsg_EnableViewSourceMode::ID));

  ViewHostMsg_FrameNavigate_Params params;
  InitNavigateParams(&params, 0, kGURL, content::PAGE_TRANSITION_TYPED);
  content::LoadCommittedDetails details;
  cont.RendererDidNavigate(params, &details);
  // Also check title and url.
  EXPECT_EQ(ASCIIToUTF16(kUrl), contents()->GetTitle());
}

// Test to ensure UpdateMaxPageID is working properly.
TEST_F(WebContentsImplTest, UpdateMaxPageID) {
  SiteInstance* instance1 = contents()->GetSiteInstance();
  scoped_refptr<SiteInstance> instance2(SiteInstance::Create(NULL));

  // Starts at -1.
  EXPECT_EQ(-1, contents()->GetMaxPageID());
  EXPECT_EQ(-1, contents()->GetMaxPageIDForSiteInstance(instance1));
  EXPECT_EQ(-1, contents()->GetMaxPageIDForSiteInstance(instance2));

  // Make sure max_page_id_ is monotonically increasing per SiteInstance.
  contents()->UpdateMaxPageID(3);
  contents()->UpdateMaxPageID(1);
  EXPECT_EQ(3, contents()->GetMaxPageID());
  EXPECT_EQ(3, contents()->GetMaxPageIDForSiteInstance(instance1));
  EXPECT_EQ(-1, contents()->GetMaxPageIDForSiteInstance(instance2));

  contents()->UpdateMaxPageIDForSiteInstance(instance2, 7);
  EXPECT_EQ(3, contents()->GetMaxPageID());
  EXPECT_EQ(3, contents()->GetMaxPageIDForSiteInstance(instance1));
  EXPECT_EQ(7, contents()->GetMaxPageIDForSiteInstance(instance2));
}

// Test simple same-SiteInstance navigation.
TEST_F(WebContentsImplTest, SimpleNavigation) {
  TestRenderViewHost* orig_rvh = test_rvh();
  SiteInstance* instance1 = contents()->GetSiteInstance();
  EXPECT_TRUE(contents()->GetPendingRenderViewHost() == NULL);

  // Navigate to URL
  const GURL url("http://www.google.com");
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(instance1, orig_rvh->GetSiteInstance());
  // Controller's pending entry will have a NULL site instance until we assign
  // it in DidNavigate.
  EXPECT_TRUE(
      NavigationEntryImpl::FromNavigationEntry(controller().GetActiveEntry())->
          site_instance() == NULL);

  // DidNavigate from the page
  contents()->TestDidNavigate(orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(orig_rvh, contents()->GetRenderViewHost());
  EXPECT_EQ(instance1, orig_rvh->GetSiteInstance());
  // Controller's entry should now have the SiteInstance, or else we won't be
  // able to find it later.
  EXPECT_EQ(
      instance1,
      NavigationEntryImpl::FromNavigationEntry(controller().GetActiveEntry())->
          site_instance());
}

// Test that we reject NavigateToEntry if the url is over content::kMaxURLChars.
TEST_F(WebContentsImplTest, NavigateToExcessivelyLongURL) {
  // Construct a URL that's kMaxURLChars + 1 long of all 'a's.
  const GURL url(std::string("http://example.org/").append(
      content::kMaxURLChars + 1, 'a'));

  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_GENERATED,
      std::string());
  EXPECT_TRUE(controller().GetActiveEntry() == NULL);
}

// Test that navigating across a site boundary creates a new RenderViewHost
// with a new SiteInstance.  Going back should do the same.
TEST_F(WebContentsImplTest, CrossSiteBoundaries) {
  contents()->transition_cross_site = true;
  TestRenderViewHost* orig_rvh = test_rvh();
  int orig_rvh_delete_count = 0;
  orig_rvh->set_delete_counter(&orig_rvh_delete_count);
  SiteInstance* instance1 = contents()->GetSiteInstance();

  // Navigate to URL.  First URL should use first RenderViewHost.
  const GURL url("http://www.google.com");
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  contents()->TestDidNavigate(orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);

  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(orig_rvh, contents()->GetRenderViewHost());

  // Navigate to new site
  const GURL url2("http://www.yahoo.com");
  controller().LoadURL(
      url2, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  EXPECT_TRUE(contents()->cross_navigation_pending());
  TestRenderViewHost* pending_rvh =
      static_cast<TestRenderViewHost*>(contents()->GetPendingRenderViewHost());
  int pending_rvh_delete_count = 0;
  pending_rvh->set_delete_counter(&pending_rvh_delete_count);

  // Navigations should be suspended in pending_rvh until ShouldCloseACK.
  EXPECT_TRUE(pending_rvh->are_navigations_suspended());
  orig_rvh->SendShouldCloseACK(true);
  EXPECT_FALSE(pending_rvh->are_navigations_suspended());

  // DidNavigate from the pending page
  contents()->TestDidNavigate(
      pending_rvh, 1, url2, content::PAGE_TRANSITION_TYPED);
  SiteInstance* instance2 = contents()->GetSiteInstance();

  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(pending_rvh, contents()->GetRenderViewHost());
  EXPECT_NE(instance1, instance2);
  EXPECT_TRUE(contents()->GetPendingRenderViewHost() == NULL);
  // We keep the original RVH around, swapped out.
  EXPECT_TRUE(contents()->GetRenderManagerForTesting()->IsSwappedOut(orig_rvh));
  EXPECT_EQ(orig_rvh_delete_count, 0);

  // Going back should switch SiteInstances again.  The first SiteInstance is
  // stored in the NavigationEntry, so it should be the same as at the start.
  // We should use the same RVH as before, swapping it back in.
  controller().GoBack();
  TestRenderViewHost* goback_rvh =
      static_cast<TestRenderViewHost*>(contents()->GetPendingRenderViewHost());
  EXPECT_EQ(orig_rvh, goback_rvh);
  EXPECT_TRUE(contents()->cross_navigation_pending());

  // Navigations should be suspended in goback_rvh until ShouldCloseACK.
  EXPECT_TRUE(goback_rvh->are_navigations_suspended());
  pending_rvh->SendShouldCloseACK(true);
  EXPECT_FALSE(goback_rvh->are_navigations_suspended());

  // DidNavigate from the back action
  contents()->TestDidNavigate(
      goback_rvh, 1, url2, content::PAGE_TRANSITION_TYPED);
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(goback_rvh, contents()->GetRenderViewHost());
  EXPECT_EQ(instance1, contents()->GetSiteInstance());
  // The pending RVH should now be swapped out, not deleted.
  EXPECT_TRUE(contents()->GetRenderManagerForTesting()->
      IsSwappedOut(pending_rvh));
  EXPECT_EQ(pending_rvh_delete_count, 0);

  // Close contents and ensure RVHs are deleted.
  DeleteContents();
  EXPECT_EQ(orig_rvh_delete_count, 1);
  EXPECT_EQ(pending_rvh_delete_count, 1);
}

// Test that navigating across a site boundary after a crash creates a new
// RVH without requiring a cross-site transition (i.e., PENDING state).
TEST_F(WebContentsImplTest, CrossSiteBoundariesAfterCrash) {
  contents()->transition_cross_site = true;
  TestRenderViewHost* orig_rvh = test_rvh();
  int orig_rvh_delete_count = 0;
  orig_rvh->set_delete_counter(&orig_rvh_delete_count);
  SiteInstance* instance1 = contents()->GetSiteInstance();

  // Navigate to URL.  First URL should use first RenderViewHost.
  const GURL url("http://www.google.com");
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  contents()->TestDidNavigate(orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);

  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(orig_rvh, contents()->GetRenderViewHost());

  // Crash the renderer.
  orig_rvh->set_render_view_created(false);

  // Navigate to new site.  We should not go into PENDING.
  const GURL url2("http://www.yahoo.com");
  controller().LoadURL(
      url2, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  RenderViewHost* new_rvh = rvh();
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_TRUE(contents()->GetPendingRenderViewHost() == NULL);
  EXPECT_NE(orig_rvh, new_rvh);
  EXPECT_EQ(orig_rvh_delete_count, 1);

  // DidNavigate from the new page
  contents()->TestDidNavigate(new_rvh, 1, url2, content::PAGE_TRANSITION_TYPED);
  SiteInstance* instance2 = contents()->GetSiteInstance();

  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(new_rvh, rvh());
  EXPECT_NE(instance1, instance2);
  EXPECT_TRUE(contents()->GetPendingRenderViewHost() == NULL);

  // Close contents and ensure RVHs are deleted.
  DeleteContents();
  EXPECT_EQ(orig_rvh_delete_count, 1);
}

// Test that opening a new contents in the same SiteInstance and then navigating
// both contentses to a new site will place both contentses in a single
// SiteInstance.
TEST_F(WebContentsImplTest, NavigateTwoTabsCrossSite) {
  contents()->transition_cross_site = true;
  TestRenderViewHost* orig_rvh = test_rvh();
  SiteInstance* instance1 = contents()->GetSiteInstance();

  // Navigate to URL.  First URL should use first RenderViewHost.
  const GURL url("http://www.google.com");
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  contents()->TestDidNavigate(orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);

  // Open a new contents with the same SiteInstance, navigated to the same site.
  TestWebContents contents2(browser_context_.get(), instance1);
  contents2.transition_cross_site = true;
  contents2.GetController().LoadURL(url, content::Referrer(),
                                    content::PAGE_TRANSITION_TYPED,
                                    std::string());
  // Need this page id to be 2 since the site instance is the same (which is the
  // scope of page IDs) and we want to consider this a new page.
  contents2.TestDidNavigate(
      contents2.GetRenderViewHost(), 2, url, content::PAGE_TRANSITION_TYPED);

  // Navigate first contents to a new site.
  const GURL url2a("http://www.yahoo.com");
  controller().LoadURL(
      url2a, content::Referrer(), content::PAGE_TRANSITION_TYPED,
      std::string());
  orig_rvh->SendShouldCloseACK(true);
  TestRenderViewHost* pending_rvh_a =
      static_cast<TestRenderViewHost*>(contents()->GetPendingRenderViewHost());
  contents()->TestDidNavigate(
      pending_rvh_a, 1, url2a, content::PAGE_TRANSITION_TYPED);
  SiteInstance* instance2a = contents()->GetSiteInstance();
  EXPECT_NE(instance1, instance2a);

  // Navigate second contents to the same site as the first tab.
  const GURL url2b("http://mail.yahoo.com");
  contents2.GetController().LoadURL(url2b, content::Referrer(),
                                    content::PAGE_TRANSITION_TYPED,
                                    std::string());
  TestRenderViewHost* rvh2 =
      static_cast<TestRenderViewHost*>(contents2.GetRenderViewHost());
  rvh2->SendShouldCloseACK(true);
  TestRenderViewHost* pending_rvh_b =
      static_cast<TestRenderViewHost*>(contents2.GetPendingRenderViewHost());
  EXPECT_TRUE(pending_rvh_b != NULL);
  EXPECT_TRUE(contents2.cross_navigation_pending());

  // NOTE(creis): We used to be in danger of showing a crash page here if the
  // second contents hadn't navigated somewhere first (bug 1145430).  That case
  // is now covered by the CrossSiteBoundariesAfterCrash test.
  contents2.TestDidNavigate(
      pending_rvh_b, 2, url2b, content::PAGE_TRANSITION_TYPED);
  SiteInstance* instance2b = contents2.GetSiteInstance();
  EXPECT_NE(instance1, instance2b);

  // Both contentses should now be in the same SiteInstance.
  EXPECT_EQ(instance2a, instance2b);
}

// Tests that WebContentsImpl uses the current URL, not the SiteInstance's site,
// to determine whether a navigation is cross-site.
TEST_F(WebContentsImplTest, CrossSiteComparesAgainstCurrentPage) {
  contents()->transition_cross_site = true;
  RenderViewHost* orig_rvh = rvh();
  SiteInstance* instance1 = contents()->GetSiteInstance();

  // Navigate to URL.
  const GURL url("http://www.google.com");
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  contents()->TestDidNavigate(
      orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);

  // Open a related contents to a second site.
  TestWebContents contents2(browser_context_.get(), instance1);
  contents2.transition_cross_site = true;
  const GURL url2("http://www.yahoo.com");
  contents2.GetController().LoadURL(url2, content::Referrer(),
                                    content::PAGE_TRANSITION_TYPED,
                                    std::string());
  // The first RVH in contents2 isn't live yet, so we shortcut the cross site
  // pending.
  TestRenderViewHost* rvh2 = static_cast<TestRenderViewHost*>(
      contents2.GetRenderViewHost());
  EXPECT_FALSE(contents2.cross_navigation_pending());
  contents2.TestDidNavigate(rvh2, 2, url2, content::PAGE_TRANSITION_TYPED);
  SiteInstance* instance2 = contents2.GetSiteInstance();
  EXPECT_NE(instance1, instance2);
  EXPECT_FALSE(contents2.cross_navigation_pending());

  // Simulate a link click in first contents to second site.  Doesn't switch
  // SiteInstances, because we don't intercept WebKit navigations.
  contents()->TestDidNavigate(
      orig_rvh, 2, url2, content::PAGE_TRANSITION_TYPED);
  SiteInstance* instance3 = contents()->GetSiteInstance();
  EXPECT_EQ(instance1, instance3);
  EXPECT_FALSE(contents()->cross_navigation_pending());

  // Navigate to the new site.  Doesn't switch SiteInstancees, because we
  // compare against the current URL, not the SiteInstance's site.
  const GURL url3("http://mail.yahoo.com");
  controller().LoadURL(
      url3, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  EXPECT_FALSE(contents()->cross_navigation_pending());
  contents()->TestDidNavigate(
      orig_rvh, 3, url3, content::PAGE_TRANSITION_TYPED);
  SiteInstance* instance4 = contents()->GetSiteInstance();
  EXPECT_EQ(instance1, instance4);
}

// Test that the onbeforeunload and onunload handlers run when navigating
// across site boundaries.
TEST_F(WebContentsImplTest, CrossSiteUnloadHandlers) {
  contents()->transition_cross_site = true;
  TestRenderViewHost* orig_rvh = test_rvh();
  SiteInstance* instance1 = contents()->GetSiteInstance();

  // Navigate to URL.  First URL should use first RenderViewHost.
  const GURL url("http://www.google.com");
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  contents()->TestDidNavigate(orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(orig_rvh, contents()->GetRenderViewHost());

  // Navigate to new site, but simulate an onbeforeunload denial.
  const GURL url2("http://www.yahoo.com");
  controller().LoadURL(
      url2, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  EXPECT_TRUE(orig_rvh->is_waiting_for_beforeunload_ack());
  orig_rvh->OnMessageReceived(ViewHostMsg_ShouldClose_ACK(
      0, false, base::TimeTicks(), base::TimeTicks()));
  EXPECT_FALSE(orig_rvh->is_waiting_for_beforeunload_ack());
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(orig_rvh, contents()->GetRenderViewHost());

  // Navigate again, but simulate an onbeforeunload approval.
  controller().LoadURL(
      url2, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  EXPECT_TRUE(orig_rvh->is_waiting_for_beforeunload_ack());
  orig_rvh->OnMessageReceived(ViewHostMsg_ShouldClose_ACK(
      0, true, base::TimeTicks(), base::TimeTicks()));
  EXPECT_FALSE(orig_rvh->is_waiting_for_beforeunload_ack());
  EXPECT_TRUE(contents()->cross_navigation_pending());
  TestRenderViewHost* pending_rvh = static_cast<TestRenderViewHost*>(
      contents()->GetPendingRenderViewHost());

  // We won't hear DidNavigate until the onunload handler has finished running.
  // (No way to simulate that here, but it involves a call from RDH to
  // WebContentsImpl::OnCrossSiteResponse.)

  // DidNavigate from the pending page
  contents()->TestDidNavigate(
      pending_rvh, 1, url2, content::PAGE_TRANSITION_TYPED);
  SiteInstance* instance2 = contents()->GetSiteInstance();
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(pending_rvh, rvh());
  EXPECT_NE(instance1, instance2);
  EXPECT_TRUE(contents()->GetPendingRenderViewHost() == NULL);
}

// Test that during a slow cross-site navigation, the original renderer can
// navigate to a different URL and have it displayed, canceling the slow
// navigation.
TEST_F(WebContentsImplTest, CrossSiteNavigationPreempted) {
  contents()->transition_cross_site = true;
  TestRenderViewHost* orig_rvh = test_rvh();
  SiteInstance* instance1 = contents()->GetSiteInstance();

  // Navigate to URL.  First URL should use first RenderViewHost.
  const GURL url("http://www.google.com");
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  contents()->TestDidNavigate(orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(orig_rvh, contents()->GetRenderViewHost());

  // Navigate to new site, simulating an onbeforeunload approval.
  const GURL url2("http://www.yahoo.com");
  controller().LoadURL(
      url2, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  EXPECT_TRUE(orig_rvh->is_waiting_for_beforeunload_ack());
  orig_rvh->OnMessageReceived(ViewHostMsg_ShouldClose_ACK(
      0, true, base::TimeTicks(), base::TimeTicks()));
  EXPECT_TRUE(contents()->cross_navigation_pending());

  // Suppose the original renderer navigates before the new one is ready.
  orig_rvh->SendNavigate(2, GURL("http://www.google.com/foo"));

  // Verify that the pending navigation is cancelled.
  EXPECT_FALSE(orig_rvh->is_waiting_for_beforeunload_ack());
  SiteInstance* instance2 = contents()->GetSiteInstance();
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(orig_rvh, rvh());
  EXPECT_EQ(instance1, instance2);
  EXPECT_TRUE(contents()->GetPendingRenderViewHost() == NULL);
}

TEST_F(WebContentsImplTest, CrossSiteNavigationBackPreempted) {
  contents()->transition_cross_site = true;

  // Start with a web ui page, which gets a new RVH with WebUI bindings.
  const GURL url1("webcontentsimpltest://blah");
  controller().LoadURL(
      url1, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  TestRenderViewHost* ntp_rvh = test_rvh();
  contents()->TestDidNavigate(ntp_rvh, 1, url1, content::PAGE_TRANSITION_TYPED);
  NavigationEntry* entry1 = controller().GetLastCommittedEntry();
  SiteInstance* instance1 = contents()->GetSiteInstance();

  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(ntp_rvh, contents()->GetRenderViewHost());
  EXPECT_EQ(url1, entry1->GetURL());
  EXPECT_EQ(instance1,
            NavigationEntryImpl::FromNavigationEntry(entry1)->site_instance());
  EXPECT_TRUE(ntp_rvh->GetEnabledBindings() & content::BINDINGS_POLICY_WEB_UI);

  // Navigate to new site.
  const GURL url2("http://www.google.com");
  controller().LoadURL(
      url2, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  EXPECT_TRUE(contents()->cross_navigation_pending());
  TestRenderViewHost* google_rvh =
      static_cast<TestRenderViewHost*>(contents()->GetPendingRenderViewHost());

  // Simulate beforeunload approval.
  EXPECT_TRUE(ntp_rvh->is_waiting_for_beforeunload_ack());
  ntp_rvh->OnMessageReceived(ViewHostMsg_ShouldClose_ACK(
      0, true, base::TimeTicks(), base::TimeTicks()));

  // DidNavigate from the pending page.
  contents()->TestDidNavigate(
      google_rvh, 1, url2, content::PAGE_TRANSITION_TYPED);
  NavigationEntry* entry2 = controller().GetLastCommittedEntry();
  SiteInstance* instance2 = contents()->GetSiteInstance();

  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(google_rvh, contents()->GetRenderViewHost());
  EXPECT_NE(instance1, instance2);
  EXPECT_FALSE(contents()->GetPendingRenderViewHost());
  EXPECT_EQ(url2, entry2->GetURL());
  EXPECT_EQ(instance2,
            NavigationEntryImpl::FromNavigationEntry(entry2)->site_instance());
  EXPECT_FALSE(google_rvh->GetEnabledBindings() &
      content::BINDINGS_POLICY_WEB_UI);

  // Navigate to third page on same site.
  const GURL url3("http://news.google.com");
  controller().LoadURL(
      url3, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  EXPECT_FALSE(contents()->cross_navigation_pending());
  contents()->TestDidNavigate(
      google_rvh, 2, url3, content::PAGE_TRANSITION_TYPED);
  NavigationEntry* entry3 = controller().GetLastCommittedEntry();
  SiteInstance* instance3 = contents()->GetSiteInstance();

  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(google_rvh, contents()->GetRenderViewHost());
  EXPECT_EQ(instance2, instance3);
  EXPECT_FALSE(contents()->GetPendingRenderViewHost());
  EXPECT_EQ(url3, entry3->GetURL());
  EXPECT_EQ(instance3,
            NavigationEntryImpl::FromNavigationEntry(entry3)->site_instance());

  // Go back within the site.
  controller().GoBack();
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(entry2, controller().GetPendingEntry());

  // Before that commits, go back again.
  controller().GoBack();
  EXPECT_TRUE(contents()->cross_navigation_pending());
  EXPECT_TRUE(contents()->GetPendingRenderViewHost());
  EXPECT_EQ(entry1, controller().GetPendingEntry());

  // Simulate beforeunload approval.
  EXPECT_TRUE(google_rvh->is_waiting_for_beforeunload_ack());
  google_rvh->OnMessageReceived(ViewHostMsg_ShouldClose_ACK(
      0, true, base::TimeTicks(), base::TimeTicks()));

  // DidNavigate from the first back. This aborts the second back's pending RVH.
  contents()->TestDidNavigate(
      google_rvh, 1, url2, content::PAGE_TRANSITION_TYPED);

  // We should commit this page and forget about the second back.
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_FALSE(controller().GetPendingEntry());
  EXPECT_EQ(google_rvh, contents()->GetRenderViewHost());
  EXPECT_EQ(url2, controller().GetLastCommittedEntry()->GetURL());

  // We should not have corrupted the NTP entry.
  EXPECT_EQ(instance3,
            NavigationEntryImpl::FromNavigationEntry(entry3)->site_instance());
  EXPECT_EQ(instance2,
            NavigationEntryImpl::FromNavigationEntry(entry2)->site_instance());
  EXPECT_EQ(instance1,
            NavigationEntryImpl::FromNavigationEntry(entry1)->site_instance());
  EXPECT_EQ(url1, entry1->GetURL());
}

// Test that during a slow cross-site navigation, a sub-frame navigation in the
// original renderer will not cancel the slow navigation (bug 42029).
TEST_F(WebContentsImplTest, CrossSiteNavigationNotPreemptedByFrame) {
  contents()->transition_cross_site = true;
  TestRenderViewHost* orig_rvh = test_rvh();

  // Navigate to URL.  First URL should use first RenderViewHost.
  const GURL url("http://www.google.com");
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  contents()->TestDidNavigate(orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(orig_rvh, contents()->GetRenderViewHost());

  // Start navigating to new site.
  const GURL url2("http://www.yahoo.com");
  controller().LoadURL(
      url2, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());

  // Simulate a sub-frame navigation arriving and ensure the RVH is still
  // waiting for a before unload response.
  orig_rvh->SendNavigateWithTransition(1, GURL("http://google.com/frame"),
                                       content::PAGE_TRANSITION_AUTO_SUBFRAME);
  EXPECT_TRUE(orig_rvh->is_waiting_for_beforeunload_ack());

  // Now simulate the onbeforeunload approval and verify the navigation is
  // not canceled.
  orig_rvh->OnMessageReceived(ViewHostMsg_ShouldClose_ACK(
      0, true, base::TimeTicks(), base::TimeTicks()));
  EXPECT_FALSE(orig_rvh->is_waiting_for_beforeunload_ack());
  EXPECT_TRUE(contents()->cross_navigation_pending());
}

// Test that a cross-site navigation is not preempted if the previous
// renderer sends a FrameNavigate message just before being told to stop.
// We should only preempt the cross-site navigation if the previous renderer
// has started a new navigation.  See http://crbug.com/79176.
TEST_F(WebContentsImplTest, CrossSiteNotPreemptedDuringBeforeUnload) {
  contents()->transition_cross_site = true;

  // Navigate to NTP URL.
  const GURL url("webcontentsimpltest://blah");
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  TestRenderViewHost* orig_rvh = test_rvh();
  EXPECT_FALSE(contents()->cross_navigation_pending());

  // Navigate to new site, with the beforeunload request in flight.
  const GURL url2("http://www.yahoo.com");
  controller().LoadURL(
      url2, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  TestRenderViewHost* pending_rvh =
      static_cast<TestRenderViewHost*>(contents()->GetPendingRenderViewHost());
  EXPECT_TRUE(contents()->cross_navigation_pending());
  EXPECT_TRUE(orig_rvh->is_waiting_for_beforeunload_ack());

  // Suppose the first navigation tries to commit now, with a
  // ViewMsg_Stop in flight.  This should not cancel the pending navigation,
  // but it should act as if the beforeunload ack arrived.
  orig_rvh->SendNavigate(1, GURL("webcontentsimpltest://blah"));
  EXPECT_TRUE(contents()->cross_navigation_pending());
  EXPECT_EQ(orig_rvh, contents()->GetRenderViewHost());
  EXPECT_FALSE(orig_rvh->is_waiting_for_beforeunload_ack());

  // The pending navigation should be able to commit successfully.
  contents()->TestDidNavigate(
      pending_rvh, 1, url2, content::PAGE_TRANSITION_TYPED);
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(pending_rvh, contents()->GetRenderViewHost());
}

// Test that the original renderer cannot preempt a cross-site navigation once
// the unload request has been made.  At this point, the cross-site navigation
// is almost ready to be displayed, and the original renderer is only given a
// short chance to run an unload handler.  Prevents regression of bug 23942.
TEST_F(WebContentsImplTest, CrossSiteCantPreemptAfterUnload) {
  contents()->transition_cross_site = true;
  TestRenderViewHost* orig_rvh = test_rvh();
  SiteInstance* instance1 = contents()->GetSiteInstance();

  // Navigate to URL.  First URL should use first RenderViewHost.
  const GURL url("http://www.google.com");
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  contents()->TestDidNavigate(orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(orig_rvh, contents()->GetRenderViewHost());

  // Navigate to new site, simulating an onbeforeunload approval.
  const GURL url2("http://www.yahoo.com");
  controller().LoadURL(
      url2, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  orig_rvh->OnMessageReceived(ViewHostMsg_ShouldClose_ACK(
      0, true, base::TimeTicks(), base::TimeTicks()));
  EXPECT_TRUE(contents()->cross_navigation_pending());
  TestRenderViewHost* pending_rvh = static_cast<TestRenderViewHost*>(
      contents()->GetPendingRenderViewHost());

  // Simulate the pending renderer's response, which leads to an unload request
  // being sent to orig_rvh.
  contents()->GetRenderManagerForTesting()->OnCrossSiteResponse(0, 0);

  // Suppose the original renderer navigates now, while the unload request is in
  // flight.  We should ignore it, wait for the unload ack, and let the pending
  // request continue.  Otherwise, the contents may close spontaneously or stop
  // responding to navigation requests.  (See bug 23942.)
  ViewHostMsg_FrameNavigate_Params params1a;
  InitNavigateParams(&params1a, 2, GURL("http://www.google.com/foo"),
                     content::PAGE_TRANSITION_TYPED);
  orig_rvh->SendNavigate(2, GURL("http://www.google.com/foo"));

  // Verify that the pending navigation is still in progress.
  EXPECT_TRUE(contents()->cross_navigation_pending());
  EXPECT_TRUE(contents()->GetPendingRenderViewHost() != NULL);

  // DidNavigate from the pending page should commit it.
  contents()->TestDidNavigate(
      pending_rvh, 1, url2, content::PAGE_TRANSITION_TYPED);
  SiteInstance* instance2 = contents()->GetSiteInstance();
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(pending_rvh, rvh());
  EXPECT_NE(instance1, instance2);
  EXPECT_TRUE(contents()->GetPendingRenderViewHost() == NULL);
}

// Test that a cross-site navigation that doesn't commit after the unload
// handler doesn't leave the contents in a stuck state.  http://crbug.com/88562
TEST_F(WebContentsImplTest, CrossSiteNavigationCanceled) {
  contents()->transition_cross_site = true;
  TestRenderViewHost* orig_rvh = test_rvh();
  SiteInstance* instance1 = contents()->GetSiteInstance();

  // Navigate to URL.  First URL should use first RenderViewHost.
  const GURL url("http://www.google.com");
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  contents()->TestDidNavigate(orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(orig_rvh, contents()->GetRenderViewHost());

  // Navigate to new site, simulating an onbeforeunload approval.
  const GURL url2("http://www.yahoo.com");
  controller().LoadURL(
      url2, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  EXPECT_TRUE(orig_rvh->is_waiting_for_beforeunload_ack());
  orig_rvh->OnMessageReceived(ViewHostMsg_ShouldClose_ACK(
      0, true, base::TimeTicks(), base::TimeTicks()));
  EXPECT_TRUE(contents()->cross_navigation_pending());

  // Simulate swap out message when the response arrives.
  orig_rvh->set_is_swapped_out(true);

  // Suppose the navigation doesn't get a chance to commit, and the user
  // navigates in the current RVH's SiteInstance.
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());

  // Verify that the pending navigation is cancelled and the renderer is no
  // longer swapped out.
  EXPECT_FALSE(orig_rvh->is_waiting_for_beforeunload_ack());
  SiteInstance* instance2 = contents()->GetSiteInstance();
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(orig_rvh, rvh());
  EXPECT_FALSE(orig_rvh->is_swapped_out());
  EXPECT_EQ(instance1, instance2);
  EXPECT_TRUE(contents()->GetPendingRenderViewHost() == NULL);
}

// Test that NavigationEntries have the correct content state after going
// forward and back.  Prevents regression for bug 1116137.
TEST_F(WebContentsImplTest, NavigationEntryContentState) {
  TestRenderViewHost* orig_rvh = test_rvh();

  // Navigate to URL.  There should be no committed entry yet.
  const GURL url("http://www.google.com");
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  NavigationEntry* entry = controller().GetLastCommittedEntry();
  EXPECT_TRUE(entry == NULL);

  // Committed entry should have content state after DidNavigate.
  contents()->TestDidNavigate(orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);
  entry = controller().GetLastCommittedEntry();
  EXPECT_FALSE(entry->GetContentState().empty());

  // Navigate to same site.
  const GURL url2("http://images.google.com");
  controller().LoadURL(
      url2, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  entry = controller().GetLastCommittedEntry();
  EXPECT_FALSE(entry->GetContentState().empty());

  // Committed entry should have content state after DidNavigate.
  contents()->TestDidNavigate(
      orig_rvh, 2, url2, content::PAGE_TRANSITION_TYPED);
  entry = controller().GetLastCommittedEntry();
  EXPECT_FALSE(entry->GetContentState().empty());

  // Now go back.  Committed entry should still have content state.
  controller().GoBack();
  contents()->TestDidNavigate(orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);
  entry = controller().GetLastCommittedEntry();
  EXPECT_FALSE(entry->GetContentState().empty());
}

// Test that NavigationEntries have the correct content state and SiteInstance
// state after opening a new window to about:blank.  Prevents regression for
// bugs b/1116137 and http://crbug.com/111975.
TEST_F(WebContentsImplTest, NavigationEntryContentStateNewWindow) {
  TestRenderViewHost* orig_rvh = test_rvh();

  // When opening a new window, it is navigated to about:blank internally.
  // Currently, this results in two DidNavigate events.
  const GURL url(chrome::kAboutBlankURL);
  contents()->TestDidNavigate(orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);
  contents()->TestDidNavigate(orig_rvh, 1, url, content::PAGE_TRANSITION_TYPED);

  // Should have a content state here.
  NavigationEntry* entry = controller().GetLastCommittedEntry();
  EXPECT_FALSE(entry->GetContentState().empty());

  // The SiteInstance should be available for other navigations to use.
  NavigationEntryImpl* entry_impl =
      NavigationEntryImpl::FromNavigationEntry(entry);
  EXPECT_FALSE(entry_impl->site_instance()->HasSite());
  int32 site_instance_id = entry_impl->site_instance()->GetId();

  // Navigating to a normal page should not cause a process swap.
  const GURL new_url("http://www.google.com");
  controller().LoadURL(new_url, content::Referrer(),
                       content::PAGE_TRANSITION_TYPED, std::string());
  EXPECT_FALSE(contents()->cross_navigation_pending());
  EXPECT_EQ(orig_rvh, contents()->GetRenderViewHost());
  contents()->TestDidNavigate(orig_rvh, 1, new_url,
                              content::PAGE_TRANSITION_TYPED);
  NavigationEntryImpl* entry_impl2 = NavigationEntryImpl::FromNavigationEntry(
      controller().GetLastCommittedEntry());
  EXPECT_EQ(site_instance_id, entry_impl2->site_instance()->GetId());
  EXPECT_TRUE(entry_impl2->site_instance()->HasSite());
}

////////////////////////////////////////////////////////////////////////////////
// Interstitial Tests
////////////////////////////////////////////////////////////////////////////////

// Test navigating to a page (with the navigation initiated from the browser,
// as when a URL is typed in the location bar) that shows an interstitial and
// creates a new navigation entry, then hiding it without proceeding.
TEST_F(WebContentsImplTest,
       ShowInterstitialFromBrowserWithNewNavigationDontProceed) {
  // Navigate to a page.
  GURL url1("http://www.google.com");
  test_rvh()->SendNavigate(1, url1);
  EXPECT_EQ(1, controller().GetEntryCount());

  // Initiate a browser navigation that will trigger the interstitial
  controller().LoadURL(GURL("http://www.evil.com"), content::Referrer(),
                        content::PAGE_TRANSITION_TYPED, std::string());

  // Show an interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL url2("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, url2, &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  // The interstitial should not show until its navigation has committed.
  EXPECT_FALSE(interstitial->is_showing());
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  // Let's commit the interstitial navigation.
  interstitial->TestDidNavigate(1, url2);
  EXPECT_TRUE(interstitial->is_showing());
  EXPECT_TRUE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == interstitial);
  NavigationEntry* entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  EXPECT_TRUE(entry->GetURL() == url2);

  // Now don't proceed.
  interstitial->DontProceed();
  EXPECT_TRUE(deleted);
  EXPECT_EQ(TestInterstitialPage::CANCELED, state);
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  EXPECT_TRUE(entry->GetURL() == url1);
  EXPECT_EQ(1, controller().GetEntryCount());
}

// Test navigating to a page (with the navigation initiated from the renderer,
// as when clicking on a link in the page) that shows an interstitial and
// creates a new navigation entry, then hiding it without proceeding.
TEST_F(WebContentsImplTest,
       ShowInterstitiaFromRendererlWithNewNavigationDontProceed) {
  // Navigate to a page.
  GURL url1("http://www.google.com");
  test_rvh()->SendNavigate(1, url1);
  EXPECT_EQ(1, controller().GetEntryCount());

  // Show an interstitial (no pending entry, the interstitial would have been
  // triggered by clicking on a link).
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL url2("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, url2, &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  // The interstitial should not show until its navigation has committed.
  EXPECT_FALSE(interstitial->is_showing());
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  // Let's commit the interstitial navigation.
  interstitial->TestDidNavigate(1, url2);
  EXPECT_TRUE(interstitial->is_showing());
  EXPECT_TRUE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == interstitial);
  NavigationEntry* entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  EXPECT_TRUE(entry->GetURL() == url2);

  // Now don't proceed.
  interstitial->DontProceed();
  EXPECT_TRUE(deleted);
  EXPECT_EQ(TestInterstitialPage::CANCELED, state);
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  EXPECT_TRUE(entry->GetURL() == url1);
  EXPECT_EQ(1, controller().GetEntryCount());
}

// Test navigating to a page that shows an interstitial without creating a new
// navigation entry (this happens when the interstitial is triggered by a
// sub-resource in the page), then hiding it without proceeding.
TEST_F(WebContentsImplTest, ShowInterstitialNoNewNavigationDontProceed) {
  // Navigate to a page.
  GURL url1("http://www.google.com");
  test_rvh()->SendNavigate(1, url1);
  EXPECT_EQ(1, controller().GetEntryCount());

  // Show an interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL url2("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), false, url2, &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  // The interstitial should not show until its navigation has committed.
  EXPECT_FALSE(interstitial->is_showing());
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  // Let's commit the interstitial navigation.
  interstitial->TestDidNavigate(1, url2);
  EXPECT_TRUE(interstitial->is_showing());
  EXPECT_TRUE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == interstitial);
  NavigationEntry* entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  // The URL specified to the interstitial should have been ignored.
  EXPECT_TRUE(entry->GetURL() == url1);

  // Now don't proceed.
  interstitial->DontProceed();
  EXPECT_TRUE(deleted);
  EXPECT_EQ(TestInterstitialPage::CANCELED, state);
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  EXPECT_TRUE(entry->GetURL() == url1);
  EXPECT_EQ(1, controller().GetEntryCount());
}

// Test navigating to a page (with the navigation initiated from the browser,
// as when a URL is typed in the location bar) that shows an interstitial and
// creates a new navigation entry, then proceeding.
TEST_F(WebContentsImplTest,
       ShowInterstitialFromBrowserNewNavigationProceed) {
  // Navigate to a page.
  GURL url1("http://www.google.com");
  test_rvh()->SendNavigate(1, url1);
  EXPECT_EQ(1, controller().GetEntryCount());

  // Initiate a browser navigation that will trigger the interstitial
  controller().LoadURL(GURL("http://www.evil.com"), content::Referrer(),
                        content::PAGE_TRANSITION_TYPED, std::string());

  // Show an interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL url2("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, url2, &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  // The interstitial should not show until its navigation has committed.
  EXPECT_FALSE(interstitial->is_showing());
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  // Let's commit the interstitial navigation.
  interstitial->TestDidNavigate(1, url2);
  EXPECT_TRUE(interstitial->is_showing());
  EXPECT_TRUE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == interstitial);
  NavigationEntry* entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  EXPECT_TRUE(entry->GetURL() == url2);

  // Then proceed.
  interstitial->Proceed();
  // The interstitial should show until the new navigation commits.
  ASSERT_FALSE(deleted);
  EXPECT_EQ(TestInterstitialPage::OKED, state);
  EXPECT_TRUE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == interstitial);

  // Simulate the navigation to the page, that's when the interstitial gets
  // hidden.
  GURL url3("http://www.thepage.com");
  test_rvh()->SendNavigate(2, url3);

  EXPECT_TRUE(deleted);
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  EXPECT_TRUE(entry->GetURL() == url3);

  EXPECT_EQ(2, controller().GetEntryCount());
}

// Test navigating to a page (with the navigation initiated from the renderer,
// as when clicking on a link in the page) that shows an interstitial and
// creates a new navigation entry, then proceeding.
TEST_F(WebContentsImplTest,
       ShowInterstitialFromRendererNewNavigationProceed) {
  // Navigate to a page.
  GURL url1("http://www.google.com");
  test_rvh()->SendNavigate(1, url1);
  EXPECT_EQ(1, controller().GetEntryCount());

  // Show an interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL url2("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, url2, &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  // The interstitial should not show until its navigation has committed.
  EXPECT_FALSE(interstitial->is_showing());
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  // Let's commit the interstitial navigation.
  interstitial->TestDidNavigate(1, url2);
  EXPECT_TRUE(interstitial->is_showing());
  EXPECT_TRUE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == interstitial);
  NavigationEntry* entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  EXPECT_TRUE(entry->GetURL() == url2);

  // Then proceed.
  interstitial->Proceed();
  // The interstitial should show until the new navigation commits.
  ASSERT_FALSE(deleted);
  EXPECT_EQ(TestInterstitialPage::OKED, state);
  EXPECT_TRUE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == interstitial);

  // Simulate the navigation to the page, that's when the interstitial gets
  // hidden.
  GURL url3("http://www.thepage.com");
  test_rvh()->SendNavigate(2, url3);

  EXPECT_TRUE(deleted);
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  EXPECT_TRUE(entry->GetURL() == url3);

  EXPECT_EQ(2, controller().GetEntryCount());
}

// Test navigating to a page that shows an interstitial without creating a new
// navigation entry (this happens when the interstitial is triggered by a
// sub-resource in the page), then proceeding.
TEST_F(WebContentsImplTest, ShowInterstitialNoNewNavigationProceed) {
  // Navigate to a page so we have a navigation entry in the controller.
  GURL url1("http://www.google.com");
  test_rvh()->SendNavigate(1, url1);
  EXPECT_EQ(1, controller().GetEntryCount());

  // Show an interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL url2("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), false, url2, &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  // The interstitial should not show until its navigation has committed.
  EXPECT_FALSE(interstitial->is_showing());
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  // Let's commit the interstitial navigation.
  interstitial->TestDidNavigate(1, url2);
  EXPECT_TRUE(interstitial->is_showing());
  EXPECT_TRUE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == interstitial);
  NavigationEntry* entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  // The URL specified to the interstitial should have been ignored.
  EXPECT_TRUE(entry->GetURL() == url1);

  // Then proceed.
  interstitial->Proceed();
  // Since this is not a new navigation, the previous page is dismissed right
  // away and shows the original page.
  EXPECT_TRUE(deleted);
  EXPECT_EQ(TestInterstitialPage::OKED, state);
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  EXPECT_TRUE(entry->GetURL() == url1);

  EXPECT_EQ(1, controller().GetEntryCount());
}

// Test navigating to a page that shows an interstitial, then navigating away.
TEST_F(WebContentsImplTest, ShowInterstitialThenNavigate) {
  // Show interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL url("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, url, &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  interstitial->TestDidNavigate(1, url);

  // While interstitial showing, navigate to a new URL.
  const GURL url2("http://www.yahoo.com");
  test_rvh()->SendNavigate(1, url2);

  EXPECT_TRUE(deleted);
  EXPECT_EQ(TestInterstitialPage::CANCELED, state);
}

// Test navigating to a page that shows an interstitial, then going back.
TEST_F(WebContentsImplTest, ShowInterstitialThenGoBack) {
  // Navigate to a page so we have a navigation entry in the controller.
  GURL url1("http://www.google.com");
  test_rvh()->SendNavigate(1, url1);
  EXPECT_EQ(1, controller().GetEntryCount());

  // Show interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL interstitial_url("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, interstitial_url,
                               &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  interstitial->TestDidNavigate(2, interstitial_url);

  // While the interstitial is showing, go back.
  controller().GoBack();
  test_rvh()->SendNavigate(1, url1);

  // Make sure we are back to the original page and that the interstitial is
  // gone.
  EXPECT_TRUE(deleted);
  EXPECT_EQ(TestInterstitialPage::CANCELED, state);
  NavigationEntry* entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry);
  EXPECT_EQ(url1.spec(), entry->GetURL().spec());
}

// Test navigating to a page that shows an interstitial, has a renderer crash,
// and then goes back.
TEST_F(WebContentsImplTest, ShowInterstitialCrashRendererThenGoBack) {
  // Navigate to a page so we have a navigation entry in the controller.
  GURL url1("http://www.google.com");
  test_rvh()->SendNavigate(1, url1);
  EXPECT_EQ(1, controller().GetEntryCount());

  // Show interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL interstitial_url("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, interstitial_url,
                               &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  interstitial->TestDidNavigate(2, interstitial_url);

  // Crash the renderer
  test_rvh()->OnMessageReceived(
      ViewHostMsg_RenderViewGone(
          0, base::TERMINATION_STATUS_PROCESS_CRASHED, -1));

  // While the interstitial is showing, go back.
  controller().GoBack();
  test_rvh()->SendNavigate(1, url1);

  // Make sure we are back to the original page and that the interstitial is
  // gone.
  EXPECT_TRUE(deleted);
  EXPECT_EQ(TestInterstitialPage::CANCELED, state);
  NavigationEntry* entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry);
  EXPECT_EQ(url1.spec(), entry->GetURL().spec());
}

// Test navigating to a page that shows an interstitial, has the renderer crash,
// and then navigates to the interstitial.
TEST_F(WebContentsImplTest, ShowInterstitialCrashRendererThenNavigate) {
  // Navigate to a page so we have a navigation entry in the controller.
  GURL url1("http://www.google.com");
  test_rvh()->SendNavigate(1, url1);
  EXPECT_EQ(1, controller().GetEntryCount());

  // Show interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL interstitial_url("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, interstitial_url,
                               &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();

  // Crash the renderer
  test_rvh()->OnMessageReceived(
      ViewHostMsg_RenderViewGone(
          0, base::TERMINATION_STATUS_PROCESS_CRASHED, -1));

  interstitial->TestDidNavigate(2, interstitial_url);
}

// Test navigating to a page that shows an interstitial, then close the
// contents.
TEST_F(WebContentsImplTest, ShowInterstitialThenCloseTab) {
  // Show interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL url("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, url, &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  interstitial->TestDidNavigate(1, url);

  // Now close the contents.
  DeleteContents();
  EXPECT_TRUE(deleted);
  EXPECT_EQ(TestInterstitialPage::CANCELED, state);
}

// Test that after Proceed is called and an interstitial is still shown, no more
// commands get executed.
TEST_F(WebContentsImplTest, ShowInterstitialProceedMultipleCommands) {
  // Navigate to a page so we have a navigation entry in the controller.
  GURL url1("http://www.google.com");
  test_rvh()->SendNavigate(1, url1);
  EXPECT_EQ(1, controller().GetEntryCount());

  // Show an interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL url2("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, url2, &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  interstitial->TestDidNavigate(1, url2);

  // Run a command.
  EXPECT_EQ(0, interstitial->command_received_count());
  interstitial->TestDomOperationResponse("toto");
  EXPECT_EQ(1, interstitial->command_received_count());

  // Then proceed.
  interstitial->Proceed();
  ASSERT_FALSE(deleted);

  // While the navigation to the new page is pending, send other commands, they
  // should be ignored.
  interstitial->TestDomOperationResponse("hello");
  interstitial->TestDomOperationResponse("hi");
  EXPECT_EQ(1, interstitial->command_received_count());
}

// Test showing an interstitial while another interstitial is already showing.
TEST_F(WebContentsImplTest, ShowInterstitialOnInterstitial) {
  // Navigate to a page so we have a navigation entry in the controller.
  GURL start_url("http://www.google.com");
  test_rvh()->SendNavigate(1, start_url);
  EXPECT_EQ(1, controller().GetEntryCount());

  // Show an interstitial.
  TestInterstitialPage::InterstitialState state1 =
      TestInterstitialPage::UNDECIDED;
  bool deleted1 = false;
  GURL url1("http://interstitial1");
  TestInterstitialPage* interstitial1 =
      new TestInterstitialPage(contents(), true, url1, &state1, &deleted1);
  TestInterstitialPageStateGuard state_guard1(interstitial1);
  interstitial1->Show();
  interstitial1->TestDidNavigate(1, url1);

  // Now show another interstitial.
  TestInterstitialPage::InterstitialState state2 =
      TestInterstitialPage::UNDECIDED;
  bool deleted2 = false;
  GURL url2("http://interstitial2");
  TestInterstitialPage* interstitial2 =
      new TestInterstitialPage(contents(), true, url2, &state2, &deleted2);
  TestInterstitialPageStateGuard state_guard2(interstitial2);
  interstitial2->Show();
  interstitial2->TestDidNavigate(1, url2);

  // Showing interstitial2 should have caused interstitial1 to go away.
  EXPECT_TRUE(deleted1);
  EXPECT_EQ(TestInterstitialPage::CANCELED, state1);

  // Let's make sure interstitial2 is working as intended.
  ASSERT_FALSE(deleted2);
  EXPECT_EQ(TestInterstitialPage::UNDECIDED, state2);
  interstitial2->Proceed();
  GURL landing_url("http://www.thepage.com");
  test_rvh()->SendNavigate(2, landing_url);

  EXPECT_TRUE(deleted2);
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  NavigationEntry* entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  EXPECT_TRUE(entry->GetURL() == landing_url);
  EXPECT_EQ(2, controller().GetEntryCount());
}

// Test showing an interstitial, proceeding and then navigating to another
// interstitial.
TEST_F(WebContentsImplTest, ShowInterstitialProceedShowInterstitial) {
  // Navigate to a page so we have a navigation entry in the controller.
  GURL start_url("http://www.google.com");
  test_rvh()->SendNavigate(1, start_url);
  EXPECT_EQ(1, controller().GetEntryCount());

  // Show an interstitial.
  TestInterstitialPage::InterstitialState state1 =
      TestInterstitialPage::UNDECIDED;
  bool deleted1 = false;
  GURL url1("http://interstitial1");
  TestInterstitialPage* interstitial1 =
      new TestInterstitialPage(contents(), true, url1, &state1, &deleted1);
  TestInterstitialPageStateGuard state_guard1(interstitial1);
  interstitial1->Show();
  interstitial1->TestDidNavigate(1, url1);

  // Take action.  The interstitial won't be hidden until the navigation is
  // committed.
  interstitial1->Proceed();
  EXPECT_EQ(TestInterstitialPage::OKED, state1);

  // Now show another interstitial (simulating the navigation causing another
  // interstitial).
  TestInterstitialPage::InterstitialState state2 =
      TestInterstitialPage::UNDECIDED;
  bool deleted2 = false;
  GURL url2("http://interstitial2");
  TestInterstitialPage* interstitial2 =
      new TestInterstitialPage(contents(), true, url2, &state2, &deleted2);
  TestInterstitialPageStateGuard state_guard2(interstitial2);
  interstitial2->Show();
  interstitial2->TestDidNavigate(1, url2);

  // Showing interstitial2 should have caused interstitial1 to go away.
  EXPECT_TRUE(deleted1);

  // Let's make sure interstitial2 is working as intended.
  ASSERT_FALSE(deleted2);
  EXPECT_EQ(TestInterstitialPage::UNDECIDED, state2);
  interstitial2->Proceed();
  GURL landing_url("http://www.thepage.com");
  test_rvh()->SendNavigate(2, landing_url);

  EXPECT_TRUE(deleted2);
  EXPECT_FALSE(contents()->ShowingInterstitialPage());
  EXPECT_TRUE(contents()->GetInterstitialPage() == NULL);
  NavigationEntry* entry = controller().GetActiveEntry();
  ASSERT_TRUE(entry != NULL);
  EXPECT_TRUE(entry->GetURL() == landing_url);
  EXPECT_EQ(2, controller().GetEntryCount());
}

// Test that navigating away from an interstitial while it's loading cause it
// not to show.
TEST_F(WebContentsImplTest, NavigateBeforeInterstitialShows) {
  // Show an interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL interstitial_url("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, interstitial_url,
                               &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();

  // Let's simulate a navigation initiated from the browser before the
  // interstitial finishes loading.
  const GURL url("http://www.google.com");
  controller().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  ASSERT_FALSE(deleted);
  EXPECT_FALSE(interstitial->is_showing());

  // Now let's make the interstitial navigation commit.
  interstitial->TestDidNavigate(1, interstitial_url);

  // After it loaded the interstitial should be gone.
  EXPECT_TRUE(deleted);
  EXPECT_EQ(TestInterstitialPage::CANCELED, state);
}

// Test that a new request to show an interstitial while an interstitial is
// pending does not cause problems. htp://crbug/29655 and htp://crbug/9442.
TEST_F(WebContentsImplTest, TwoQuickInterstitials) {
  GURL interstitial_url("http://interstitial");

  // Show a first interstitial.
  TestInterstitialPage::InterstitialState state1 =
      TestInterstitialPage::UNDECIDED;
  bool deleted1 = false;
  TestInterstitialPage* interstitial1 =
      new TestInterstitialPage(contents(), true, interstitial_url,
                               &state1, &deleted1);
  TestInterstitialPageStateGuard state_guard1(interstitial1);
  interstitial1->Show();

  // Show another interstitial on that same contents before the first one had
  // time to load.
  TestInterstitialPage::InterstitialState state2 =
      TestInterstitialPage::UNDECIDED;
  bool deleted2 = false;
  TestInterstitialPage* interstitial2 =
      new TestInterstitialPage(contents(), true, interstitial_url,
                               &state2, &deleted2);
  TestInterstitialPageStateGuard state_guard2(interstitial2);
  interstitial2->Show();

  // The first interstitial should have been closed and deleted.
  EXPECT_TRUE(deleted1);
  EXPECT_EQ(TestInterstitialPage::CANCELED, state1);

  // The 2nd one should still be OK.
  ASSERT_FALSE(deleted2);
  EXPECT_EQ(TestInterstitialPage::UNDECIDED, state2);

  // Make the interstitial navigation commit it should be showing.
  interstitial2->TestDidNavigate(1, interstitial_url);
  EXPECT_EQ(interstitial2, contents()->GetInterstitialPage());
}

// Test showing an interstitial and have its renderer crash.
TEST_F(WebContentsImplTest, InterstitialCrasher) {
  // Show an interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL url("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, url, &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  // Simulate a renderer crash before the interstitial is shown.
  interstitial->TestRenderViewGone(
      base::TERMINATION_STATUS_PROCESS_CRASHED, -1);
  // The interstitial should have been dismissed.
  EXPECT_TRUE(deleted);
  EXPECT_EQ(TestInterstitialPage::CANCELED, state);

  // Now try again but this time crash the intersitial after it was shown.
  interstitial =
      new TestInterstitialPage(contents(), true, url, &state, &deleted);
  interstitial->Show();
  interstitial->TestDidNavigate(1, url);
  // Simulate a renderer crash.
  interstitial->TestRenderViewGone(
      base::TERMINATION_STATUS_PROCESS_CRASHED, -1);
  // The interstitial should have been dismissed.
  EXPECT_TRUE(deleted);
  EXPECT_EQ(TestInterstitialPage::CANCELED, state);
}

// Tests that showing an interstitial as a result of a browser initiated
// navigation while an interstitial is showing does not remove the pending
// entry (see http://crbug.com/9791).
TEST_F(WebContentsImplTest, NewInterstitialDoesNotCancelPendingEntry) {
  const char kUrl[] = "http://www.badguys.com/";
  const GURL kGURL(kUrl);

  // Start a navigation to a page
  contents()->GetController().LoadURL(
      kGURL, content::Referrer(), content::PAGE_TRANSITION_TYPED,
      std::string());

  // Simulate that navigation triggering an interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, kGURL, &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  interstitial->TestDidNavigate(1, kGURL);

  // Initiate a new navigation from the browser that also triggers an
  // interstitial.
  contents()->GetController().LoadURL(
      kGURL, content::Referrer(), content::PAGE_TRANSITION_TYPED,
      std::string());
  TestInterstitialPage::InterstitialState state2 =
      TestInterstitialPage::UNDECIDED;
  bool deleted2 = false;
  TestInterstitialPage* interstitial2 =
      new TestInterstitialPage(contents(), true, kGURL, &state, &deleted);
  TestInterstitialPageStateGuard state_guard2(interstitial2);
  interstitial2->Show();
  interstitial2->TestDidNavigate(1, kGURL);

  // Make sure we still have an entry.
  NavigationEntry* entry = contents()->GetController().GetPendingEntry();
  ASSERT_TRUE(entry);
  EXPECT_EQ(kUrl, entry->GetURL().spec());

  // And that the first interstitial is gone, but not the second.
  EXPECT_TRUE(deleted);
  EXPECT_EQ(TestInterstitialPage::CANCELED, state);
  EXPECT_FALSE(deleted2);
  EXPECT_EQ(TestInterstitialPage::UNDECIDED, state2);
}

// Tests that Javascript messages are not shown while an interstitial is
// showing.
TEST_F(WebContentsImplTest, NoJSMessageOnInterstitials) {
  const char kUrl[] = "http://www.badguys.com/";
  const GURL kGURL(kUrl);

  // Start a navigation to a page
  contents()->GetController().LoadURL(
      kGURL, content::Referrer(), content::PAGE_TRANSITION_TYPED,
      std::string());
  // DidNavigate from the page
  contents()->TestDidNavigate(rvh(), 1, kGURL, content::PAGE_TRANSITION_TYPED);

  // Simulate showing an interstitial while the page is showing.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, kGURL, &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  interstitial->TestDidNavigate(1, kGURL);

  // While the interstitial is showing, let's simulate the hidden page
  // attempting to show a JS message.
  IPC::Message* dummy_message = new IPC::Message;
  bool did_suppress_message = false;
  contents()->RunJavaScriptMessage(contents()->GetRenderViewHost(),
      ASCIIToUTF16("This is an informative message"), ASCIIToUTF16("OK"),
      kGURL, content::JAVASCRIPT_MESSAGE_TYPE_ALERT, dummy_message,
      &did_suppress_message);
  EXPECT_TRUE(did_suppress_message);
}

// Makes sure that if the source passed to CopyStateFromAndPrune has an
// interstitial it isn't copied over to the destination.
TEST_F(WebContentsImplTest, CopyStateFromAndPruneSourceInterstitial) {
  // Navigate to a page.
  GURL url1("http://www.google.com");
  test_rvh()->SendNavigate(1, url1);
  EXPECT_EQ(1, controller().GetEntryCount());

  // Initiate a browser navigation that will trigger the interstitial
  controller().LoadURL(GURL("http://www.evil.com"), content::Referrer(),
                        content::PAGE_TRANSITION_TYPED, std::string());

  // Show an interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL url2("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(contents(), true, url2, &state, &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  interstitial->TestDidNavigate(1, url2);
  EXPECT_TRUE(interstitial->is_showing());
  EXPECT_EQ(2, controller().GetEntryCount());

  // Create another NavigationController.
  GURL url3("http://foo2");
  scoped_ptr<TestWebContents> other_contents(
      static_cast<TestWebContents*>(CreateTestWebContents()));
  NavigationControllerImpl& other_controller =
      other_contents->GetControllerImpl();
  other_contents->NavigateAndCommit(url3);
  other_contents->ExpectSetHistoryLengthAndPrune(
      NavigationEntryImpl::FromNavigationEntry(
          other_controller.GetEntryAtIndex(0))->site_instance(), 1,
      other_controller.GetEntryAtIndex(0)->GetPageID());
  other_controller.CopyStateFromAndPrune(&controller());

  // The merged controller should only have two entries: url1 and url2.
  ASSERT_EQ(2, other_controller.GetEntryCount());
  EXPECT_EQ(1, other_controller.GetCurrentEntryIndex());
  EXPECT_EQ(url1, other_controller.GetEntryAtIndex(0)->GetURL());
  EXPECT_EQ(url3, other_controller.GetEntryAtIndex(1)->GetURL());

  // And the merged controller shouldn't be showing an interstitial.
  EXPECT_FALSE(other_contents->ShowingInterstitialPage());
}

// Makes sure that CopyStateFromAndPrune does the right thing if the object
// CopyStateFromAndPrune is invoked on is showing an interstitial.
TEST_F(WebContentsImplTest, CopyStateFromAndPruneTargetInterstitial) {
  // Navigate to a page.
  GURL url1("http://www.google.com");
  contents()->NavigateAndCommit(url1);

  // Create another NavigationController.
  scoped_ptr<TestWebContents> other_contents(
      static_cast<TestWebContents*>(CreateTestWebContents()));
  NavigationControllerImpl& other_controller =
      other_contents->GetControllerImpl();

  // Navigate it to url2.
  GURL url2("http://foo2");
  other_contents->NavigateAndCommit(url2);

  // Show an interstitial.
  TestInterstitialPage::InterstitialState state =
      TestInterstitialPage::UNDECIDED;
  bool deleted = false;
  GURL url3("http://interstitial");
  TestInterstitialPage* interstitial =
      new TestInterstitialPage(other_contents.get(), true, url3, &state,
                               &deleted);
  TestInterstitialPageStateGuard state_guard(interstitial);
  interstitial->Show();
  interstitial->TestDidNavigate(1, url3);
  EXPECT_TRUE(interstitial->is_showing());
  EXPECT_EQ(2, other_controller.GetEntryCount());
  other_contents->ExpectSetHistoryLengthAndPrune(
      NavigationEntryImpl::FromNavigationEntry(
          other_controller.GetEntryAtIndex(0))->site_instance(), 1,
      other_controller.GetEntryAtIndex(0)->GetPageID());
  other_controller.CopyStateFromAndPrune(&controller());

  // The merged controller should only have two entries: url1 and url2.
  ASSERT_EQ(2, other_controller.GetEntryCount());
  EXPECT_EQ(1, other_controller.GetCurrentEntryIndex());
  EXPECT_EQ(url1, other_controller.GetEntryAtIndex(0)->GetURL());
  EXPECT_EQ(url3, other_controller.GetEntryAtIndex(1)->GetURL());

  // It should have a transient entry.
  EXPECT_TRUE(other_controller.GetTransientEntry());

  // And the interstitial should be showing.
  EXPECT_TRUE(other_contents->ShowingInterstitialPage());

  // And the interstitial should do a reload on don't proceed.
  EXPECT_TRUE(static_cast<InterstitialPageImpl*>(
      other_contents->GetInterstitialPage())->reload_on_dont_proceed());
}
