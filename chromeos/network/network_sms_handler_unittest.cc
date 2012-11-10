// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/network_sms_handler.h"

#include <set>
#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {

namespace {

class TestObserver : public NetworkSmsHandler::Observer {
 public:
  TestObserver() {}
  virtual ~TestObserver() {}

  virtual void MessageReceived(const base::DictionaryValue& message) OVERRIDE {
    std::string text;
    if (message.GetStringWithoutPathExpansion(
            NetworkSmsHandler::kTextKey, &text)) {
      messages_.insert(text);
    }
  }

  void ClearMessages() {
    messages_.clear();
  }

  int message_count() { return messages_.size(); }
  const std::set<std::string>& messages() const {
    return messages_;
  }

 private:
  std::set<std::string> messages_;
};

}  // namespace

class NetworkSmsHandlerTest : public testing::Test {
 public:
  NetworkSmsHandlerTest() {}
  virtual ~NetworkSmsHandlerTest() {}

  virtual void SetUp() OVERRIDE {
    // Initialize DBusThreadManager with a stub implementation.
    DBusThreadManager::InitializeWithStub();
  }

  virtual void TearDown() OVERRIDE {
    DBusThreadManager::Shutdown();
  }

 protected:
  MessageLoopForUI message_loop_;
};

TEST_F(NetworkSmsHandlerTest, SmsHandlerDbusStub) {
  // This relies on the stub dbus implementations for FlimflamManagerClient,
  // FlimflamDeviceClient, GsmSMSClient, ModemMessagingClient and SMSClient.
  // Initialize a sms handler. The stub dbus clients will not send the
  // first test message until RequestUpdate has been called.
  scoped_ptr<NetworkSmsHandler> sms_handler(new NetworkSmsHandler());
  scoped_ptr<TestObserver> test_observer(new TestObserver());
  sms_handler->AddObserver(test_observer.get());
  sms_handler->Init();
  message_loop_.RunAllPending();
  EXPECT_EQ(test_observer->message_count(), 0);

  // Test that no messages have been received yet
  const std::set<std::string>& messages(test_observer->messages());
  // Note: The following string corresponds to values in
  // ModemMessagingClientStubImpl and SmsClientStubImpl.
  const char kMessage1[] = "SMSClientStubImpl: Test Message: /SMS/0";
  EXPECT_EQ(messages.find(kMessage1), messages.end());

  // Test for messages delivered by signals.
  test_observer->ClearMessages();
  sms_handler->RequestUpdate();
  message_loop_.RunAllPending();
  EXPECT_GE(test_observer->message_count(), 1);
  EXPECT_NE(messages.find(kMessage1), messages.end());
}

}  // namespace chromeos
