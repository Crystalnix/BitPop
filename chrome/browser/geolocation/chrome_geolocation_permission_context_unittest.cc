// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/hash_tables.h"
#include "base/memory/scoped_vector.h"
#include "base/synchronization/waitable_event.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/content_settings/tab_specific_content_settings.h"
#include "chrome/browser/geolocation/chrome_geolocation_permission_context.h"
#include "chrome/browser/infobars/infobar.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "chrome/browser/tab_contents/confirm_infobar_delegate.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/tab_contents/test_tab_contents.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/test_browser_thread.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_ANDROID)
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/common/pref_names.h"
#endif

using content::BrowserThread;
using content::MockRenderProcessHost;
using content::RenderViewHostTester;
using content::WebContents;
using content::WebContentsTester;

// ClosedDelegateTracker ------------------------------------------------------

// We need to track which infobars were closed.
class ClosedDelegateTracker : public content::NotificationObserver {
 public:
  ClosedDelegateTracker();
  virtual ~ClosedDelegateTracker();

  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details);

  size_t size() const {
    return removed_infobar_delegates_.size();
  }

  bool Contains(InfoBarDelegate* delegate) const;
  void Clear();

 private:
  FRIEND_TEST_ALL_PREFIXES(GeolocationPermissionContextTests, TabDestroyed);
  content::NotificationRegistrar registrar_;
  std::set<InfoBarDelegate*> removed_infobar_delegates_;
};

ClosedDelegateTracker::ClosedDelegateTracker() {
  registrar_.Add(this, chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_REMOVED,
                 content::NotificationService::AllSources());
}

ClosedDelegateTracker::~ClosedDelegateTracker() {
}

void ClosedDelegateTracker::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(type == chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_REMOVED);
  removed_infobar_delegates_.insert(
      content::Details<InfoBarRemovedDetails>(details)->first);
}

bool ClosedDelegateTracker::Contains(InfoBarDelegate* delegate) const {
  return removed_infobar_delegates_.count(delegate) != 0;
}

void ClosedDelegateTracker::Clear() {
  removed_infobar_delegates_.clear();
}

// GeolocationPermissionContextTests ------------------------------------------

// This class sets up GeolocationArbitrator.
class GeolocationPermissionContextTests : public TabContentsTestHarness {
 public:
  GeolocationPermissionContextTests();

 protected:
  virtual ~GeolocationPermissionContextTests();

  int process_id() {
    return contents()->GetRenderProcessHost()->GetID();
  }
  int process_id_for_tab(int tab) {
    return extra_tabs_[tab]->web_contents()->GetRenderProcessHost()->GetID();
  }
  int render_id() { return contents()->GetRenderViewHost()->GetRoutingID(); }
  int render_id_for_tab(int tab) {
    return extra_tabs_[tab]->web_contents()->
        GetRenderViewHost()->GetRoutingID();
  }
  int bridge_id() const { return 42; }  // Not relevant at this level.
  InfoBarTabHelper* infobar_tab_helper() {
    return tab_contents()->infobar_tab_helper();
  }

  void RequestGeolocationPermission(int render_process_id,
                                    int render_view_id,
                                    int bridge_id,
                                    const GURL& requesting_frame);
  void PermissionResponse(int render_process_id,
                          int render_view_id,
                          int bridge_id,
                          bool allowed);
  void CheckPermissionMessageSent(int bridge_id, bool allowed);
  void CheckPermissionMessageSentForTab(int tab, int bridge_id, bool allowed);
  void CheckPermissionMessageSentInternal(MockRenderProcessHost* process,
                                          int bridge_id,
                                          bool allowed);
  void AddNewTab(const GURL& url);
  void CheckTabContentsState(const GURL& requesting_frame,
                             ContentSetting expected_content_setting);

  scoped_refptr<ChromeGeolocationPermissionContext>
      geolocation_permission_context_;
  ClosedDelegateTracker closed_delegate_tracker_;
  ScopedVector<TabContents> extra_tabs_;

 private:
  // TabContentsTestHarness:
  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread db_thread_;

  // A map between renderer child id and a pair represending the bridge id and
  // whether the requested permission was allowed.
  base::hash_map<int, std::pair<int, bool> > responses_;
};

