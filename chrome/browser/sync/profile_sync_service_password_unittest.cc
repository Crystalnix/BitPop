// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

#include "base/synchronization/waitable_event.h"
#include "base/task.h"
#include "base/test/test_timeouts.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/password_manager/password_store.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/sync/abstract_profile_sync_service_test.h"
#include "chrome/browser/sync/engine/syncapi.h"
#include "chrome/browser/sync/glue/password_change_processor.h"
#include "chrome/browser/sync/glue/password_data_type_controller.h"
#include "chrome/browser/sync/glue/password_model_associator.h"
#include "chrome/browser/sync/profile_sync_factory.h"
#include "chrome/browser/sync/profile_sync_factory_mock.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_test_util.h"
#include "chrome/browser/sync/protocol/password_specifics.pb.h"
#include "chrome/browser/sync/syncable/directory_manager.h"
#include "chrome/browser/sync/syncable/syncable.h"
#include "chrome/browser/sync/test_profile_sync_service.h"
#include "chrome/common/net/gaia/gaia_constants.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/sync/engine/test_id_factory.h"
#include "chrome/test/profile_mock.h"
#include "content/browser/browser_thread.h"
#include "content/common/notification_observer_mock.h"
#include "content/common/notification_source.h"
#include "content/common/notification_type.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "webkit/glue/password_form.h"

using base::Time;
using browser_sync::PasswordChangeProcessor;
using browser_sync::PasswordDataTypeController;
using browser_sync::PasswordModelAssociator;
using browser_sync::TestIdFactory;
using browser_sync::UnrecoverableErrorHandler;
using sync_api::SyncManager;
using sync_api::UserShare;
using syncable::BASE_VERSION;
using syncable::CREATE;
using syncable::DirectoryManager;
using syncable::IS_DEL;
using syncable::IS_DIR;
using syncable::IS_UNAPPLIED_UPDATE;
using syncable::IS_UNSYNCED;
using syncable::MutableEntry;
using syncable::SERVER_IS_DIR;
using syncable::SERVER_VERSION;
using syncable::SPECIFICS;
using syncable::ScopedDirLookup;
using syncable::UNIQUE_SERVER_TAG;
using syncable::UNITTEST;
using syncable::WriteTransaction;
using testing::_;
using testing::AtLeast;
using testing::DoAll;
using testing::DoDefault;
using testing::ElementsAre;
using testing::Eq;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::SaveArg;
using testing::SetArgumentPointee;
using webkit_glue::PasswordForm;

ACTION_P3(MakePasswordSyncComponents, service, ps, dtc) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  PasswordModelAssociator* model_associator =
      new PasswordModelAssociator(service, ps);
  PasswordChangeProcessor* change_processor =
      new PasswordChangeProcessor(model_associator, ps, dtc);
  return ProfileSyncFactory::SyncComponents(model_associator,
                                            change_processor);
}

ACTION_P(AcquireSyncTransaction, password_test_service) {
  // Check to make sure we can aquire a transaction (will crash if a transaction
  // is already held by this thread, deadlock if held by another thread).
  sync_api::WriteTransaction trans(password_test_service->GetUserShare());
  VLOG(1) << "Sync transaction acquired.";
}

static void QuitMessageLoop() {
  MessageLoop::current()->Quit();
}

class MockPasswordStore : public PasswordStore {
 public:
  MOCK_METHOD1(RemoveLogin, void(const PasswordForm&));
  MOCK_METHOD2(GetLogins, int(const PasswordForm&, PasswordStoreConsumer*));
  MOCK_METHOD1(AddLogin, void(const PasswordForm&));
  MOCK_METHOD1(UpdateLogin, void(const PasswordForm&));
  MOCK_METHOD0(ReportMetrics, void());
  MOCK_METHOD0(ReportMetricsImpl, void());
  MOCK_METHOD1(AddLoginImpl, void(const PasswordForm&));
  MOCK_METHOD1(UpdateLoginImpl, void(const PasswordForm&));
  MOCK_METHOD1(RemoveLoginImpl, void(const PasswordForm&));
  MOCK_METHOD2(RemoveLoginsCreatedBetweenImpl, void(const base::Time&,
               const base::Time&));
  MOCK_METHOD2(GetLoginsImpl, void(GetLoginsRequest*, const PasswordForm&));
  MOCK_METHOD1(GetAutofillableLoginsImpl, void(GetLoginsRequest*));
  MOCK_METHOD1(GetBlacklistLoginsImpl, void(GetLoginsRequest*));
  MOCK_METHOD1(FillAutofillableLogins,
      bool(std::vector<PasswordForm*>*));
  MOCK_METHOD1(FillBlacklistLogins,
      bool(std::vector<PasswordForm*>*));
};

