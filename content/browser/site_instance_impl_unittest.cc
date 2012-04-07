// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/compiler_specific.h"
#include "base/stl_util.h"
#include "base/string16.h"
#include "content/browser/browser_thread_impl.h"
#include "content/browser/browsing_instance.h"
#include "content/browser/child_process_security_policy.h"
#include "content/browser/mock_content_browser_client.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/renderer_host/test_render_view_host.h"
#include "content/browser/site_instance_impl.h"
#include "content/browser/tab_contents/navigation_entry_impl.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/public/browser/web_ui_controller_factory.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/url_constants.h"
#include "content/test/test_browser_context.h"
#include "googleurl/src/url_util.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserContext;
using content::BrowserThread;
using content::BrowserThreadImpl;
using content::NavigationEntry;
using content::NavigationEntryImpl;
using content::SiteInstance;
using content::WebUI;
using content::WebUIController;

namespace {

const char kSameAsAnyInstanceURL[] = "about:internets";

const char kPrivilegedScheme[] = "privileged";

class SiteInstanceTestWebUIControllerFactory
    : public content::WebUIControllerFactory {
 public:
  virtual WebUIController* CreateWebUIControllerForURL(
      WebUI* web_ui, const GURL& url) const OVERRIDE {
    return NULL;
  }
  virtual WebUI::TypeID GetWebUIType(BrowserContext* browser_context,
      const GURL& url) const OVERRIDE {
    return WebUI::kNoWebUI;
  }
  virtual bool UseWebUIForURL(BrowserContext* browser_context,
                              const GURL& url) const OVERRIDE {
    return HasWebUIScheme(url);
  }
  virtual bool UseWebUIBindingsForURL(BrowserContext* browser_context,
                                      const GURL& url) const OVERRIDE {
    return HasWebUIScheme(url);
  }
  virtual bool HasWebUIScheme(const GURL& url) const OVERRIDE {
    return url.SchemeIs(chrome::kChromeUIScheme);
  }
  virtual bool IsURLAcceptableForWebUI(BrowserContext* browser_context,
      const GURL& url) const OVERRIDE {
    return false;
  }
};

class SiteInstanceTestBrowserClient : public content::MockContentBrowserClient {
 public:
  SiteInstanceTestBrowserClient()
      : privileged_process_id_(-1) {
  }

  virtual content::WebUIControllerFactory*
      GetWebUIControllerFactory() OVERRIDE {
    return &factory_;
  }

  virtual bool ShouldUseProcessPerSite(BrowserContext* browser_context,
                                       const GURL& effective_url) OVERRIDE {
    return false;
  }

  virtual bool IsURLSameAsAnySiteInstance(const GURL& url) OVERRIDE {
    return url == GURL(kSameAsAnyInstanceURL) ||
           url == GURL(chrome::kAboutCrashURL);
  }

  virtual bool IsSuitableHost(content::RenderProcessHost* process_host,
                              const GURL& site_url) OVERRIDE {
    return (privileged_process_id_ == process_host->GetID()) ==
        site_url.SchemeIs(kPrivilegedScheme);
  }

  void set_privileged_process_id(int process_id) {
    privileged_process_id_ = process_id;
  }

 private:
  SiteInstanceTestWebUIControllerFactory factory_;
  int privileged_process_id_;
};

class SiteInstanceTest : public testing::Test {
 public:
  SiteInstanceTest()
      : ui_thread_(BrowserThread::UI, &message_loop_),
        old_browser_client_(NULL) {
  }

  virtual void SetUp() {
    old_browser_client_ = content::GetContentClient()->browser();
    content::GetContentClient()->set_browser(&browser_client_);
    url_util::AddStandardScheme(kPrivilegedScheme);
    url_util::AddStandardScheme(chrome::kChromeUIScheme);
  }

  virtual void TearDown() {
    content::GetContentClient()->set_browser(old_browser_client_);
  }

  void set_privileged_process_id(int process_id) {
    browser_client_.set_privileged_process_id(process_id);
  }

 private:
  MessageLoopForUI message_loop_;
  BrowserThreadImpl ui_thread_;

  SiteInstanceTestBrowserClient browser_client_;
  content::ContentBrowserClient* old_browser_client_;
};

class TestBrowsingInstance : public BrowsingInstance {
 public:
  TestBrowsingInstance(BrowserContext* browser_context, int* delete_counter)
      : BrowsingInstance(browser_context),
        use_process_per_site_(false),
        delete_counter_(delete_counter) {
  }

