// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_vector.h"
#include "chrome/browser/content_settings/tab_specific_content_settings.h"
#include "chrome/browser/geolocation/geolocation_content_settings_map.h"
#include "chrome/browser/tab_contents/confirm_infobar_delegate.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/tab_contents/test_tab_contents_wrapper.h"
#include "chrome/test/testing_profile.h"
#include "content/browser/browser_thread.h"
#include "content/browser/geolocation/arbitrator_dependency_factories_for_test.h"
#include "content/browser/geolocation/geolocation_permission_context.h"
#include "content/browser/geolocation/location_arbitrator.h"
#include "content/browser/geolocation/location_provider.h"
#include "content/browser/geolocation/mock_location_provider.h"
#include "content/browser/renderer_host/mock_render_process_host.h"
#include "content/browser/tab_contents/test_tab_contents.h"
#include "content/common/geolocation_messages.h"
#include "content/common/notification_details.h"
#include "content/common/notification_registrar.h"
#include "content/common/notification_source.h"
#include "content/common/notification_type.h"
#include "testing/gtest/include/gtest/gtest.h"

// TestTabContentsWithPendingInfoBar ------------------------------------------

namespace {

// TestTabContents short-circuits TAB_CONTENTS_INFOBAR_REMOVED to call
// InfoBarClosed() directly. We need to observe it and call InfoBarClosed()
// later.
class TestTabContentsWithPendingInfoBar : public TabContentsWrapper {
 public:
  TestTabContentsWithPendingInfoBar(Profile* profile, SiteInstance* instance);
  virtual ~TestTabContentsWithPendingInfoBar();

  // TabContentsWrapper:
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  InfoBarDelegate* removed_infobar_delegate_;
  NotificationRegistrar registrar_;
};

TestTabContentsWithPendingInfoBar::TestTabContentsWithPendingInfoBar(
    Profile* profile,
    SiteInstance* instance)
    : TabContentsWrapper(
          new TabContents(profile, instance, MSG_ROUTING_NONE, NULL, NULL)),
      removed_infobar_delegate_(NULL) {
  Source<TabContents> source(tab_contents());
  registrar_.Add(this, NotificationType::TAB_CONTENTS_INFOBAR_REMOVED,
                 source);
}

TestTabContentsWithPendingInfoBar::~TestTabContentsWithPendingInfoBar() {
}

void TestTabContentsWithPendingInfoBar::Observe(
    NotificationType type,
    const NotificationSource& source,
    const NotificationDetails& details) {
  if (type.value == NotificationType::TAB_CONTENTS_INFOBAR_REMOVED)
    removed_infobar_delegate_ = Details<InfoBarDelegate>(details).ptr();
  else
    TabContentsWrapper::Observe(type, source, details);
}

}  // namespace


// GeolocationPermissionContextTests ------------------------------------------

// This class sets up GeolocationArbitrator.
class GeolocationPermissionContextTests : public TabContentsWrapperTestHarness {
 public:
  GeolocationPermissionContextTests();

 protected:
  virtual ~GeolocationPermissionContextTests();

  int process_id() { return contents()->render_view_host()->process()->id(); }
  int process_id_for_tab(int tab) {
    return extra_tabs_[tab]->render_view_host()->process()->id();
  }
  int render_id() { return contents()->render_view_host()->routing_id(); }
  int render_id_for_tab(int tab) {
    return extra_tabs_[tab]->render_view_host()->routing_id();
  }
  int bridge_id() const { return 42; }  // Not relevant at this level.

  void CheckPermissionMessageSent(int bridge_id, bool allowed);
  void CheckPermissionMessageSentForTab(int tab, int bridge_id, bool allowed);
  void CheckPermissionMessageSentInternal(MockRenderProcessHost* process,
                                          int bridge_id,
                                          bool allowed);
  void AddNewTab(const GURL& url);
  void CheckTabContentsState(const GURL& requesting_frame,
                             ContentSetting expected_content_setting);

  TestTabContentsWithPendingInfoBar* tab_contents_with_pending_infobar_;
  scoped_refptr<GeolocationPermissionContext> geolocation_permission_context_;
  ScopedVector<TestTabContentsWithPendingInfoBar> extra_tabs_;

