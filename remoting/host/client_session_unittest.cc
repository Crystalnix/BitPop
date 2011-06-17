// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/client_session.h"
#include "remoting/host/host_mock_objects.h"
#include "remoting/protocol/protocol_mock_objects.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace remoting {

namespace {

// A task that does nothing.
class DummyTask : public Task {
 public:
  void Run() {}
};

}  // namespace

using protocol::MockConnectionToClient;
using protocol::MockConnectionToClientEventHandler;
using protocol::MockHostStub;
using protocol::MockInputStub;

using testing::_;
using testing::DeleteArg;
using testing::InSequence;
using testing::Return;

class ClientSessionTest : public testing::Test {
 public:
  ClientSessionTest() {}

  virtual void SetUp() {
    connection_ = new MockConnectionToClient(&message_loop_,
                                             &connection_event_handler_,
                                             &host_stub_,
                                             &input_stub_);
    user_authenticator_ = new MockUserAuthenticator();
    client_session_ = new ClientSession(
        &session_event_handler_,
        user_authenticator_,
        connection_,
        &input_stub_);

    ON_CALL(input_stub_, InjectKeyEvent(_, _)).WillByDefault(DeleteArg<1>());
    ON_CALL(input_stub_, InjectMouseEvent(_, _)).WillByDefault(DeleteArg<1>());
  }

 protected:
  MessageLoop message_loop_;
  MockConnectionToClientEventHandler connection_event_handler_;
  MockHostStub host_stub_;
  MockInputStub input_stub_;
  MockClientSessionEventHandler session_event_handler_;
  MockUserAuthenticator* user_authenticator_;
  scoped_refptr<MockConnectionToClient> connection_;
  scoped_refptr<ClientSession> client_session_;
};

TEST_F(ClientSessionTest, InputStubFilter) {
  protocol::KeyEvent key_event1;
  key_event1.set_pressed(true);
  key_event1.set_keycode(1);

  protocol::KeyEvent key_event2;
  key_event2.set_pressed(true);
  key_event2.set_keycode(2);

  protocol::KeyEvent key_event3;
  key_event3.set_pressed(true);
  key_event3.set_keycode(3);

  protocol::MouseEvent mouse_event1;
  mouse_event1.set_x(100);
  mouse_event1.set_y(101);

  protocol::MouseEvent mouse_event2;
  mouse_event2.set_x(200);
  mouse_event2.set_y(201);

  protocol::MouseEvent mouse_event3;
  mouse_event3.set_x(300);
  mouse_event3.set_y(301);

  protocol::LocalLoginCredentials credentials;
  credentials.set_type(protocol::PASSWORD);
  credentials.set_username("user");
  credentials.set_credential("password");

  InSequence s;
  EXPECT_CALL(*user_authenticator_, Authenticate(_, _))
      .WillOnce(Return(true));
  EXPECT_CALL(session_event_handler_, LocalLoginSucceeded(_));
  EXPECT_CALL(input_stub_, InjectKeyEvent(&key_event2, _));
  EXPECT_CALL(input_stub_, InjectMouseEvent(&mouse_event2, _));
  EXPECT_CALL(*connection_.get(), Disconnect());

  // These events should not get through to the input stub,
  // because the client isn't authenticated yet.
  client_session_->InjectKeyEvent(&key_event1, new DummyTask());
  client_session_->InjectMouseEvent(&mouse_event1, new DummyTask());
  client_session_->BeginSessionRequest(&credentials, new DummyTask());
  // These events should get through to the input stub.
  client_session_->InjectKeyEvent(&key_event2, new DummyTask());
  client_session_->InjectMouseEvent(&mouse_event2, new DummyTask());
  client_session_->Disconnect();
  // These events should not get through to the input stub,
  // because the client has disconnected.
  client_session_->InjectKeyEvent(&key_event3, new DummyTask());
  client_session_->InjectMouseEvent(&mouse_event3, new DummyTask());
}

}  // namespace remoting