  // Overrides BrowsingInstance::ShouldUseProcessPerSite so that we can test
  // both alternatives without using command-line switches.
  bool ShouldUseProcessPerSite(const GURL& url) {
    return use_process_per_site_;
  }

  void set_use_process_per_site(bool use_process_per_site) {
    use_process_per_site_ = use_process_per_site;
  }

  // Make a few methods public for tests.
  using BrowsingInstance::ShouldUseProcessPerSite;
  using BrowsingInstance::browser_context;
  using BrowsingInstance::HasSiteInstance;
  using BrowsingInstance::GetSiteInstanceForURL;
  using BrowsingInstance::RegisterSiteInstance;
  using BrowsingInstance::UnregisterSiteInstance;

 private:
  virtual ~TestBrowsingInstance() {
    (*delete_counter_)++;
  }

  // Set by individual tests.
  bool use_process_per_site_;

  int* delete_counter_;
};

class TestSiteInstance : public SiteInstanceImpl {
 public:
  static TestSiteInstance* CreateTestSiteInstance(
      BrowserContext* browser_context,
      int* site_delete_counter,
      int* browsing_delete_counter) {
    TestBrowsingInstance* browsing_instance =
        new TestBrowsingInstance(browser_context, browsing_delete_counter);
    return new TestSiteInstance(browsing_instance, site_delete_counter);
  }

 private:
  TestSiteInstance(BrowsingInstance* browsing_instance, int* delete_counter)
    : SiteInstanceImpl(browsing_instance), delete_counter_(delete_counter) {}
  virtual ~TestSiteInstance() {
    (*delete_counter_)++;
  }

  int* delete_counter_;
};

}  // namespace

// Test to ensure no memory leaks for SiteInstance objects.
TEST_F(SiteInstanceTest, SiteInstanceDestructor) {
  // The existence of these factories will cause TabContents to create our test
  // one instead of the real one.
  MockRenderProcessHostFactory rph_factory;
  TestRenderViewHostFactory rvh_factory(&rph_factory);
  int site_delete_counter = 0;
  int browsing_delete_counter = 0;
  const GURL url("test:foo");

  // Ensure that instances are deleted when their NavigationEntries are gone.
  TestSiteInstance* instance =
      TestSiteInstance::CreateTestSiteInstance(NULL, &site_delete_counter,
                                               &browsing_delete_counter);
  EXPECT_EQ(0, site_delete_counter);

  NavigationEntryImpl* e1 = new NavigationEntryImpl(
      instance, 0, url, content::Referrer(), string16(),
      content::PAGE_TRANSITION_LINK, false);

  // Redundantly setting e1's SiteInstance shouldn't affect the ref count.
  e1->set_site_instance(instance);
  EXPECT_EQ(0, site_delete_counter);

  // Add a second reference
  NavigationEntryImpl* e2 = new NavigationEntryImpl(
      instance, 0, url, content::Referrer(), string16(),
      content::PAGE_TRANSITION_LINK, false);

  // Now delete both entries and be sure the SiteInstance goes away.
  delete e1;
  EXPECT_EQ(0, site_delete_counter);
  EXPECT_EQ(0, browsing_delete_counter);
  delete e2;
  EXPECT_EQ(1, site_delete_counter);
  // instance is now deleted
  EXPECT_EQ(1, browsing_delete_counter);
  // browsing_instance is now deleted

  // Ensure that instances are deleted when their RenderViewHosts are gone.
  scoped_ptr<TestBrowserContext> browser_context(new TestBrowserContext());
  instance =
      TestSiteInstance::CreateTestSiteInstance(browser_context.get(),
                                               &site_delete_counter,
                                               &browsing_delete_counter);
  {
    TabContents contents(browser_context.get(),
                         instance,
                         MSG_ROUTING_NONE,
                         NULL,
                         NULL);
    EXPECT_EQ(1, site_delete_counter);
    EXPECT_EQ(1, browsing_delete_counter);
  }

  // Make sure that we flush any messages related to the above TabContents
  // destruction.
  MessageLoop::current()->RunAllPending();

  EXPECT_EQ(2, site_delete_counter);
  EXPECT_EQ(2, browsing_delete_counter);
  // contents is now deleted, along with instance and browsing_instance
}

