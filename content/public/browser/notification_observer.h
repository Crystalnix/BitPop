// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_NOTIFICATION_OBSERVER_H_
#define CONTENT_PUBLIC_BROWSER_NOTIFICATION_OBSERVER_H_
#pragma once

#include "content/common/content_export.h"

namespace content {

class NotificationDetails;
class NotificationSource;

// This is the base class for notification observers. When a matching
// notification is posted to the notification service, Observe is called.
class CONTENT_EXPORT NotificationObserver {
 public:
  NotificationObserver() {}
  virtual ~NotificationObserver() {}

  virtual void Observe(int type,
                       const NotificationSource& source,
                       const NotificationDetails& details) = 0;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_NOTIFICATION_OBSERVER_H_
