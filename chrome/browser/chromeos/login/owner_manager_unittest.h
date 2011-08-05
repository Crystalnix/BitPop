// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_OWNER_MANAGER_UNITTEST_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_OWNER_MANAGER_UNITTEST_H_

#include "chrome/browser/chromeos/login/owner_manager.h"

#include <string>
#include <vector>

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "content/common/notification_observer.h"
#include "content/common/notification_registrar.h"
#include "content/common/notification_service.h"
#include "content/common/notification_type.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"


namespace chromeos {
class MockKeyLoadObserver : public NotificationObserver {
 public:
  MockKeyLoadObserver()
      : success_expected_(false),
        quit_on_observe_(true),
        observed_(false) {
    registrar_.Add(
        this,
        NotificationType::OWNER_KEY_FETCH_ATTEMPT_FAILED,
        NotificationService::AllSources());
    registrar_.Add(
        this,
        NotificationType::OWNER_KEY_FETCH_ATTEMPT_SUCCEEDED,
        NotificationService::AllSources());
  }

  virtual ~MockKeyLoadObserver();

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  void ExpectKeyFetchSuccess(bool should_succeed) {
    success_expected_ = should_succeed;
  }

  void SetQuitOnKeyFetch(bool should_quit) { quit_on_observe_ = should_quit; }

 private:
  NotificationRegistrar registrar_;
  bool success_expected_;
  bool quit_on_observe_;
  bool observed_;
  DISALLOW_COPY_AND_ASSIGN(MockKeyLoadObserver);
};

class MockKeyUser : public OwnerManager::Delegate {
 public:
  explicit MockKeyUser(const OwnerManager::KeyOpCode expected)
      : expected_(expected),
        quit_on_callback_(true) {
  }
  MockKeyUser(const OwnerManager::KeyOpCode expected, bool quit_on_callback)
      : expected_(expected),
        quit_on_callback_(quit_on_callback) {
  }

  virtual ~MockKeyUser() {}

  virtual void OnKeyOpComplete(const OwnerManager::KeyOpCode return_code,
                       const std::vector<uint8>& payload);

  const OwnerManager::KeyOpCode expected_;
  const bool quit_on_callback_;
 private:
  DISALLOW_COPY_AND_ASSIGN(MockKeyUser);
};

class MockKeyUpdateUser : public OwnerManager::KeyUpdateDelegate {
 public:
  MockKeyUpdateUser() {}
  virtual ~MockKeyUpdateUser() {}

  virtual void OnKeyUpdated();

 private:
  DISALLOW_COPY_AND_ASSIGN(MockKeyUpdateUser);
};


class MockSigner : public OwnerManager::Delegate {
 public:
  MockSigner(const OwnerManager::KeyOpCode expected,
             const std::vector<uint8>& sig);
  virtual ~MockSigner();

  virtual void OnKeyOpComplete(const OwnerManager::KeyOpCode return_code,
                       const std::vector<uint8>& payload);

  const OwnerManager::KeyOpCode expected_code_;
  const std::vector<uint8> expected_sig_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MockSigner);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_OWNER_MANAGER_UNITTEST_H_