// Test that NavigationEntries with SiteInstances can be cloned, but that their
// SiteInstances can be changed afterwards.  Also tests that the ref counts are
// updated properly after the change.
TEST_F(SiteInstanceTest, CloneNavigationEntry) {
  int site_delete_counter1 = 0;
  int site_delete_counter2 = 0;
  int browsing_delete_counter = 0;
  const GURL url("test:foo");

  SiteInstanceImpl* instance1 =
      TestSiteInstance::CreateTestSiteInstance(NULL, &site_delete_counter1,
                                               &browsing_delete_counter);
  SiteInstanceImpl* instance2 =
      TestSiteInstance::CreateTestSiteInstance(NULL, &site_delete_counter2,
                                               &browsing_delete_counter);

  NavigationEntryImpl* e1 = new NavigationEntryImpl(
      instance1, 0, url, content::Referrer(), string16(),
      content::PAGE_TRANSITION_LINK, false);
  // Clone the entry
  NavigationEntryImpl* e2 = new NavigationEntryImpl(*e1);

  // Should be able to change the SiteInstance of the cloned entry.
  e2->set_site_instance(instance2);

  // The first SiteInstance should go away after deleting e1, since e2 should
  // no longer be referencing it.
  delete e1;
  EXPECT_EQ(1, site_delete_counter1);
  EXPECT_EQ(0, site_delete_counter2);

  // The second SiteInstance should go away after deleting e2.
  delete e2;
  EXPECT_EQ(1, site_delete_counter1);
  EXPECT_EQ(1, site_delete_counter2);

  // Both BrowsingInstances are also now deleted
  EXPECT_EQ(2, browsing_delete_counter);
}

// Test to ensure GetProcess returns and creates processes correctly.
TEST_F(SiteInstanceTest, GetProcess) {
  // Ensure that GetProcess returns a process.
  scoped_ptr<TestBrowserContext> browser_context(new TestBrowserContext());
  scoped_ptr<content::RenderProcessHost> host1;
  scoped_refptr<SiteInstanceImpl> instance(static_cast<SiteInstanceImpl*>(
      SiteInstance::Create(browser_context.get())));
  host1.reset(instance->GetProcess());
  EXPECT_TRUE(host1.get() != NULL);

  // Ensure that GetProcess creates a new process.
  scoped_refptr<SiteInstanceImpl> instance2(static_cast<SiteInstanceImpl*>(
      SiteInstance::Create(browser_context.get())));
  scoped_ptr<content::RenderProcessHost> host2(instance2->GetProcess());
  EXPECT_TRUE(host2.get() != NULL);
  EXPECT_NE(host1.get(), host2.get());
}

// Test to ensure SetSite and site() work properly.
TEST_F(SiteInstanceTest, SetSite) {
  scoped_refptr<SiteInstanceImpl> instance(static_cast<SiteInstanceImpl*>(
      SiteInstance::Create(NULL)));
  EXPECT_FALSE(instance->HasSite());
  EXPECT_TRUE(instance->GetSite().is_empty());

  instance->SetSite(GURL("http://www.google.com/index.html"));
  EXPECT_EQ(GURL("http://google.com"), instance->GetSite());

  EXPECT_TRUE(instance->HasSite());
}

// Test to ensure GetSiteForURL properly returns sites for URLs.
TEST_F(SiteInstanceTest, GetSiteForURL) {
  // Pages are irrelevant.
  GURL test_url = GURL("http://www.google.com/index.html");
  EXPECT_EQ(GURL("http://google.com"),
            SiteInstanceImpl::GetSiteForURL(NULL, test_url));

  // Ports are irrlevant.
  test_url = GURL("https://www.google.com:8080");
  EXPECT_EQ(GURL("https://google.com"),
            SiteInstanceImpl::GetSiteForURL(NULL, test_url));

  // Javascript URLs have no site.
  test_url = GURL("javascript:foo();");
  EXPECT_EQ(GURL(), SiteInstanceImpl::GetSiteForURL(NULL, test_url));

  test_url = GURL("http://foo/a.html");
  EXPECT_EQ(GURL("http://foo"), SiteInstanceImpl::GetSiteForURL(
      NULL, test_url));

  test_url = GURL("file:///C:/Downloads/");
  EXPECT_EQ(GURL(), SiteInstanceImpl::GetSiteForURL(NULL, test_url));

  // TODO(creis): Do we want to special case file URLs to ensure they have
  // either no site or a special "file://" site?  We currently return
  // "file://home/" as the site, which seems broken.
  // test_url = GURL("file://home/");
  // EXPECT_EQ(GURL(), SiteInstanceImpl::GetSiteForURL(NULL, test_url));
}

