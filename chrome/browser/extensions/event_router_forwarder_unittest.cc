// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/event_router_forwarder.h"

#include "base/bind.h"
#include "base/message_loop.h"
#include "base/system_monitor/system_monitor.h"
#include "base/test/thread_test_helper.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "content/public/test/test_browser_thread.h"
#include "googleurl/src/gurl.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;

namespace extensions {

namespace {

const char kEventName[] = "event_name";
const char kEventArgs[] = "event_args";
const char kExt[] = "extension";

class MockEventRouterForwarder : public EventRouterForwarder {
 public:
  MOCK_METHOD6(CallEventRouter,
      void(Profile*, const std::string&, const std::string&, const std::string&,
           Profile*, const GURL&));

 protected:
  virtual ~MockEventRouterForwarder() {}
};

}  // namespace

class EventRouterForwarderTest : public testing::Test {
 protected:
  EventRouterForwarderTest()
      : ui_thread_(BrowserThread::UI, &message_loop_),
        io_thread_(BrowserThread::IO),
        profile_manager_(
            static_cast<TestingBrowserProcess*>(g_browser_process)) {
#if defined(OS_MACOSX)
    base::SystemMonitor::AllocateSystemIOPorts();
#endif
    dummy.reset(new base::SystemMonitor);
  }

  virtual void SetUp() {
    ASSERT_TRUE(profile_manager_.SetUp());

    // Inject a BrowserProcess with a ProfileManager.
    ASSERT_TRUE(io_thread_.Start());

    profile1_ = profile_manager_.CreateTestingProfile("one");
    profile2_ = profile_manager_.CreateTestingProfile("two");
  }

  TestingProfile* CreateIncognitoProfile(TestingProfile* base) {
    TestingProfile* incognito = new TestingProfile;  // Owned by |base|.
    incognito->set_incognito(true);
    base->SetOffTheRecordProfile(incognito);
    return incognito;
  }

  MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread io_thread_;
  TestingProfileManager profile_manager_;
  scoped_ptr<base::SystemMonitor> dummy;
  // Profiles are weak pointers, owned by ProfileManager in |browser_process_|.
  TestingProfile* profile1_;
  TestingProfile* profile2_;
};

TEST_F(EventRouterForwarderTest, BroadcastRendererUI) {
  scoped_refptr<MockEventRouterForwarder> event_router(
      new MockEventRouterForwarder);
  GURL url;
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile1_, "", kEventName, kEventArgs, profile1_, url));
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile2_, "", kEventName, kEventArgs, profile2_, url));
  event_router->BroadcastEventToRenderers(kEventName, kEventArgs, url);
}

TEST_F(EventRouterForwarderTest, BroadcastRendererUIIncognito) {
  scoped_refptr<MockEventRouterForwarder> event_router(
      new MockEventRouterForwarder);
  using ::testing::_;
  GURL url;
  Profile* incognito = CreateIncognitoProfile(profile1_);
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile1_, "", kEventName, kEventArgs, profile1_, url));
  EXPECT_CALL(*event_router,
      CallEventRouter(incognito, _, _, _, _, _)).Times(0);
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile2_, "", kEventName, kEventArgs, profile2_, url));
  event_router->BroadcastEventToRenderers(kEventName, kEventArgs, url);
}

// This is the canonical test for passing control flow from the IO thread
// to the UI thread. Repeating this for all public functions of
// EventRouterForwarder would not increase coverage.
TEST_F(EventRouterForwarderTest, BroadcastRendererIO) {
  scoped_refptr<MockEventRouterForwarder> event_router(
      new MockEventRouterForwarder);
  GURL url;
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile1_, "", kEventName, kEventArgs, profile1_, url));
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile2_, "", kEventName, kEventArgs, profile2_, url));
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
      base::Bind(
          &MockEventRouterForwarder::BroadcastEventToRenderers,
          event_router.get(),
          std::string(kEventName), std::string(kEventArgs), url));

  // Wait for IO thread's message loop to be processed
  scoped_refptr<base::ThreadTestHelper> helper(
      new base::ThreadTestHelper(
          BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO)));
  ASSERT_TRUE(helper->Run());

  MessageLoop::current()->RunAllPending();
}

TEST_F(EventRouterForwarderTest, UnicastRendererUIRestricted) {
  scoped_refptr<MockEventRouterForwarder> event_router(
      new MockEventRouterForwarder);
  using ::testing::_;
  GURL url;
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile1_, "", kEventName, kEventArgs, profile1_, url));
  EXPECT_CALL(*event_router,
      CallEventRouter(profile2_, _, _, _, _, _)).Times(0);
  event_router->DispatchEventToRenderers(kEventName, kEventArgs,
                                         profile1_, true, url);
}

