// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_test_message_listener.h"

#include "base/string_number_conversions.h"
#include "chrome/browser/extensions/extension_test_api.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_service.h"

ExtensionTestMessageListener::ExtensionTestMessageListener(
    const std::string& expected_message,
    bool will_reply)
    : expected_message_(expected_message),
      satisfied_(false),
      waiting_(false),
      will_reply_(will_reply) {
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_TEST_MESSAGE,
                 content::NotificationService::AllSources());
}

ExtensionTestMessageListener::~ExtensionTestMessageListener() {}

bool ExtensionTestMessageListener::WaitUntilSatisfied()  {
  if (satisfied_)
    return true;
  waiting_ = true;
  ui_test_utils::RunMessageLoop();
  return satisfied_;
}

void ExtensionTestMessageListener::Reply(const std::string& message) {
  DCHECK(satisfied_);
  DCHECK(will_reply_);
  function_->Reply(message);
  function_ = NULL;
  will_reply_ = false;
}

void ExtensionTestMessageListener::Reply(int message) {
  Reply(base::IntToString(message));
}

void ExtensionTestMessageListener::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  const std::string& content = *content::Details<std::string>(details).ptr();
  if (!satisfied_ && content == expected_message_) {
    function_ = content::Source<ExtensionTestSendMessageFunction>(source).ptr();
    satisfied_ = true;
    registrar_.RemoveAll();  // Stop listening for more messages.
    if (!will_reply_) {
      function_->Reply("");
      function_ = NULL;
    }
    if (waiting_) {
      waiting_ = false;
      MessageLoopForUI::current()->Quit();
    }
  }
}