GeolocationPermissionContextTests::GeolocationPermissionContextTests()
    : TabContentsTestHarness(),
      ui_thread_(BrowserThread::UI, MessageLoop::current()),
      db_thread_(BrowserThread::DB) {
}

GeolocationPermissionContextTests::~GeolocationPermissionContextTests() {
}

void GeolocationPermissionContextTests::RequestGeolocationPermission(
    int render_process_id,
    int render_view_id,
    int bridge_id,
    const GURL& requesting_frame) {
  geolocation_permission_context_->RequestGeolocationPermission(
      render_process_id, render_view_id, bridge_id, requesting_frame,
      base::Bind(&GeolocationPermissionContextTests::PermissionResponse,
                 base::Unretained(this),
                 render_process_id,
                 render_view_id,
                 bridge_id));
}

void GeolocationPermissionContextTests::PermissionResponse(
    int render_process_id,
    int render_view_id,
    int bridge_id,
    bool allowed) {
  responses_[render_process_id] = std::make_pair(bridge_id, allowed);
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
      extra_tabs_[tab]->web_contents()->GetRenderProcessHost()),
      bridge_id, allowed);
}

void GeolocationPermissionContextTests::CheckPermissionMessageSentInternal(
    MockRenderProcessHost* process,
    int bridge_id,
    bool allowed) {
  ASSERT_EQ(responses_.count(process->GetID()), 1U);
  EXPECT_EQ(bridge_id, responses_[process->GetID()].first);
  EXPECT_EQ(allowed, responses_[process->GetID()].second);
  responses_.erase(process->GetID());
}

void GeolocationPermissionContextTests::AddNewTab(const GURL& url) {
  WebContents* new_tab =
      WebContents::Create(profile(), NULL, MSG_ROUTING_NONE, NULL, NULL);
  new_tab->GetController().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_TYPED, std::string());
  RenderViewHostTester::For(new_tab->GetRenderViewHost())->
      SendNavigate(extra_tabs_.size() + 1, url);
  extra_tabs_.push_back(new TabContents(new_tab));
}

void GeolocationPermissionContextTests::CheckTabContentsState(
    const GURL& requesting_frame,
    ContentSetting expected_content_setting) {
  TabSpecificContentSettings* content_settings =
      tab_contents()->content_settings();
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
  db_thread_.Start();
  TabContentsTestHarness::SetUp();
  geolocation_permission_context_ =
      new ChromeGeolocationPermissionContext(profile());
}

void GeolocationPermissionContextTests::TearDown() {
  extra_tabs_.clear();
  TabContentsTestHarness::TearDown();
  // Schedule another task on the DB thread to notify us that it's safe to
  // carry on with the test.
  base::WaitableEvent done(false, false);
  BrowserThread::PostTask(BrowserThread::DB, FROM_HERE,
      base::Bind(&base::WaitableEvent::Signal, base::Unretained(&done)));
  done.Wait();
  db_thread_.Stop();
}


// Tests ----------------------------------------------------------------------

TEST_F(GeolocationPermissionContextTests, SinglePermission) {
  GURL requesting_frame("http://www.example.com/geolocation");
  NavigateAndCommit(requesting_frame);
  EXPECT_EQ(0U, infobar_tab_helper()->infobar_count());
  RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), requesting_frame);
  ASSERT_EQ(1U, infobar_tab_helper()->infobar_count());
  ConfirmInfoBarDelegate* infobar_0 = infobar_tab_helper()->
      GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  infobar_0->Cancel();
  infobar_tab_helper()->RemoveInfoBar(infobar_0);
  EXPECT_EQ(1U, closed_delegate_tracker_.size());
  EXPECT_TRUE(closed_delegate_tracker_.Contains(infobar_0));
  infobar_0->InfoBarClosed();
}