// Test of distinguishing URLs from different sites.  Most of this logic is
// tested in RegistryControlledDomainTest.  This test focuses on URLs with
// different schemes or ports.
TEST_F(SiteInstanceTest, IsSameWebSite) {
  GURL url_foo = GURL("http://foo/a.html");
  GURL url_foo2 = GURL("http://foo/b.html");
  GURL url_foo_https = GURL("https://foo/a.html");
  GURL url_foo_port = GURL("http://foo:8080/a.html");
  GURL url_javascript = GURL("javascript:alert(1);");
  GURL url_crash = GURL(chrome::kAboutCrashURL);
  GURL url_browser_specified = GURL(kSameAsAnyInstanceURL);

  // Same scheme and port -> same site.
  EXPECT_TRUE(SiteInstance::IsSameWebSite(NULL, url_foo, url_foo2));

  // Different scheme -> different site.
  EXPECT_FALSE(SiteInstance::IsSameWebSite(NULL, url_foo, url_foo_https));

  // Different port -> same site.
  // (Changes to document.domain make renderer ignore the port.)
  EXPECT_TRUE(SiteInstance::IsSameWebSite(NULL, url_foo, url_foo_port));

  // JavaScript links should be considered same site for anything.
  EXPECT_TRUE(SiteInstance::IsSameWebSite(NULL, url_javascript, url_foo));
  EXPECT_TRUE(SiteInstance::IsSameWebSite(NULL, url_javascript, url_foo_https));
  EXPECT_TRUE(SiteInstance::IsSameWebSite(NULL, url_javascript, url_foo_port));

  // The URLs specified by the ContentBrowserClient should also be treated as
  // same site.
  EXPECT_TRUE(SiteInstance::IsSameWebSite(NULL, url_crash, url_foo));
  EXPECT_TRUE(SiteInstance::IsSameWebSite(NULL, url_browser_specified,
                                          url_foo));
}

// Test to ensure that there is only one SiteInstance per site in a given
// BrowsingInstance, when process-per-site is not in use.
TEST_F(SiteInstanceTest, OneSiteInstancePerSite) {
  int delete_counter = 0;
  TestBrowsingInstance* browsing_instance =
      new TestBrowsingInstance(NULL, &delete_counter);
  browsing_instance->set_use_process_per_site(false);

  const GURL url_a1("http://www.google.com/1.html");
  scoped_refptr<SiteInstanceImpl> site_instance_a1(
      static_cast<SiteInstanceImpl*>(
          browsing_instance->GetSiteInstanceForURL(url_a1)));
  EXPECT_TRUE(site_instance_a1.get() != NULL);

  // A separate site should create a separate SiteInstance.
  const GURL url_b1("http://www.yahoo.com/");
  scoped_refptr<SiteInstanceImpl> site_instance_b1(
      static_cast<SiteInstanceImpl*>(
          browsing_instance->GetSiteInstanceForURL(url_b1)));
  EXPECT_NE(site_instance_a1.get(), site_instance_b1.get());

  // Getting the new SiteInstance from the BrowsingInstance and from another
  // SiteInstance in the BrowsingInstance should give the same result.
  EXPECT_EQ(site_instance_b1.get(),
            site_instance_a1->GetRelatedSiteInstance(url_b1));

  // A second visit to the original site should return the same SiteInstance.
  const GURL url_a2("http://www.google.com/2.html");
  EXPECT_EQ(site_instance_a1.get(),
            browsing_instance->GetSiteInstanceForURL(url_a2));
  EXPECT_EQ(site_instance_a1.get(),
            site_instance_a1->GetRelatedSiteInstance(url_a2));

  // A visit to the original site in a new BrowsingInstance (same or different
  // browser context) should return a different SiteInstance.
  TestBrowsingInstance* browsing_instance2 =
      new TestBrowsingInstance(NULL, &delete_counter);
  browsing_instance2->set_use_process_per_site(false);
  // Ensure the new SiteInstance is ref counted so that it gets deleted.
  scoped_refptr<SiteInstanceImpl> site_instance_a2_2(
      static_cast<SiteInstanceImpl*>(
          browsing_instance2->GetSiteInstanceForURL(url_a2)));
  EXPECT_NE(site_instance_a1.get(), site_instance_a2_2.get());

  // Should be able to see that we do have SiteInstances.
  EXPECT_TRUE(browsing_instance->HasSiteInstance(
      GURL("http://mail.google.com")));
  EXPECT_TRUE(browsing_instance2->HasSiteInstance(
      GURL("http://mail.google.com")));
  EXPECT_TRUE(browsing_instance->HasSiteInstance(
      GURL("http://mail.yahoo.com")));

  // Should be able to see that we don't have SiteInstances.
  EXPECT_FALSE(browsing_instance->HasSiteInstance(
      GURL("https://www.google.com")));
  EXPECT_FALSE(browsing_instance2->HasSiteInstance(
      GURL("http://www.yahoo.com")));

  // browsing_instances will be deleted when their SiteInstances are deleted
}