class PasswordTestProfileSyncService : public TestProfileSyncService {
 public:
  PasswordTestProfileSyncService(ProfileSyncFactory* factory,
                                 Profile* profile,
                                 const std::string& test_user,
                                 bool synchronous_backend_initialization,
                                 Task* initial_condition_setup_task,
                                 Task* passphrase_accept_task)
      : TestProfileSyncService(factory, profile, test_user,
                               synchronous_backend_initialization,
                               initial_condition_setup_task),
        passphrase_accept_task_(passphrase_accept_task) {}

  virtual ~PasswordTestProfileSyncService() {}

  virtual void OnPassphraseAccepted() {
    if (passphrase_accept_task_) {
      passphrase_accept_task_->Run();
    }

    TestProfileSyncService::OnPassphraseAccepted();
  }

 private:
  Task* passphrase_accept_task_;
};

class ProfileSyncServicePasswordTest : public AbstractProfileSyncServiceTest {
 public:
  sync_api::UserShare* GetUserShare() {
    return service_->GetUserShare();
  }
 protected:
  ProfileSyncServicePasswordTest()
      : db_thread_(BrowserThread::DB) {
  }

  virtual void SetUp() {
    profile_.CreateRequestContext();
    password_store_ = new MockPasswordStore();
    db_thread_.Start();

    notification_service_ = new ThreadNotificationService(&db_thread_);
    notification_service_->Init();
    registrar_.Add(&observer_,
        NotificationType::SYNC_CONFIGURE_DONE,
        NotificationService::AllSources());
    registrar_.Add(&observer_,
        NotificationType::SYNC_CONFIGURE_BLOCKED,
        NotificationService::AllSources());
  }

  virtual void TearDown() {
    service_.reset();
    notification_service_->TearDown();
    db_thread_.Stop();
    {
      // The request context gets deleted on the I/O thread. To prevent a leak
      // supply one here.
      BrowserThread io_thread(BrowserThread::IO, MessageLoop::current());
      profile_.ResetRequestContext();
    }
    MessageLoop::current()->RunAllPending();
  }

  static void SignalEvent(base::WaitableEvent* done) {
    done->Signal();
  }

  void FlushLastDBTask() {
    base::WaitableEvent done(false, false);
    BrowserThread::PostTask(BrowserThread::DB, FROM_HERE,
       NewRunnableFunction(&ProfileSyncServicePasswordTest::SignalEvent,
                           &done));
    done.TimedWait(base::TimeDelta::FromMilliseconds(
        TestTimeouts::action_timeout_ms()));
  }

  void StartSyncService(Task* root_task, Task* node_task) {
    if (!service_.get()) {
      service_.reset(new PasswordTestProfileSyncService(
          &factory_, &profile_, "test_user", false, root_task, node_task));
      service_->RegisterPreferences();
      profile_.GetPrefs()->SetBoolean(prefs::kSyncPasswords, true);
      PasswordDataTypeController* data_type_controller =
          new PasswordDataTypeController(&factory_,
                                         &profile_,
                                         service_.get());

      EXPECT_CALL(factory_, CreatePasswordSyncComponents(_, _, _)).
          Times(AtLeast(1)).  // Can be more if we hit NEEDS_CRYPTO.
          WillRepeatedly(MakePasswordSyncComponents(service_.get(),
                                                    password_store_.get(),
                                                    data_type_controller));
      EXPECT_CALL(factory_, CreateDataTypeManager(_, _)).
          WillOnce(ReturnNewDataTypeManager());

      // We need tokens to get the tests going
      token_service_.IssueAuthTokenForTest(
          GaiaConstants::kSyncService, "token");

      EXPECT_CALL(profile_, GetTokenService()).
          WillRepeatedly(Return(&token_service_));

      EXPECT_CALL(profile_, GetPasswordStore(_)).
          Times(AtLeast(2)).  // Can be more if we hit NEEDS_CRYPTO.
          WillRepeatedly(Return(password_store_.get()));

      EXPECT_CALL(observer_,
          Observe(
              NotificationType(NotificationType::SYNC_CONFIGURE_DONE),_,_));
      EXPECT_CALL(observer_,
          Observe(
              NotificationType(
              NotificationType::SYNC_CONFIGURE_BLOCKED),_,_))
          .WillOnce(InvokeWithoutArgs(QuitMessageLoop));

      service_->RegisterDataTypeController(data_type_controller);
      service_->Initialize();
      MessageLoop::current()->Run();
      FlushLastDBTask();

      service_->SetPassphrase("foo", false, true);
      MessageLoop::current()->Run();
    }
  }