TEST_F(EventRouterForwarderTest, UnicastRendererUIRestrictedIncognito1) {
  scoped_refptr<MockEventRouterForwarder> event_router(
      new MockEventRouterForwarder);
  Profile* incognito = CreateIncognitoProfile(profile1_);
  using ::testing::_;
  GURL url;
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile1_, "", kEventName, kEventArgs, profile1_, url));
  EXPECT_CALL(*event_router,
      CallEventRouter(incognito, _, _, _, _, _)).Times(0);
  EXPECT_CALL(*event_router,
      CallEventRouter(profile2_, _, _, _, _, _)).Times(0);
  event_router->DispatchEventToRenderers(kEventName, kEventArgs,
                                         profile1_, true, url);
}

TEST_F(EventRouterForwarderTest, UnicastRendererUIRestrictedIncognito2) {
  scoped_refptr<MockEventRouterForwarder> event_router(
      new MockEventRouterForwarder);
  Profile* incognito = CreateIncognitoProfile(profile1_);
  using ::testing::_;
  GURL url;
  EXPECT_CALL(*event_router,
      CallEventRouter(profile1_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(*event_router,
      CallEventRouter(
          incognito, "", kEventName, kEventArgs, incognito, url));
  EXPECT_CALL(*event_router,
      CallEventRouter(profile2_, _, _, _, _, _)).Times(0);
  event_router->DispatchEventToRenderers(kEventName, kEventArgs,
                                         incognito, true, url);
}

TEST_F(EventRouterForwarderTest, UnicastRendererUIUnrestricted) {
  scoped_refptr<MockEventRouterForwarder> event_router(
      new MockEventRouterForwarder);
  using ::testing::_;
  GURL url;
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile1_, "", kEventName, kEventArgs, NULL, url));
  EXPECT_CALL(*event_router,
      CallEventRouter(profile2_, _, _, _, _, _)).Times(0);
  event_router->DispatchEventToRenderers(kEventName, kEventArgs,
                                         profile1_, false, url);
}

TEST_F(EventRouterForwarderTest, UnicastRendererUIUnrestrictedIncognito) {
  scoped_refptr<MockEventRouterForwarder> event_router(
      new MockEventRouterForwarder);
  Profile* incognito = CreateIncognitoProfile(profile1_);
  using ::testing::_;
  GURL url;
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile1_, "", kEventName, kEventArgs, NULL, url));
  EXPECT_CALL(*event_router,
      CallEventRouter(incognito, _, _, _, _, _)).Times(0);
  EXPECT_CALL(*event_router,
      CallEventRouter(profile2_, _, _, _, _, _)).Times(0);
  event_router->DispatchEventToRenderers(kEventName, kEventArgs,
                                         profile1_, false, url);
}

TEST_F(EventRouterForwarderTest, BroadcastExtensionUI) {
  scoped_refptr<MockEventRouterForwarder> event_router(
      new MockEventRouterForwarder);
  GURL url;
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile1_, kExt, kEventName, kEventArgs, profile1_, url));
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile2_, kExt, kEventName, kEventArgs, profile2_, url));
  event_router->BroadcastEventToExtension(kExt, kEventName, kEventArgs, url);
}

TEST_F(EventRouterForwarderTest, UnicastExtensionUIRestricted) {
  scoped_refptr<MockEventRouterForwarder> event_router(
      new MockEventRouterForwarder);
  using ::testing::_;
  GURL url;
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile1_, kExt, kEventName, kEventArgs, profile1_, url));
  EXPECT_CALL(*event_router,
      CallEventRouter(profile2_, _, _, _, _, _)).Times(0);
  event_router->DispatchEventToExtension(kExt, kEventName, kEventArgs,
                                         profile1_, true, url);
}

TEST_F(EventRouterForwarderTest, UnicastExtensionUIUnrestricted) {
  scoped_refptr<MockEventRouterForwarder> event_router(
      new MockEventRouterForwarder);
  using ::testing::_;
  GURL url;
  EXPECT_CALL(*event_router,
      CallEventRouter(
          profile1_, kExt, kEventName, kEventArgs, NULL, url));
  EXPECT_CALL(*event_router,
      CallEventRouter(profile2_, _, _, _, _, _)).Times(0);
  event_router->DispatchEventToExtension(kExt, kEventName, kEventArgs,
                                         profile1_, false, url);
}

}  // namespace extensions