 private:
  // TabContentsWrapperTestHarness:
  virtual void SetUp();
  virtual void TearDown();

  BrowserThread ui_thread_;
  scoped_refptr<GeolocationArbitratorDependencyFactory> dependency_factory_;
};

GeolocationPermissionContextTests::GeolocationPermissionContextTests()
    : tab_contents_with_pending_infobar_(NULL),
      ui_thread_(BrowserThread::UI, MessageLoop::current()),
      dependency_factory_(
          new GeolocationArbitratorDependencyFactoryWithLocationProvider(
              &NewAutoSuccessMockNetworkLocationProvider)) {
}

GeolocationPermissionContextTests::~GeolocationPermissionContextTests() {
}

void GeolocationPermissionContextTests::CheckPermissionMessageSent(
    int bridge_id,
    bool allowed) {
  CheckPermissionMessageSentInternal(process(), bridge_id, allowed);
}

void GeolocationPermissionContextTests::CheckPermissionMessageSentForTab(
    int tab,
    int bridge_id,
    bool allowed) {
  CheckPermissionMessageSentInternal(static_cast<MockRenderProcessHost*>(
      extra_tabs_[tab]->render_view_host()->process()), bridge_id, allowed);
}

void GeolocationPermissionContextTests::CheckPermissionMessageSentInternal(
    MockRenderProcessHost* process,
    int bridge_id,
    bool allowed) {
  MessageLoop::current()->PostTask(FROM_HERE, new MessageLoop::QuitTask());
  MessageLoop::current()->Run();
  const IPC::Message* message = process->sink().GetFirstMessageMatching(
      GeolocationMsg_PermissionSet::ID);
  ASSERT_TRUE(message);
  GeolocationMsg_PermissionSet::Param param;
  GeolocationMsg_PermissionSet::Read(message, &param);
  EXPECT_EQ(bridge_id, param.a);
  EXPECT_EQ(allowed, param.b);
  process->sink().ClearMessages();
}

void GeolocationPermissionContextTests::AddNewTab(const GURL& url) {
  TestTabContentsWithPendingInfoBar* new_tab =
      new TestTabContentsWithPendingInfoBar(profile(), NULL);
  new_tab->controller().LoadURL(url, GURL(), PageTransition::TYPED);
  static_cast<TestRenderViewHost*>(new_tab->tab_contents()->render_manager()->
      current_host())->SendNavigate(extra_tabs_.size() + 1, url);
  extra_tabs_.push_back(new_tab);
}

void GeolocationPermissionContextTests::CheckTabContentsState(
    const GURL& requesting_frame,
    ContentSetting expected_content_setting) {
  TabSpecificContentSettings* content_settings =
      contents_wrapper()->content_settings();
  const GeolocationSettingsState::StateMap& state_map =
      content_settings->geolocation_settings_state().state_map();
  EXPECT_EQ(1U, state_map.count(requesting_frame.GetOrigin()));
  EXPECT_EQ(0U, state_map.count(requesting_frame));
  GeolocationSettingsState::StateMap::const_iterator settings =
      state_map.find(requesting_frame.GetOrigin());
  ASSERT_FALSE(settings == state_map.end())
      << "geolocation state not found " << requesting_frame;
  EXPECT_EQ(expected_content_setting, settings->second);
}

void GeolocationPermissionContextTests::SetUp() {
  TabContentsWrapperTestHarness::SetUp();
  GeolocationArbitrator::SetDependencyFactoryForTest(
      dependency_factory_.get());
  SiteInstance* site_instance = contents()->GetSiteInstance();
  tab_contents_with_pending_infobar_ =
      new TestTabContentsWithPendingInfoBar(profile_.get(), site_instance);
  SetContentsWrapper(tab_contents_with_pending_infobar_);
  geolocation_permission_context_ =
      new GeolocationPermissionContext(profile());
}

void GeolocationPermissionContextTests::TearDown() {
  GeolocationArbitrator::SetDependencyFactoryForTest(NULL);
  TabContentsWrapperTestHarness::TearDown();
}


// Tests ----------------------------------------------------------------------