  void AddPasswordSyncNode(const PasswordForm& entry) {
    sync_api::WriteTransaction trans(service_->GetUserShare());
    sync_api::ReadNode password_root(&trans);
    ASSERT_TRUE(password_root.InitByTagLookup(browser_sync::kPasswordTag));

    sync_api::WriteNode node(&trans);
    std::string tag = PasswordModelAssociator::MakeTag(entry);
    ASSERT_TRUE(node.InitUniqueByCreation(syncable::PASSWORDS,
                                          password_root,
                                          tag));
    PasswordModelAssociator::WriteToSyncNode(entry, &node);
  }

  void GetPasswordEntriesFromSyncDB(std::vector<PasswordForm>* entries) {
    sync_api::ReadTransaction trans(service_->GetUserShare());
    sync_api::ReadNode password_root(&trans);
    ASSERT_TRUE(password_root.InitByTagLookup(browser_sync::kPasswordTag));

    int64 child_id = password_root.GetFirstChildId();
    while (child_id != sync_api::kInvalidId) {
      sync_api::ReadNode child_node(&trans);
      ASSERT_TRUE(child_node.InitByIdLookup(child_id));

      const sync_pb::PasswordSpecificsData& password =
          child_node.GetPasswordSpecifics();

      PasswordForm form;
      PasswordModelAssociator::CopyPassword(password, &form);

      entries->push_back(form);

      child_id = child_node.GetSuccessorId();
    }
  }

  bool ComparePasswords(const PasswordForm& lhs, const PasswordForm& rhs) {
    return lhs.scheme == rhs.scheme &&
           lhs.signon_realm == rhs.signon_realm &&
           lhs.origin == rhs.origin &&
           lhs.action == rhs.action &&
           lhs.username_element == rhs.username_element &&
           lhs.username_value == rhs.username_value &&
           lhs.password_element == rhs.password_element &&
           lhs.password_value == rhs.password_value &&
           lhs.ssl_valid == rhs.ssl_valid &&
           lhs.preferred == rhs.preferred &&
           lhs.date_created == rhs.date_created &&
           lhs.blacklisted_by_user == rhs.blacklisted_by_user;
  }

  void SetIdleChangeProcessorExpectations() {
    EXPECT_CALL(*password_store_, AddLoginImpl(_)).Times(0);
    EXPECT_CALL(*password_store_, UpdateLoginImpl(_)).Times(0);
    EXPECT_CALL(*password_store_, RemoveLoginImpl(_)).Times(0);
  }

  friend class AddPasswordEntriesTask;

  BrowserThread db_thread_;
  scoped_refptr<ThreadNotificationService> notification_service_;
  NotificationObserverMock observer_;
  ProfileMock profile_;
  scoped_refptr<MockPasswordStore> password_store_;
  NotificationRegistrar registrar_;
};

class AddPasswordEntriesTask : public Task {
 public:
  AddPasswordEntriesTask(ProfileSyncServicePasswordTest* test,
                         const std::vector<PasswordForm>& entries)
      : test_(test), entries_(entries) {
  }

  virtual void Run() {
    for (size_t i = 0; i < entries_.size(); ++i) {
      test_->AddPasswordSyncNode(entries_[i]);
    }
  }

 private:
  ProfileSyncServicePasswordTest* test_;
  const std::vector<PasswordForm>& entries_;
};

TEST_F(ProfileSyncServicePasswordTest, FailModelAssociation) {
  StartSyncService(NULL, NULL);
  EXPECT_TRUE(service_->unrecoverable_error_detected());
}

TEST_F(ProfileSyncServicePasswordTest, EmptyNativeEmptySync) {
  EXPECT_CALL(*password_store_, FillAutofillableLogins(_))
      .WillOnce(Return(true));
  EXPECT_CALL(*password_store_, FillBlacklistLogins(_))
      .WillOnce(Return(true));
  SetIdleChangeProcessorExpectations();
  CreateRootTask task(this, syncable::PASSWORDS);
  StartSyncService(&task, NULL);
  std::vector<PasswordForm> sync_entries;
  GetPasswordEntriesFromSyncDB(&sync_entries);
  EXPECT_EQ(0U, sync_entries.size());
}

