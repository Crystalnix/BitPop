// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_KIOSK_MODE_KIOSK_MODE_IDLE_LOGOUT_H_
#define CHROME_BROWSER_CHROMEOS_KIOSK_MODE_KIOSK_MODE_IDLE_LOGOUT_H_

#include "ash/wm/user_activity_observer.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chromeos/dbus/power_manager_client.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

namespace chromeos {

class KioskModeIdleLogout : public ash::UserActivityObserver,
                            public PowerManagerClient::Observer,
                            public content::NotificationObserver {
 public:
  static void Initialize();

  KioskModeIdleLogout();

 private:
  friend class KioskModeIdleLogoutTest;

  // Really initialize idle logout when KioskModeHelper is initialized.
  void Setup();

  // Overridden from content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Overridden from PowerManagerClient::Observer:
  virtual void IdleNotify(int64 threshold) OVERRIDE;

  // UserActivityObserver::Observer overrides:
  virtual void OnUserActivity() OVERRIDE;

  void SetupIdleNotifications();
  void RequestNextIdleNotification();

  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(KioskModeIdleLogout);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_KIOSK_MODE_KIOSK_MODE_IDLE_LOGOUT_H_
