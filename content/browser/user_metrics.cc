// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/user_metrics.h"

#include "base/bind.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"

namespace content {
namespace {

// Forward declare because of circular dependency.
void CallRecordOnUI(const std::string& action);

void Record(const char *action) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&CallRecordOnUI, action));
    return;
  }

  NotificationService::current()->Notify(
      NOTIFICATION_USER_ACTION,
      NotificationService::AllSources(),
      Details<const char*>(&action));
}

void CallRecordOnUI(const std::string& action) {
  Record(action.c_str());
}

}  // namespace

void RecordAction(const UserMetricsAction& action) {
  Record(action.str_);
}

void RecordComputedAction(const std::string& action) {
  Record(action.c_str());
}

}  // namespace content