#if defined(OS_ANDROID)
TEST_F(GeolocationPermissionContextTests, GeolocationEnabledDisabled) {
  profile()->GetHostContentSettingsMap()->SetDefaultContentSetting(
      CONTENT_SETTINGS_TYPE_GEOLOCATION,
      CONTENT_SETTING_ALLOW);

  // Check that the request is denied with preference disabled,
  // even though the default policy allows it.
  GURL requesting_frame("http://www.example.com/geolocation");
  NavigateAndCommit(requesting_frame);
  EXPECT_EQ(0U, infobar_tab_helper()->infobar_count());
  profile()->GetPrefs()->SetBoolean(prefs::kGeolocationEnabled, false);
  RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), requesting_frame);
  ASSERT_EQ(0U, infobar_tab_helper()->infobar_count());
  CheckPermissionMessageSent(bridge_id(), false);

  // Reenable the preference and check that the request now goes though.
  profile()->GetPrefs()->SetBoolean(prefs::kGeolocationEnabled, true);
  RequestGeolocationPermission(
      process_id(), render_id(), bridge_id() + 1, requesting_frame);
  ASSERT_EQ(0U, infobar_tab_helper()->infobar_count());
  CheckPermissionMessageSent(bridge_id() + 1, true);
}
#endif

TEST_F(GeolocationPermissionContextTests, QueuedPermission) {
  GURL requesting_frame_0("http://www.example.com/geolocation");
  GURL requesting_frame_1("http://www.example-2.com/geolocation");
  EXPECT_EQ(CONTENT_SETTING_ASK,
      profile()->GetHostContentSettingsMap()->GetContentSetting(
          requesting_frame_0,
          requesting_frame_0,
          CONTENT_SETTINGS_TYPE_GEOLOCATION,
          std::string()));
  EXPECT_EQ(CONTENT_SETTING_ASK,
      profile()->GetHostContentSettingsMap()->GetContentSetting(
          requesting_frame_1,
          requesting_frame_0,
          CONTENT_SETTINGS_TYPE_GEOLOCATION,
          std::string()));


  NavigateAndCommit(requesting_frame_0);
  EXPECT_EQ(0U, infobar_tab_helper()->infobar_count());
  // Request permission for two frames.
  RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), requesting_frame_0);
  RequestGeolocationPermission(
      process_id(), render_id(), bridge_id() + 1, requesting_frame_1);
  // Ensure only one infobar is created.
  ASSERT_EQ(1U, infobar_tab_helper()->infobar_count());
  ConfirmInfoBarDelegate* infobar_0 = infobar_tab_helper()->
      GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_0);
  string16 text_0 = infobar_0->GetMessageText();

  // Accept the first frame.
  infobar_0->Accept();
  CheckTabContentsState(requesting_frame_0, CONTENT_SETTING_ALLOW);
  CheckPermissionMessageSent(bridge_id(), true);

  infobar_tab_helper()->RemoveInfoBar(infobar_0);
  EXPECT_EQ(1U, closed_delegate_tracker_.size());
  EXPECT_TRUE(closed_delegate_tracker_.Contains(infobar_0));
  closed_delegate_tracker_.Clear();
  infobar_0->InfoBarClosed();
  // Now we should have a new infobar for the second frame.
  ASSERT_EQ(1U, infobar_tab_helper()->infobar_count());

  ConfirmInfoBarDelegate* infobar_1 = infobar_tab_helper()->
      GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_1);
  string16 text_1 = infobar_1->GetMessageText();
  EXPECT_NE(text_0, text_1);

  // Cancel (block) this frame.
  infobar_1->Cancel();
  CheckTabContentsState(requesting_frame_1, CONTENT_SETTING_BLOCK);
  CheckPermissionMessageSent(bridge_id() + 1, false);
  infobar_tab_helper()->RemoveInfoBar(infobar_1);
  EXPECT_EQ(1U, closed_delegate_tracker_.size());
  EXPECT_TRUE(closed_delegate_tracker_.Contains(infobar_1));
  infobar_1->InfoBarClosed();
  EXPECT_EQ(0U, infobar_tab_helper()->infobar_count());
  // Ensure the persisted permissions are ok.
  EXPECT_EQ(CONTENT_SETTING_ALLOW,
      profile()->GetHostContentSettingsMap()->GetContentSetting(
          requesting_frame_0,
          requesting_frame_0,
          CONTENT_SETTINGS_TYPE_GEOLOCATION,
          std::string()));

  EXPECT_EQ(CONTENT_SETTING_BLOCK,
      profile()->GetHostContentSettingsMap()->GetContentSetting(
          requesting_frame_1,
          requesting_frame_0,
          CONTENT_SETTINGS_TYPE_GEOLOCATION,
          std::string()));
}

