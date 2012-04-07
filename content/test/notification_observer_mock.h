// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_TEST_NOTIFICATION_OBSERVER_MOCK_H_
#define CONTENT_TEST_NOTIFICATION_OBSERVER_MOCK_H_
#pragma once

#include "content/public/browser/notification_observer.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace content {

class NotificationDetails;
class NotificationSource;

class NotificationObserverMock : public NotificationObserver {
 public:
  NotificationObserverMock();
  virtual ~NotificationObserverMock();

  MOCK_METHOD3(Observe, void(int type,
                             const NotificationSource& source,
                             const NotificationDetails& details));
};

}  // namespace content

#endif  // CONTENT_TEST_NOTIFICATION_OBSERVER_MOCK_H_