TEST_F(GeolocationPermissionContextTests, SinglePermission) {
  GURL requesting_frame("http://www.example.com/geolocation");
  NavigateAndCommit(requesting_frame);
  EXPECT_EQ(0U, contents_wrapper()->infobar_count());
  geolocation_permission_context_->RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), requesting_frame);
  EXPECT_EQ(1U, contents_wrapper()->infobar_count());
}

TEST_F(GeolocationPermissionContextTests, QueuedPermission) {
  GURL requesting_frame_0("http://www.example.com/geolocation");
  GURL requesting_frame_1("http://www.example-2.com/geolocation");
  EXPECT_EQ(CONTENT_SETTING_ASK,
      profile()->GetGeolocationContentSettingsMap()->GetContentSetting(
          requesting_frame_0, requesting_frame_0));
  EXPECT_EQ(CONTENT_SETTING_ASK,
      profile()->GetGeolocationContentSettingsMap()->GetContentSetting(
          requesting_frame_1, requesting_frame_0));

  NavigateAndCommit(requesting_frame_0);
  EXPECT_EQ(0U, contents_wrapper()->infobar_count());
  // Request permission for two frames.
  geolocation_permission_context_->RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), requesting_frame_0);
  geolocation_permission_context_->RequestGeolocationPermission(
      process_id(), render_id(), bridge_id() + 1, requesting_frame_1);
  // Ensure only one infobar is created.
  EXPECT_EQ(1U, contents_wrapper()->infobar_count());
  ConfirmInfoBarDelegate* infobar_0 =
      contents_wrapper()->GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_0);
  string16 text_0 = infobar_0->GetMessageText();

  // Accept the first frame.
  infobar_0->Accept();
  CheckTabContentsState(requesting_frame_0, CONTENT_SETTING_ALLOW);
  CheckPermissionMessageSent(bridge_id(), true);

  contents_wrapper()->RemoveInfoBar(infobar_0);
  EXPECT_EQ(infobar_0,
            tab_contents_with_pending_infobar_->removed_infobar_delegate_);
  infobar_0->InfoBarClosed();
  // Now we should have a new infobar for the second frame.
  EXPECT_EQ(1U, contents_wrapper()->infobar_count());

  ConfirmInfoBarDelegate* infobar_1 =
      contents_wrapper()->GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_1);
  string16 text_1 = infobar_1->GetMessageText();
  EXPECT_NE(text_0, text_1);

  // Cancel (block) this frame.
  infobar_1->Cancel();
  CheckTabContentsState(requesting_frame_1, CONTENT_SETTING_BLOCK);
  CheckPermissionMessageSent(bridge_id() + 1, false);
  contents_wrapper()->RemoveInfoBar(infobar_1);
  EXPECT_EQ(infobar_1,
            tab_contents_with_pending_infobar_->removed_infobar_delegate_);
  infobar_1->InfoBarClosed();
  EXPECT_EQ(0U, contents_wrapper()->infobar_count());
  // Ensure the persisted permissions are ok.
  EXPECT_EQ(CONTENT_SETTING_ALLOW,
      profile()->GetGeolocationContentSettingsMap()->GetContentSetting(
          requesting_frame_0, requesting_frame_0));
  EXPECT_EQ(CONTENT_SETTING_BLOCK,
      profile()->GetGeolocationContentSettingsMap()->GetContentSetting(
          requesting_frame_1, requesting_frame_0));
}

