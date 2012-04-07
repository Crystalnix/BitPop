// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/test/test_notification_tracker.h"

#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"

TestNotificationTracker::Event::Event()
    : type(content::NOTIFICATION_ALL),
      source(content::NotificationService::AllSources()),
      details(content::NotificationService::NoDetails()) {
}
TestNotificationTracker::Event::Event(int t,
                                      content::NotificationSource s,
                                      content::NotificationDetails d)
    : type(t),
      source(s),
      details(d) {
}

TestNotificationTracker::TestNotificationTracker() {
}

TestNotificationTracker::~TestNotificationTracker() {
}

void TestNotificationTracker::ListenFor(
    int type,
    const content::NotificationSource& source) {
  registrar_.Add(this, type, source);
}

void TestNotificationTracker::Reset() {
  events_.clear();
}

bool TestNotificationTracker::Check1AndReset(int type) {
  if (size() != 1) {
    Reset();
    return false;
  }
  bool success = events_[0].type == type;
  Reset();
  return success;
}

bool TestNotificationTracker::Check2AndReset(int type1,
                                             int type2) {
  if (size() != 2) {
    Reset();
    return false;
  }
  bool success = events_[0].type == type1 && events_[1].type == type2;
  Reset();
  return success;
}

bool TestNotificationTracker::Check3AndReset(int type1,
                                             int type2,
                                             int type3) {
  if (size() != 3) {
    Reset();
    return false;
  }
  bool success = events_[0].type == type1 &&
                 events_[1].type == type2 &&
                 events_[2].type == type3;
  Reset();
  return success;
}

void TestNotificationTracker::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  events_.push_back(Event(type, source, details));
}
