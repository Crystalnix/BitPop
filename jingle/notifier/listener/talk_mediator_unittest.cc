// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/basictypes.h"
#include "base/message_loop.h"
#include "jingle/notifier/base/notifier_options.h"
#include "jingle/notifier/listener/mediator_thread_mock.h"
#include "jingle/notifier/listener/mediator_thread_impl.h"
#include "jingle/notifier/listener/talk_mediator_impl.h"
#include "talk/xmpp/xmppengine.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace notifier {

using ::testing::_;

class MockTalkMediatorDelegate : public TalkMediator::Delegate {
 public:
  MockTalkMediatorDelegate() {}
  virtual ~MockTalkMediatorDelegate() {}

  MOCK_METHOD1(OnNotificationStateChange,
               void(bool notification_changed));
  MOCK_METHOD1(OnIncomingNotification,
               void(const Notification& data));
  MOCK_METHOD0(OnOutgoingNotification, void());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockTalkMediatorDelegate);
};

class TalkMediatorImplTest : public testing::Test {
 protected:
  TalkMediatorImplTest() {}
  virtual ~TalkMediatorImplTest() {}

  TalkMediatorImpl* NewMockedTalkMediator(
      MockMediatorThread* mock_mediator_thread) {
    return new TalkMediatorImpl(mock_mediator_thread,
                                NotifierOptions());
  }

  int last_message_;

 private:
  // TalkMediatorImpl expects a message loop.
  MessageLoop message_loop_;

  DISALLOW_COPY_AND_ASSIGN(TalkMediatorImplTest);
};

TEST_F(TalkMediatorImplTest, SetAuthToken) {
  scoped_ptr<TalkMediatorImpl> talk1(
      NewMockedTalkMediator(new MockMediatorThread()));
  talk1->SetAuthToken("chromium@gmail.com", "token", "fake_service");
  EXPECT_TRUE(talk1->state_.initialized);
  talk1->Logout();

  scoped_ptr<TalkMediatorImpl> talk2(
      NewMockedTalkMediator(new MockMediatorThread()));
  talk2->SetAuthToken("chromium@mail.google.com", "token", "fake_service");
  EXPECT_TRUE(talk2->state_.initialized);
  talk2->Logout();

  scoped_ptr<TalkMediatorImpl> talk3(
      NewMockedTalkMediator(new MockMediatorThread()));
  talk3->SetAuthToken("chromium@mail.google.com", "token", "fake_service");
  EXPECT_TRUE(talk3->state_.initialized);
  talk3->Logout();
}

TEST_F(TalkMediatorImplTest, LoginWiring) {
  // The TalkMediatorImpl owns the mock.
  MockMediatorThread* mock = new MockMediatorThread();
  scoped_ptr<TalkMediatorImpl> talk1(NewMockedTalkMediator(mock));

  // Login checks states for initialization.
  EXPECT_FALSE(talk1->Login());
  EXPECT_EQ(0, mock->login_calls);

  talk1->SetAuthToken("chromium@gmail.com", "token", "fake_service");
  EXPECT_EQ(0, mock->update_settings_calls);

  EXPECT_TRUE(talk1->Login());
  EXPECT_EQ(1, mock->login_calls);

  // We call SetAuthToken again to update the settings after an update.
  talk1->SetAuthToken("chromium@gmail.com", "token", "fake_service");
  EXPECT_EQ(1, mock->update_settings_calls);

  // Successive calls to login will fail.  One needs to create a new talk
  // mediator object.
  EXPECT_FALSE(talk1->Login());
  EXPECT_EQ(1, mock->login_calls);

  EXPECT_TRUE(talk1->Logout());
  EXPECT_EQ(1, mock->logout_calls);

  // Successive logout calls do nothing.
  EXPECT_FALSE(talk1->Logout());
  EXPECT_EQ(1, mock->logout_calls);
}

TEST_F(TalkMediatorImplTest, SendNotification) {
  // The TalkMediatorImpl owns the mock.
  MockMediatorThread* mock = new MockMediatorThread();
  scoped_ptr<TalkMediatorImpl> talk1(NewMockedTalkMediator(mock));

  Notification data;
  talk1->SendNotification(data);
  EXPECT_EQ(1, mock->send_calls);

  talk1->SetAuthToken("chromium@gmail.com", "token", "fake_service");
  EXPECT_TRUE(talk1->Login());
  talk1->OnConnectionStateChange(true);
  EXPECT_EQ(1, mock->login_calls);

  talk1->SendNotification(data);
  EXPECT_EQ(2, mock->send_calls);
  talk1->SendNotification(data);
  EXPECT_EQ(3, mock->send_calls);

  EXPECT_TRUE(talk1->Logout());
  EXPECT_EQ(1, mock->logout_calls);

  talk1->SendNotification(data);
  EXPECT_EQ(4, mock->send_calls);
}

TEST_F(TalkMediatorImplTest, MediatorThreadCallbacks) {
  // The TalkMediatorImpl owns the mock.
  MockMediatorThread* mock = new MockMediatorThread();
  scoped_ptr<TalkMediatorImpl> talk1(NewMockedTalkMediator(mock));

  MockTalkMediatorDelegate mock_delegate;
  EXPECT_CALL(mock_delegate, OnNotificationStateChange(true));
  EXPECT_CALL(mock_delegate, OnIncomingNotification(_));
  EXPECT_CALL(mock_delegate, OnOutgoingNotification());

  talk1->SetDelegate(&mock_delegate);

  talk1->SetAuthToken("chromium@gmail.com", "token", "fake_service");
  EXPECT_TRUE(talk1->Login());
  EXPECT_EQ(1, mock->login_calls);

  // The message triggers calls to listen and subscribe.
  EXPECT_EQ(1, mock->listen_calls);
  EXPECT_EQ(1, mock->subscribe_calls);

  // After subscription success is receieved, the talk mediator will allow
  // sending of notifications.
  Notification outgoing_data;
  talk1->SendNotification(outgoing_data);
  EXPECT_EQ(1, mock->send_calls);

  Notification incoming_data;
  incoming_data.channel = "service_url";
  incoming_data.data = "service_data";
  mock->ReceiveNotification(incoming_data);

  // Shouldn't trigger a call to the delegate since we disconnect
  // it before we logout the mediator thread.
  talk1->Logout();
  talk1.reset();
}

}  // namespace notifier