TEST_F(GeolocationPermissionContextTests, CancelGeolocationPermissionRequest) {
  GURL requesting_frame_0("http://www.example.com/geolocation");
  GURL requesting_frame_1("http://www.example-2.com/geolocation");
  EXPECT_EQ(CONTENT_SETTING_ASK,
      profile()->GetHostContentSettingsMap()->GetContentSetting(
          requesting_frame_0,
          requesting_frame_0,
          CONTENT_SETTINGS_TYPE_GEOLOCATION,
          std::string()));

  EXPECT_EQ(CONTENT_SETTING_ASK,
      profile()->GetHostContentSettingsMap()->GetContentSetting(
          requesting_frame_1,
          requesting_frame_0,
          CONTENT_SETTINGS_TYPE_GEOLOCATION,
          std::string()));


  NavigateAndCommit(requesting_frame_0);
  EXPECT_EQ(0U, infobar_tab_helper()->infobar_count());
  // Request permission for two frames.
  RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), requesting_frame_0);
  RequestGeolocationPermission(
      process_id(), render_id(), bridge_id() + 1, requesting_frame_1);
  ASSERT_EQ(1U, infobar_tab_helper()->infobar_count());

  ConfirmInfoBarDelegate* infobar_0 = infobar_tab_helper()->
      GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_0);
  string16 text_0 = infobar_0->GetMessageText();

  // Simulate the frame going away, ensure the infobar for this frame
  // is removed and the next pending infobar is created.
  geolocation_permission_context_->CancelGeolocationPermissionRequest(
      process_id(), render_id(), bridge_id(), requesting_frame_0);
  EXPECT_EQ(1U, closed_delegate_tracker_.size());
  EXPECT_TRUE(closed_delegate_tracker_.Contains(infobar_0));
  closed_delegate_tracker_.Clear();
  infobar_0->InfoBarClosed();
  ASSERT_EQ(1U, infobar_tab_helper()->infobar_count());

  ConfirmInfoBarDelegate* infobar_1 = infobar_tab_helper()->
      GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_1);
  string16 text_1 = infobar_1->GetMessageText();
  EXPECT_NE(text_0, text_1);

  // Allow this frame.
  infobar_1->Accept();
  CheckTabContentsState(requesting_frame_1, CONTENT_SETTING_ALLOW);
  CheckPermissionMessageSent(bridge_id() + 1, true);
  infobar_tab_helper()->RemoveInfoBar(infobar_1);
  EXPECT_EQ(1U, closed_delegate_tracker_.size());
  EXPECT_TRUE(closed_delegate_tracker_.Contains(infobar_1));
  infobar_1->InfoBarClosed();
  EXPECT_EQ(0U, infobar_tab_helper()->infobar_count());
  // Ensure the persisted permissions are ok.
  EXPECT_EQ(CONTENT_SETTING_ASK,
      profile()->GetHostContentSettingsMap()->GetContentSetting(
          requesting_frame_0,
          requesting_frame_0,
          CONTENT_SETTINGS_TYPE_GEOLOCATION,
          std::string()));

  EXPECT_EQ(CONTENT_SETTING_ALLOW,
      profile()->GetHostContentSettingsMap()->GetContentSetting(
          requesting_frame_1,
          requesting_frame_0,
          CONTENT_SETTINGS_TYPE_GEOLOCATION,
          std::string()));
}

TEST_F(GeolocationPermissionContextTests, InvalidURL) {
  GURL invalid_embedder("about:blank");
  GURL requesting_frame;
  NavigateAndCommit(invalid_embedder);
  EXPECT_EQ(0U, infobar_tab_helper()->infobar_count());
  RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), requesting_frame);
  EXPECT_EQ(0U, infobar_tab_helper()->infobar_count());
  CheckPermissionMessageSent(bridge_id(), false);
}