TEST_F(GeolocationPermissionContextTests, CancelGeolocationPermissionRequest) {
  GURL requesting_frame_0("http://www.example.com/geolocation");
  GURL requesting_frame_1("http://www.example-2.com/geolocation");
  EXPECT_EQ(CONTENT_SETTING_ASK,
      profile()->GetGeolocationContentSettingsMap()->GetContentSetting(
          requesting_frame_0, requesting_frame_0));
  EXPECT_EQ(CONTENT_SETTING_ASK,
      profile()->GetGeolocationContentSettingsMap()->GetContentSetting(
          requesting_frame_1, requesting_frame_0));

  NavigateAndCommit(requesting_frame_0);
  EXPECT_EQ(0U, contents_wrapper()->infobar_count());
  // Request permission for two frames.
  geolocation_permission_context_->RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), requesting_frame_0);
  geolocation_permission_context_->RequestGeolocationPermission(
      process_id(), render_id(), bridge_id() + 1, requesting_frame_1);
  EXPECT_EQ(1U, contents_wrapper()->infobar_count());

  ConfirmInfoBarDelegate* infobar_0 =
      contents_wrapper()->GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_0);
  string16 text_0 = infobar_0->GetMessageText();

  // Simulate the frame going away, ensure the infobar for this frame
  // is removed and the next pending infobar is created.
  geolocation_permission_context_->CancelGeolocationPermissionRequest(
      process_id(), render_id(), bridge_id(), requesting_frame_0);
  EXPECT_EQ(infobar_0,
            tab_contents_with_pending_infobar_->removed_infobar_delegate_);
  infobar_0->InfoBarClosed();
  EXPECT_EQ(1U, contents_wrapper()->infobar_count());

  ConfirmInfoBarDelegate* infobar_1 =
      contents_wrapper()->GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_1);
  string16 text_1 = infobar_1->GetMessageText();
  EXPECT_NE(text_0, text_1);

  // Allow this frame.
  infobar_1->Accept();
  CheckTabContentsState(requesting_frame_1, CONTENT_SETTING_ALLOW);
  CheckPermissionMessageSent(bridge_id() + 1, true);
  contents_wrapper()->RemoveInfoBar(infobar_1);
  EXPECT_EQ(infobar_1,
            tab_contents_with_pending_infobar_->removed_infobar_delegate_);
  infobar_1->InfoBarClosed();
  EXPECT_EQ(0U, contents_wrapper()->infobar_count());
  // Ensure the persisted permissions are ok.
  EXPECT_EQ(CONTENT_SETTING_ASK,
      profile()->GetGeolocationContentSettingsMap()->GetContentSetting(
          requesting_frame_0, requesting_frame_0));
  EXPECT_EQ(CONTENT_SETTING_ALLOW,
      profile()->GetGeolocationContentSettingsMap()->GetContentSetting(
          requesting_frame_1, requesting_frame_0));
}

TEST_F(GeolocationPermissionContextTests, InvalidURL) {
  GURL invalid_embedder;
  GURL requesting_frame("about:blank");
  NavigateAndCommit(invalid_embedder);
  EXPECT_EQ(0U, contents_wrapper()->infobar_count());
  geolocation_permission_context_->RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), requesting_frame);
  EXPECT_EQ(0U, contents_wrapper()->infobar_count());
  CheckPermissionMessageSent(bridge_id(), false);
}

TEST_F(GeolocationPermissionContextTests, SameOriginMultipleTabs) {
  GURL url_a("http://www.example.com/geolocation");
  GURL url_b("http://www.example-2.com/geolocation");
  NavigateAndCommit(url_a);
  AddNewTab(url_b);
  AddNewTab(url_a);

  EXPECT_EQ(0U, contents_wrapper()->infobar_count());
  geolocation_permission_context_->RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), url_a);
  EXPECT_EQ(1U, contents_wrapper()->infobar_count());

  geolocation_permission_context_->RequestGeolocationPermission(
      process_id_for_tab(0), render_id_for_tab(0), bridge_id(), url_b);
  EXPECT_EQ(1U, extra_tabs_[0]->infobar_count());

  geolocation_permission_context_->RequestGeolocationPermission(
      process_id_for_tab(1), render_id_for_tab(1), bridge_id(), url_a);
  EXPECT_EQ(1U, extra_tabs_[1]->infobar_count());

  ConfirmInfoBarDelegate* removed_infobar =
      extra_tabs_[1]->GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();

  // Accept the first tab.
  ConfirmInfoBarDelegate* infobar_0 =
      contents_wrapper()->GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_0);
  infobar_0->Accept();
  CheckPermissionMessageSent(bridge_id(), true);
  contents_wrapper()->RemoveInfoBar(infobar_0);
  EXPECT_EQ(infobar_0,
            tab_contents_with_pending_infobar_->removed_infobar_delegate_);
  infobar_0->InfoBarClosed();
  // Now the infobar for the tab with the same origin should have gone.
  EXPECT_EQ(0U, extra_tabs_[1]->infobar_count());
  CheckPermissionMessageSentForTab(1, bridge_id(), true);
  // Destroy the infobar that has just been removed.
  removed_infobar->InfoBarClosed();

  // But the other tab should still have the info bar...
  EXPECT_EQ(1U, extra_tabs_[0]->infobar_count());
  extra_tabs_.reset();
}