TEST_F(ProfileSyncServicePasswordTest, HasNativeEntriesEmptySync) {
  std::vector<PasswordForm*> forms;
  std::vector<PasswordForm> expected_forms;
  PasswordForm* new_form = new PasswordForm;
  new_form->scheme = PasswordForm::SCHEME_HTML;
  new_form->signon_realm = "pie";
  new_form->origin = GURL("http://pie.com");
  new_form->action = GURL("http://pie.com/submit");
  new_form->username_element = UTF8ToUTF16("name");
  new_form->username_value = UTF8ToUTF16("tom");
  new_form->password_element = UTF8ToUTF16("cork");
  new_form->password_value = UTF8ToUTF16("password1");
  new_form->ssl_valid = true;
  new_form->preferred = false;
  new_form->date_created = base::Time::FromInternalValue(1234);
  new_form->blacklisted_by_user = false;
  forms.push_back(new_form);
  expected_forms.push_back(*new_form);
  EXPECT_CALL(*password_store_, FillAutofillableLogins(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(forms), Return(true)));
  EXPECT_CALL(*password_store_, FillBlacklistLogins(_))
      .WillOnce(Return(true));
  SetIdleChangeProcessorExpectations();
  CreateRootTask task(this, syncable::PASSWORDS);
  StartSyncService(&task, NULL);
  std::vector<PasswordForm> sync_forms;
  GetPasswordEntriesFromSyncDB(&sync_forms);
  ASSERT_EQ(1U, sync_forms.size());
  EXPECT_TRUE(ComparePasswords(expected_forms[0], sync_forms[0]));
}

TEST_F(ProfileSyncServicePasswordTest, HasNativeEntriesEmptySyncSameUsername) {
  std::vector<PasswordForm*> forms;
  std::vector<PasswordForm> expected_forms;

  {
    PasswordForm* new_form = new PasswordForm;
    new_form->scheme = PasswordForm::SCHEME_HTML;
    new_form->signon_realm = "pie";
    new_form->origin = GURL("http://pie.com");
    new_form->action = GURL("http://pie.com/submit");
    new_form->username_element = UTF8ToUTF16("name");
    new_form->username_value = UTF8ToUTF16("tom");
    new_form->password_element = UTF8ToUTF16("cork");
    new_form->password_value = UTF8ToUTF16("password1");
    new_form->ssl_valid = true;
    new_form->preferred = false;
    new_form->date_created = base::Time::FromInternalValue(1234);
    new_form->blacklisted_by_user = false;
    forms.push_back(new_form);
    expected_forms.push_back(*new_form);
  }
  {
    PasswordForm* new_form = new PasswordForm;
    new_form->scheme = PasswordForm::SCHEME_HTML;
    new_form->signon_realm = "pie";
    new_form->origin = GURL("http://pie.com");
    new_form->action = GURL("http://pie.com/submit");
    new_form->username_element = UTF8ToUTF16("name");
    new_form->username_value = UTF8ToUTF16("pete");
    new_form->password_element = UTF8ToUTF16("cork");
    new_form->password_value = UTF8ToUTF16("password2");
    new_form->ssl_valid = true;
    new_form->preferred = false;
    new_form->date_created = base::Time::FromInternalValue(1234);
    new_form->blacklisted_by_user = false;
    forms.push_back(new_form);
    expected_forms.push_back(*new_form);
  }

  EXPECT_CALL(*password_store_, FillAutofillableLogins(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(forms), Return(true)));
  EXPECT_CALL(*password_store_, FillBlacklistLogins(_))
      .WillOnce(Return(true));
  SetIdleChangeProcessorExpectations();
  CreateRootTask task(this, syncable::PASSWORDS);
  StartSyncService(&task, NULL);
  std::vector<PasswordForm> sync_forms;
  GetPasswordEntriesFromSyncDB(&sync_forms);
  ASSERT_EQ(2U, sync_forms.size());
  EXPECT_TRUE(ComparePasswords(expected_forms[0], sync_forms[1]));
  EXPECT_TRUE(ComparePasswords(expected_forms[1], sync_forms[0]));
}