TEST_F(GeolocationPermissionContextTests, SameOriginMultipleTabs) {
  GURL url_a("http://www.example.com/geolocation");
  GURL url_b("http://www.example-2.com/geolocation");
  NavigateAndCommit(url_a);
  AddNewTab(url_b);
  AddNewTab(url_a);

  EXPECT_EQ(0U, infobar_tab_helper()->infobar_count());
  RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), url_a);
  ASSERT_EQ(1U, infobar_tab_helper()->infobar_count());

  RequestGeolocationPermission(
      process_id_for_tab(0), render_id_for_tab(0), bridge_id(), url_b);
  EXPECT_EQ(1U, extra_tabs_[0]->infobar_tab_helper()->infobar_count());

  RequestGeolocationPermission(
      process_id_for_tab(1), render_id_for_tab(1), bridge_id(), url_a);
  ASSERT_EQ(1U, extra_tabs_[1]->infobar_tab_helper()->infobar_count());

  ConfirmInfoBarDelegate* removed_infobar = extra_tabs_[1]->
      infobar_tab_helper()->GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();

  // Accept the first tab.
  ConfirmInfoBarDelegate* infobar_0 = infobar_tab_helper()->
      GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_0);
  infobar_0->Accept();
  CheckPermissionMessageSent(bridge_id(), true);
  infobar_tab_helper()->RemoveInfoBar(infobar_0);
  EXPECT_EQ(2U, closed_delegate_tracker_.size());
  EXPECT_TRUE(closed_delegate_tracker_.Contains(infobar_0));
  infobar_0->InfoBarClosed();
  // Now the infobar for the tab with the same origin should have gone.
  EXPECT_EQ(0U, extra_tabs_[1]->infobar_tab_helper()->infobar_count());
  CheckPermissionMessageSentForTab(1, bridge_id(), true);
  EXPECT_TRUE(closed_delegate_tracker_.Contains(removed_infobar));
  closed_delegate_tracker_.Clear();
  // Destroy the infobar that has just been removed.
  removed_infobar->InfoBarClosed();

  // But the other tab should still have the info bar...
  ASSERT_EQ(1U, extra_tabs_[0]->infobar_tab_helper()->infobar_count());
  ConfirmInfoBarDelegate* infobar_1 = extra_tabs_[0]->infobar_tab_helper()->
      GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  infobar_1->Cancel();
  extra_tabs_[0]->infobar_tab_helper()->RemoveInfoBar(infobar_1);
  EXPECT_EQ(1U, closed_delegate_tracker_.size());
  EXPECT_TRUE(closed_delegate_tracker_.Contains(infobar_1));
  infobar_1->InfoBarClosed();
}

TEST_F(GeolocationPermissionContextTests, QueuedOriginMultipleTabs) {
  GURL url_a("http://www.example.com/geolocation");
  GURL url_b("http://www.example-2.com/geolocation");
  NavigateAndCommit(url_a);
  AddNewTab(url_a);

  EXPECT_EQ(0U, infobar_tab_helper()->infobar_count());
  RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), url_a);
  ASSERT_EQ(1U, infobar_tab_helper()->infobar_count());

  RequestGeolocationPermission(
      process_id_for_tab(0), render_id_for_tab(0), bridge_id(), url_a);
  EXPECT_EQ(1U, extra_tabs_[0]->infobar_tab_helper()->infobar_count());

  RequestGeolocationPermission(
      process_id_for_tab(0), render_id_for_tab(0), bridge_id() + 1, url_b);
  ASSERT_EQ(1U, extra_tabs_[0]->infobar_tab_helper()->infobar_count());

  ConfirmInfoBarDelegate* removed_infobar =
      infobar_tab_helper()->GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();

  // Accept the second tab.
  ConfirmInfoBarDelegate* infobar_0 = extra_tabs_[0]->infobar_tab_helper()->
      GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_0);
  infobar_0->Accept();
  CheckPermissionMessageSentForTab(0, bridge_id(), true);
  extra_tabs_[0]->infobar_tab_helper()->RemoveInfoBar(infobar_0);
  EXPECT_EQ(2U, closed_delegate_tracker_.size());
  EXPECT_TRUE(closed_delegate_tracker_.Contains(infobar_0));
  infobar_0->InfoBarClosed();
  // Now the infobar for the tab with the same origin should have gone.
  EXPECT_EQ(0U, infobar_tab_helper()->infobar_count());
  CheckPermissionMessageSent(bridge_id(), true);
  EXPECT_TRUE(closed_delegate_tracker_.Contains(removed_infobar));
  closed_delegate_tracker_.Clear();
  // Destroy the infobar that has just been removed.
  removed_infobar->InfoBarClosed();

  // And we should have the queued infobar displayed now.
  ASSERT_EQ(1U, extra_tabs_[0]->infobar_tab_helper()->infobar_count());

  // Accept the second infobar.
  ConfirmInfoBarDelegate* infobar_1 = extra_tabs_[0]->infobar_tab_helper()->
      GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_1);
  infobar_1->Accept();
  CheckPermissionMessageSentForTab(0, bridge_id() + 1, true);
  extra_tabs_[0]->infobar_tab_helper()->RemoveInfoBar(infobar_1);
  EXPECT_EQ(1U, closed_delegate_tracker_.size());
  EXPECT_TRUE(closed_delegate_tracker_.Contains(infobar_1));
  infobar_1->InfoBarClosed();
}