TEST_F(GeolocationPermissionContextTests, QueuedOriginMultipleTabs) {
  GURL url_a("http://www.example.com/geolocation");
  GURL url_b("http://www.example-2.com/geolocation");
  NavigateAndCommit(url_a);
  AddNewTab(url_a);

  EXPECT_EQ(0U, contents_wrapper()->infobar_count());
  geolocation_permission_context_->RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), url_a);
  EXPECT_EQ(1U, contents_wrapper()->infobar_count());

  geolocation_permission_context_->RequestGeolocationPermission(
      process_id_for_tab(0), render_id_for_tab(0), bridge_id(), url_a);
  EXPECT_EQ(1U, extra_tabs_[0]->infobar_count());

  geolocation_permission_context_->RequestGeolocationPermission(
      process_id_for_tab(0), render_id_for_tab(0), bridge_id() + 1, url_b);
  EXPECT_EQ(1U, extra_tabs_[0]->infobar_count());

  ConfirmInfoBarDelegate* removed_infobar =
      contents_wrapper()->GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();

  // Accept the second tab.
  ConfirmInfoBarDelegate* infobar_0 =
      extra_tabs_[0]->GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_0);
  infobar_0->Accept();
  CheckPermissionMessageSentForTab(0, bridge_id(), true);
  extra_tabs_[0]->RemoveInfoBar(infobar_0);
  EXPECT_EQ(infobar_0,
            extra_tabs_[0]->removed_infobar_delegate_);
  infobar_0->InfoBarClosed();
  // Now the infobar for the tab with the same origin should have gone.
  EXPECT_EQ(0U, contents_wrapper()->infobar_count());
  CheckPermissionMessageSent(bridge_id(), true);
  // Destroy the infobar that has just been removed.
  removed_infobar->InfoBarClosed();

  // And we should have the queued infobar displayed now.
  EXPECT_EQ(1U, extra_tabs_[0]->infobar_count());

  // Accept the second infobar.
  ConfirmInfoBarDelegate* infobar_1 =
      extra_tabs_[0]->GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_1);
  infobar_1->Accept();
  CheckPermissionMessageSentForTab(0, bridge_id() + 1, true);
  extra_tabs_[0]->RemoveInfoBar(infobar_1);
  EXPECT_EQ(infobar_1,
            extra_tabs_[0]->removed_infobar_delegate_);
  infobar_1->InfoBarClosed();

  extra_tabs_.reset();
}

TEST_F(GeolocationPermissionContextTests, TabDestroyed) {
  GURL requesting_frame_0("http://www.example.com/geolocation");
  GURL requesting_frame_1("http://www.example-2.com/geolocation");
  EXPECT_EQ(
      CONTENT_SETTING_ASK,
      profile()->GetGeolocationContentSettingsMap()->GetContentSetting(
          requesting_frame_0, requesting_frame_0));
  EXPECT_EQ(
      CONTENT_SETTING_ASK,
      profile()->GetGeolocationContentSettingsMap()->GetContentSetting(
          requesting_frame_1, requesting_frame_0));

  NavigateAndCommit(requesting_frame_0);
  EXPECT_EQ(0U, contents_wrapper()->infobar_count());
  // Request permission for two frames.
  geolocation_permission_context_->RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), requesting_frame_0);
  geolocation_permission_context_->RequestGeolocationPermission(
      process_id(), render_id(), bridge_id() + 1, requesting_frame_1);
  // Ensure only one infobar is created.
  EXPECT_EQ(1U, contents_wrapper()->infobar_count());
  ConfirmInfoBarDelegate* infobar_0 =
      contents_wrapper()->GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_0);
  string16 text_0 = infobar_0->GetMessageText();

  // Delete the tab contents.
  DeleteContents();
}