TEST_F(ProfileSyncServicePasswordTest, HasNativeHasSyncNoMerge) {
  std::vector<PasswordForm*> native_forms;
  std::vector<PasswordForm> sync_forms;
  std::vector<PasswordForm> expected_forms;
  {
    PasswordForm* new_form = new PasswordForm;
    new_form->scheme = PasswordForm::SCHEME_HTML;
    new_form->signon_realm = "pie";
    new_form->origin = GURL("http://pie.com");
    new_form->action = GURL("http://pie.com/submit");
    new_form->username_element = UTF8ToUTF16("name");
    new_form->username_value = UTF8ToUTF16("tom");
    new_form->password_element = UTF8ToUTF16("cork");
    new_form->password_value = UTF8ToUTF16("password1");
    new_form->ssl_valid = true;
    new_form->preferred = false;
    new_form->date_created = base::Time::FromInternalValue(1234);
    new_form->blacklisted_by_user = false;

    native_forms.push_back(new_form);
    expected_forms.push_back(*new_form);
  }

  {
    PasswordForm new_form;
    new_form.scheme = PasswordForm::SCHEME_HTML;
    new_form.signon_realm = "pie2";
    new_form.origin = GURL("http://pie2.com");
    new_form.action = GURL("http://pie2.com/submit");
    new_form.username_element = UTF8ToUTF16("name2");
    new_form.username_value = UTF8ToUTF16("tom2");
    new_form.password_element = UTF8ToUTF16("cork2");
    new_form.password_value = UTF8ToUTF16("password12");
    new_form.ssl_valid = false;
    new_form.preferred = true;
    new_form.date_created = base::Time::FromInternalValue(12345);
    new_form.blacklisted_by_user = false;
    sync_forms.push_back(new_form);
    expected_forms.push_back(new_form);
  }

  EXPECT_CALL(*password_store_, FillAutofillableLogins(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(native_forms), Return(true)));
  EXPECT_CALL(*password_store_, FillBlacklistLogins(_)).WillOnce(Return(true));
  EXPECT_CALL(*password_store_, AddLoginImpl(_)).Times(1);

  CreateRootTask root_task(this, syncable::PASSWORDS);
  AddPasswordEntriesTask node_task(this, sync_forms);
  StartSyncService(&root_task, &node_task);

  std::vector<PasswordForm> new_sync_forms;
  GetPasswordEntriesFromSyncDB(&new_sync_forms);

  EXPECT_EQ(2U, new_sync_forms.size());
  EXPECT_TRUE(ComparePasswords(expected_forms[0], new_sync_forms[0]));
  EXPECT_TRUE(ComparePasswords(expected_forms[1], new_sync_forms[1]));
}

// Same as HasNativeHasEmptyNoMerge, but we attempt to aquire a sync transaction
// every time the password store is accessed.
TEST_F(ProfileSyncServicePasswordTest, EnsureNoTransactions) {
  std::vector<PasswordForm*> native_forms;
  std::vector<PasswordForm> sync_forms;
  std::vector<PasswordForm> expected_forms;
  {
    PasswordForm* new_form = new PasswordForm;
    new_form->scheme = PasswordForm::SCHEME_HTML;
    new_form->signon_realm = "pie";
    new_form->origin = GURL("http://pie.com");
    new_form->action = GURL("http://pie.com/submit");
    new_form->username_element = UTF8ToUTF16("name");
    new_form->username_value = UTF8ToUTF16("tom");
    new_form->password_element = UTF8ToUTF16("cork");
    new_form->password_value = UTF8ToUTF16("password1");
    new_form->ssl_valid = true;
    new_form->preferred = false;
    new_form->date_created = base::Time::FromInternalValue(1234);
    new_form->blacklisted_by_user = false;

    native_forms.push_back(new_form);
    expected_forms.push_back(*new_form);
  }

  {
    PasswordForm new_form;
    new_form.scheme = PasswordForm::SCHEME_HTML;
    new_form.signon_realm = "pie2";
    new_form.origin = GURL("http://pie2.com");
    new_form.action = GURL("http://pie2.com/submit");
    new_form.username_element = UTF8ToUTF16("name2");
    new_form.username_value = UTF8ToUTF16("tom2");
    new_form.password_element = UTF8ToUTF16("cork2");
    new_form.password_value = UTF8ToUTF16("password12");
    new_form.ssl_valid = false;
    new_form.preferred = true;
    new_form.date_created = base::Time::FromInternalValue(12345);
    new_form.blacklisted_by_user = false;
    sync_forms.push_back(new_form);
    expected_forms.push_back(new_form);
  }

  EXPECT_CALL(*password_store_, FillAutofillableLogins(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(native_forms),
                      AcquireSyncTransaction(this),
                      Return(true)));
  EXPECT_CALL(*password_store_, FillBlacklistLogins(_))
      .WillOnce(DoAll(AcquireSyncTransaction(this),
                      Return(true)));
  EXPECT_CALL(*password_store_, AddLoginImpl(_))
      .WillOnce(AcquireSyncTransaction(this));

  CreateRootTask root_task(this, syncable::PASSWORDS);
  AddPasswordEntriesTask node_task(this, sync_forms);
  StartSyncService(&root_task, &node_task);

  std::vector<PasswordForm> new_sync_forms;
  GetPasswordEntriesFromSyncDB(&new_sync_forms);

  EXPECT_EQ(2U, new_sync_forms.size());
  EXPECT_TRUE(ComparePasswords(expected_forms[0], new_sync_forms[0]));
  EXPECT_TRUE(ComparePasswords(expected_forms[1], new_sync_forms[1]));
}