TEST_F(GeolocationPermissionContextTests, TabDestroyed) {
  GURL requesting_frame_0("http://www.example.com/geolocation");
  GURL requesting_frame_1("http://www.example-2.com/geolocation");
  EXPECT_EQ(
      CONTENT_SETTING_ASK,
      profile()->GetHostContentSettingsMap()->GetContentSetting(
          requesting_frame_0,
          requesting_frame_0,
          CONTENT_SETTINGS_TYPE_GEOLOCATION,
          std::string()));

  EXPECT_EQ(
      CONTENT_SETTING_ASK,
      profile()->GetHostContentSettingsMap()->GetContentSetting(
          requesting_frame_1,
          requesting_frame_0,
          CONTENT_SETTINGS_TYPE_GEOLOCATION,
          std::string()));

  NavigateAndCommit(requesting_frame_0);
  EXPECT_EQ(0U, infobar_tab_helper()->infobar_count());
  // Request permission for two frames.
  RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), requesting_frame_0);
  RequestGeolocationPermission(
      process_id(), render_id(), bridge_id() + 1, requesting_frame_1);
  // Ensure only one infobar is created.
  ASSERT_EQ(1U, infobar_tab_helper()->infobar_count());
  ConfirmInfoBarDelegate* infobar_0 = infobar_tab_helper()->
      GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(infobar_0);

  // Delete the tab contents.
  DeleteContents();
  infobar_0->InfoBarClosed();

  // During contents destruction, the infobar will have been closed, and a
  // second (with it's own new delegate) will have been created. In Chromium,
  // this would be properly deleted by the InfoBarContainer, but in this unit
  // test, the closest thing we have to that is the ClosedDelegateTracker.
  ASSERT_EQ(2U, closed_delegate_tracker_.size());
  ASSERT_TRUE(closed_delegate_tracker_.Contains(infobar_0));
  closed_delegate_tracker_.removed_infobar_delegates_.erase(infobar_0);
  (*closed_delegate_tracker_.removed_infobar_delegates_.begin())->
      InfoBarClosed();
}

TEST_F(GeolocationPermissionContextTests, InfoBarUsesCommittedEntry) {
  GURL requesting_frame_0("http://www.example.com/geolocation");
  GURL requesting_frame_1("http://www.example-2.com/geolocation");
  NavigateAndCommit(requesting_frame_0);
  NavigateAndCommit(requesting_frame_1);
  EXPECT_EQ(0U, infobar_tab_helper()->infobar_count());
  // Go back: navigate to a pending entry before requesting geolocation
  // permission.
  contents()->GetController().GoBack();
  // Request permission for the committed frame (not the pending one).
  RequestGeolocationPermission(
      process_id(), render_id(), bridge_id(), requesting_frame_1);
  // Ensure the infobar is created.
  ASSERT_EQ(1U, infobar_tab_helper()->infobar_count());
  InfoBarDelegate* infobar_0 = infobar_tab_helper()->GetInfoBarDelegateAt(0);
  ASSERT_TRUE(infobar_0);
  // Ensure the infobar is not yet expired.
  content::LoadCommittedDetails details;
  details.entry = contents()->GetController().GetLastCommittedEntry();
  ASSERT_FALSE(infobar_0->ShouldExpire(details));
  // Commit the "GoBack()" above, and ensure the infobar is now expired.
  WebContentsTester::For(contents())->CommitPendingNavigation();
  details.entry = contents()->GetController().GetLastCommittedEntry();
  ASSERT_TRUE(infobar_0->ShouldExpire(details));

  // Delete the tab contents.
  DeleteContents();
  infobar_0->InfoBarClosed();
}