// Test to ensure that there is only one SiteInstance per site for an entire
// BrowserContext, if process-per-site is in use.
TEST_F(SiteInstanceTest, OneSiteInstancePerSiteInBrowserContext) {
  int delete_counter = 0;
  TestBrowsingInstance* browsing_instance =
      new TestBrowsingInstance(NULL, &delete_counter);
  browsing_instance->set_use_process_per_site(true);

  const GURL url_a1("http://www.google.com/1.html");
  scoped_refptr<SiteInstanceImpl> site_instance_a1(
      static_cast<SiteInstanceImpl*>(
          browsing_instance->GetSiteInstanceForURL(url_a1)));
  EXPECT_TRUE(site_instance_a1.get() != NULL);

  // A separate site should create a separate SiteInstance.
  const GURL url_b1("http://www.yahoo.com/");
  scoped_refptr<SiteInstanceImpl> site_instance_b1(
      static_cast<SiteInstanceImpl*>(
          browsing_instance->GetSiteInstanceForURL(url_b1)));
  EXPECT_NE(site_instance_a1.get(), site_instance_b1.get());

  // Getting the new SiteInstance from the BrowsingInstance and from another
  // SiteInstance in the BrowsingInstance should give the same result.
  EXPECT_EQ(site_instance_b1.get(),
            site_instance_a1->GetRelatedSiteInstance(url_b1));

  // A second visit to the original site should return the same SiteInstance.
  const GURL url_a2("http://www.google.com/2.html");
  EXPECT_EQ(site_instance_a1.get(),
            browsing_instance->GetSiteInstanceForURL(url_a2));
  EXPECT_EQ(site_instance_a1.get(),
            site_instance_a1->GetRelatedSiteInstance(url_a2));

  // A visit to the original site in a new BrowsingInstance (same browser
  // context) should also return the same SiteInstance.
  // This BrowsingInstance doesn't get its own SiteInstance within the test, so
  // it won't be deleted by its children.  Thus, we'll keep a ref count to it
  // to make sure it gets deleted.
  scoped_refptr<TestBrowsingInstance> browsing_instance2(
      new TestBrowsingInstance(NULL, &delete_counter));
  browsing_instance2->set_use_process_per_site(true);
  EXPECT_EQ(site_instance_a1.get(),
            browsing_instance2->GetSiteInstanceForURL(url_a2));

  // A visit to the original site in a new BrowsingInstance (different browser
  // context) should return a different SiteInstance.
  scoped_ptr<TestBrowserContext> browser_context(new TestBrowserContext());
  TestBrowsingInstance* browsing_instance3 =
      new TestBrowsingInstance(browser_context.get(), &delete_counter);
  browsing_instance3->set_use_process_per_site(true);
  // Ensure the new SiteInstance is ref counted so that it gets deleted.
  scoped_refptr<SiteInstanceImpl> site_instance_a2_3(
      static_cast<SiteInstanceImpl*>(
          browsing_instance3->GetSiteInstanceForURL(url_a2)));
  EXPECT_NE(site_instance_a1.get(), site_instance_a2_3.get());

  // Should be able to see that we do have SiteInstances.
  EXPECT_TRUE(browsing_instance->HasSiteInstance(
      GURL("http://mail.google.com")));  // visited before
  EXPECT_TRUE(browsing_instance2->HasSiteInstance(
      GURL("http://mail.google.com")));  // visited before
  EXPECT_TRUE(browsing_instance->HasSiteInstance(
      GURL("http://mail.yahoo.com")));  // visited before
  EXPECT_TRUE(browsing_instance2->HasSiteInstance(
      GURL("http://www.yahoo.com")));  // different BI, but same browser context

  // Should be able to see that we don't have SiteInstances.
  EXPECT_FALSE(browsing_instance->HasSiteInstance(
      GURL("https://www.google.com")));  // not visited before
  EXPECT_FALSE(browsing_instance3->HasSiteInstance(
      GURL("http://www.yahoo.com")));  // different BI, different context

  // browsing_instances will be deleted when their SiteInstances are deleted
}