TEST_F(ProfileSyncServicePasswordTest, HasNativeHasSyncMergeEntry) {
  std::vector<PasswordForm*> native_forms;
  std::vector<PasswordForm> sync_forms;
  std::vector<PasswordForm> expected_forms;
  {
    PasswordForm* new_form = new PasswordForm;
    new_form->scheme = PasswordForm::SCHEME_HTML;
    new_form->signon_realm = "pie";
    new_form->origin = GURL("http://pie.com");
    new_form->action = GURL("http://pie.com/submit");
    new_form->username_element = UTF8ToUTF16("name");
    new_form->username_value = UTF8ToUTF16("tom");
    new_form->password_element = UTF8ToUTF16("cork");
    new_form->password_value = UTF8ToUTF16("password1");
    new_form->ssl_valid = true;
    new_form->preferred = false;
    new_form->date_created = base::Time::FromInternalValue(1234);
    new_form->blacklisted_by_user = false;

    native_forms.push_back(new_form);
  }

  {
    PasswordForm new_form;
    new_form.scheme = PasswordForm::SCHEME_HTML;
    new_form.signon_realm = "pie";
    new_form.origin = GURL("http://pie.com");
    new_form.action = GURL("http://pie.com/submit");
    new_form.username_element = UTF8ToUTF16("name");
    new_form.username_value = UTF8ToUTF16("tom");
    new_form.password_element = UTF8ToUTF16("cork");
    new_form.password_value = UTF8ToUTF16("password12");
    new_form.ssl_valid = false;
    new_form.preferred = true;
    new_form.date_created = base::Time::FromInternalValue(12345);
    new_form.blacklisted_by_user = false;
    sync_forms.push_back(new_form);
  }

  {
    PasswordForm new_form;
    new_form.scheme = PasswordForm::SCHEME_HTML;
    new_form.signon_realm = "pie";
    new_form.origin = GURL("http://pie.com");
    new_form.action = GURL("http://pie.com/submit");
    new_form.username_element = UTF8ToUTF16("name");
    new_form.username_value = UTF8ToUTF16("tom");
    new_form.password_element = UTF8ToUTF16("cork");
    new_form.password_value = UTF8ToUTF16("password12");
    new_form.ssl_valid = false;
    new_form.preferred = true;
    new_form.date_created = base::Time::FromInternalValue(12345);
    new_form.blacklisted_by_user = false;
    expected_forms.push_back(new_form);
  }

  EXPECT_CALL(*password_store_, FillAutofillableLogins(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(native_forms), Return(true)));
  EXPECT_CALL(*password_store_, FillBlacklistLogins(_)).WillOnce(Return(true));
  EXPECT_CALL(*password_store_, UpdateLoginImpl(_)).Times(1);

  CreateRootTask root_task(this, syncable::PASSWORDS);
  AddPasswordEntriesTask node_task(this, sync_forms);

  StartSyncService(&root_task, &node_task);

  std::vector<PasswordForm> new_sync_forms;
  GetPasswordEntriesFromSyncDB(&new_sync_forms);

  EXPECT_EQ(1U, new_sync_forms.size());
  EXPECT_TRUE(ComparePasswords(expected_forms[0], new_sync_forms[0]));
}
