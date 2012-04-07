// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DBUS_POWER_MANAGER_CLIENT_H_
#define CHROME_BROWSER_CHROMEOS_DBUS_POWER_MANAGER_CLIENT_H_

#include <string>

#include "base/basictypes.h"
#include "base/callback.h"

namespace base {
class TimeTicks;
}
namespace dbus {
class Bus;
}

namespace chromeos {

// This is the local struct that is used in Chrome.
struct PowerSupplyStatus {
  bool line_power_on;

  bool battery_is_present;
  bool battery_is_full;

  // Time in seconds until the battery is empty or full, 0 for unknown.
  int64 battery_seconds_to_empty;
  int64 battery_seconds_to_full;

  double battery_percentage;

  PowerSupplyStatus();
  std::string ToString() const;
};

// Callback used for processing the idle time.  The int64 param is the number of
// seconds the user has been idle.
typedef base::Callback<void(int64)> CalculateIdleTimeCallback;

// PowerManagerClient is used to communicate with the power manager.
class PowerManagerClient {
 public:
  // Interface for observing changes from the power manager.
  class Observer {
   public:
    virtual ~Observer() {}

    // Called when the brightness is changed.
    // |level| is of the range [0, 100].
    // |user_initiated| is true if the action is initiated by the user.
    virtual void BrightnessChanged(int level, bool user_initiated) {}

    // Called when power supply polling takes place.  |status| is a data
    // structure that contains the current state of the power supply.
    virtual void PowerChanged(const PowerSupplyStatus& status) {}

    // Called when the system resumes from suspend.
    virtual void SystemResumed() {}

    // Called when the power button is pressed or released.
    virtual void PowerButtonStateChanged(bool down,
                                         const base::TimeTicks& timestamp) {}

    // Called when the lock button is pressed or released.
    virtual void LockButtonStateChanged(bool down,
                                        const base::TimeTicks& timestamp) {}

    // Called when the screen is locked.
    virtual void LockScreen() {}

    // Called when the screen is unlocked.
    virtual void UnlockScreen() {}

    // Called when the screen fails to unlock.
    virtual void UnlockScreenFailed() {}
  };

  // Adds and removes the observer.
  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;
  virtual bool HasObserver(Observer* observer) = 0;

  // Decreases the screen brightness. |allow_off| controls whether or not
  // it's allowed to turn off the back light.
  virtual void DecreaseScreenBrightness(bool allow_off) = 0;

  // Increases the screen brightness.
  virtual void IncreaseScreenBrightness() = 0;

  // UI initiated request for power supply status update.
  virtual void RequestStatusUpdate() = 0;

  // Requests restart of the system.
  virtual void RequestRestart() = 0;

  // Requests shutdown of the system.
  virtual void RequestShutdown() = 0;

  // Calculates idle time asynchronously, after the idle time request has
  // replied.  It passes the idle time in seconds to |callback|.  If it
  // encounters some error, it passes -1 to |callback|.
  virtual void CalculateIdleTime(const CalculateIdleTimeCallback& callback) = 0;

  // Notifies PowerManager that a user requested to lock the screen.
  virtual void NotifyScreenLockRequested() = 0;

  // Notifies PowerManager that screen lock has been completed.
  virtual void NotifyScreenLockCompleted() = 0;

  // Notifies PowerManager that a user unlocked the screen.
  virtual void NotifyScreenUnlockRequested() = 0;

  // Notifies PowerManager that screen is unlocked.
  virtual void NotifyScreenUnlockCompleted() = 0;

  // Creates the instance.
  static PowerManagerClient* Create(dbus::Bus* bus);

  virtual ~PowerManagerClient();

 protected:
  // Create() should be used instead.
  PowerManagerClient();

 private:
  DISALLOW_COPY_AND_ASSIGN(PowerManagerClient);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_DBUS_POWER_MANAGER_CLIENT_H_
