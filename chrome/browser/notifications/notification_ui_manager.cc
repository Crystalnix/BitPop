// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/notification_ui_manager.h"

#include "base/command_line.h"
#include "chrome/browser/notifications/balloon_notification_ui_manager.h"
#include "chrome/browser/notifications/message_center_notification_manager.h"
#include "chrome/common/chrome_switches.h"

// static
bool NotificationUIManager::DelegatesToMessageCenter() {
#if defined(OS_WIN) || defined(OS_CHROMEOS)
  return CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableRichNotifications);
#else
  return false;
#endif
}

#if !defined(OS_MACOSX)
// static
NotificationUIManager* NotificationUIManager::Create(PrefService* local_state) {
  if (DelegatesToMessageCenter()) {
    return new MessageCenterNotificationManager();
  } else {
    BalloonNotificationUIManager* balloon_manager =
        new BalloonNotificationUIManager(local_state);
    balloon_manager->SetBalloonCollection(BalloonCollection::Create());
    return balloon_manager;
  }
}
#endif