static SiteInstanceImpl* CreateSiteInstance(
    content::RenderProcessHostFactory* factory, const GURL& url) {
  SiteInstanceImpl* instance =
      reinterpret_cast<SiteInstanceImpl*>(
          SiteInstance::CreateForURL(NULL, url));
  instance->set_render_process_host_factory(factory);
  return instance;
}

// Test to ensure that pages that require certain privileges are grouped
// in processes with similar pages.
TEST_F(SiteInstanceTest, ProcessSharingByType) {
  MockRenderProcessHostFactory rph_factory;
  ChildProcessSecurityPolicy* policy =
      ChildProcessSecurityPolicy::GetInstance();

  // Make a bunch of mock renderers so that we hit the limit.
  std::vector<MockRenderProcessHost*> hosts;
  for (size_t i = 0; i < content::kMaxRendererProcessCount; ++i)
    hosts.push_back(new MockRenderProcessHost(NULL));

  // Create some extension instances and make sure they share a process.
  scoped_refptr<SiteInstanceImpl> extension1_instance(
      CreateSiteInstance(&rph_factory,
          GURL(kPrivilegedScheme + std::string("://foo/bar"))));
  set_privileged_process_id(extension1_instance->GetProcess()->GetID());

  scoped_refptr<SiteInstanceImpl> extension2_instance(
      CreateSiteInstance(&rph_factory,
          GURL(kPrivilegedScheme + std::string("://baz/bar"))));

  scoped_ptr<content::RenderProcessHost> extension_host(
      extension1_instance->GetProcess());
  EXPECT_EQ(extension1_instance->GetProcess(),
            extension2_instance->GetProcess());

  // Create some WebUI instances and make sure they share a process.
  scoped_refptr<SiteInstanceImpl> webui1_instance(CreateSiteInstance(
      &rph_factory,
      GURL(chrome::kChromeUIScheme + std::string("://newtab"))));
  policy->GrantWebUIBindings(webui1_instance->GetProcess()->GetID());

  scoped_refptr<SiteInstanceImpl> webui2_instance(CreateSiteInstance(
      &rph_factory,
      GURL(chrome::kChromeUIScheme + std::string("://history"))));

  scoped_ptr<content::RenderProcessHost> dom_host(
      webui1_instance->GetProcess());
  EXPECT_EQ(webui1_instance->GetProcess(), webui2_instance->GetProcess());

  // Make sure none of differing privilege processes are mixed.
  EXPECT_NE(extension1_instance->GetProcess(), webui1_instance->GetProcess());

  for (size_t i = 0; i < content::kMaxRendererProcessCount; ++i) {
    EXPECT_NE(extension1_instance->GetProcess(), hosts[i]);
    EXPECT_NE(webui1_instance->GetProcess(), hosts[i]);
  }

  STLDeleteContainerPointers(hosts.begin(), hosts.end());
}

// Test to ensure that HasWrongProcessForURL behaves properly for different
// types of URLs.
TEST_F(SiteInstanceTest, HasWrongProcessForURL) {
  scoped_ptr<TestBrowserContext> browser_context(new TestBrowserContext());
  scoped_ptr<content::RenderProcessHost> host;
  scoped_refptr<SiteInstanceImpl> instance(static_cast<SiteInstanceImpl*>(
      SiteInstance::Create(browser_context.get())));

  EXPECT_FALSE(instance->HasSite());
  EXPECT_TRUE(instance->GetSite().is_empty());

  instance->SetSite(GURL("http://evernote.com/"));
  EXPECT_TRUE(instance->HasSite());

  // Check prior to "assigning" a process to the instance, which is expected
  // to return false due to not being attached to any process yet.
  EXPECT_FALSE(instance->HasWrongProcessForURL(GURL("http://google.com")));

  // The call to GetProcess actually creates a new real process, which works
  // fine, but might be a cause for problems in different contexts.
  host.reset(instance->GetProcess());
  EXPECT_TRUE(host.get() != NULL);
  EXPECT_TRUE(instance->HasProcess());

  EXPECT_FALSE(instance->HasWrongProcessForURL(GURL("http://evernote.com")));
  EXPECT_FALSE(instance->HasWrongProcessForURL(
      GURL("javascript:alert(document.location.href);")));

  EXPECT_TRUE(instance->HasWrongProcessForURL(GURL("chrome://settings")));
}
