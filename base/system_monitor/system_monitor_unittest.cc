// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/system_monitor/system_monitor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

class PowerTest : public SystemMonitor::PowerObserver {
 public:
  PowerTest()
      : battery_(false),
        power_state_changes_(0),
        suspends_(0),
        resumes_(0) {
  }

  // PowerObserver callbacks.
  void OnPowerStateChange(bool on_battery_power) {
    power_state_changes_++;
  }

  void OnSuspend() {
    suspends_++;
  }

  void OnResume() {
    resumes_++;
  }

  // Test status counts.
  bool battery() { return battery_; }
  int power_state_changes() { return power_state_changes_; }
  int suspends() { return suspends_; }
  int resumes() { return resumes_; }

 private:
  bool battery_;   // Do we currently think we're on battery power.
  int power_state_changes_;  // Count of OnPowerStateChange notifications.
  int suspends_;  // Count of OnSuspend notifications.
  int resumes_;  // Count of OnResume notifications.
};

TEST(SystemMonitor, PowerNotifications) {
  const int kObservers = 5;

  // Initialize a message loop for this to run on.
  MessageLoop loop;

#if defined(OS_MACOSX)
  SystemMonitor::AllocateSystemIOPorts();
#endif

  SystemMonitor system_monitor;
  PowerTest test[kObservers];
  for (int index = 0; index < kObservers; ++index)
    system_monitor.AddPowerObserver(&test[index]);

  // Send a bunch of power changes.  Since the battery power hasn't
  // actually changed, we shouldn't get notifications.
  for (int index = 0; index < 5; index++) {
    system_monitor.ProcessPowerMessage(SystemMonitor::POWER_STATE_EVENT);
    EXPECT_EQ(test[0].power_state_changes(), 0);
  }

  // Sending resume when not suspended should have no effect.
  system_monitor.ProcessPowerMessage(SystemMonitor::RESUME_EVENT);
  loop.RunAllPending();
  EXPECT_EQ(test[0].resumes(), 0);

  // Pretend we suspended.
  system_monitor.ProcessPowerMessage(SystemMonitor::SUSPEND_EVENT);
  loop.RunAllPending();
  EXPECT_EQ(test[0].suspends(), 1);

  // Send a second suspend notification.  This should be suppressed.
  system_monitor.ProcessPowerMessage(SystemMonitor::SUSPEND_EVENT);
  loop.RunAllPending();
  EXPECT_EQ(test[0].suspends(), 1);

  // Pretend we were awakened.
  system_monitor.ProcessPowerMessage(SystemMonitor::RESUME_EVENT);
  loop.RunAllPending();
  EXPECT_EQ(test[0].resumes(), 1);

  // Send a duplicate resume notification.  This should be suppressed.
  system_monitor.ProcessPowerMessage(SystemMonitor::RESUME_EVENT);
  loop.RunAllPending();
  EXPECT_EQ(test[0].resumes(), 1);
}

class DevicesChangedTest : public SystemMonitor::DevicesChangedObserver {
 public:
  DevicesChangedTest()
      : changes_(0) {
  }

  // DevicesChangedObserver callbacks.
  virtual void OnDevicesChanged() OVERRIDE {
    changes_++;
  }

  // Test status counts.
  int changes() const { return changes_; }

 private:
  int changes_;  // Count of OnDevicesChanged notifications.

  DISALLOW_COPY_AND_ASSIGN(DevicesChangedTest);
};

TEST(SystemMonitor, DeviceChangeNotifications) {
  const int kObservers = 5;

  // Initialize a message loop for this to run on.
  MessageLoop loop;

#if defined(OS_MACOSX)
  SystemMonitor::AllocateSystemIOPorts();
#endif

  SystemMonitor system_monitor;
  DevicesChangedTest test[kObservers];
  for (int index = 0; index < kObservers; ++index)
    system_monitor.AddDevicesChangedObserver(&test[index]);

  system_monitor.ProcessDevicesChanged();
  loop.RunAllPending();
  EXPECT_EQ(1, test[0].changes());

  system_monitor.ProcessDevicesChanged();
  system_monitor.ProcessDevicesChanged();
  loop.RunAllPending();
  EXPECT_EQ(3, test[0].changes());
}

}  // namespace base
