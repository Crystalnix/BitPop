// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_NOTIFICATIONS_BALLOON_COLLECTION_IMPL_H_
#define CHROME_BROWSER_CHROMEOS_NOTIFICATIONS_BALLOON_COLLECTION_IMPL_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/chromeos/notifications/balloon_view_host.h"
#include "chrome/browser/notifications/balloon_collection.h"
#include "chrome/browser/notifications/balloon_collection_base.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/gfx/point.h"
#include "ui/gfx/rect.h"

namespace gfx {
class Size;
}  // namespace gfx

namespace chromeos {

class BalloonViewImpl;

// A balloon collection represents a set of notification balloons being
// shown in the chromeos notification panel. Unlike other platforms,
// chromeos shows the all notifications in the notification panel, and
// this class does not manage the location of balloons.
class BalloonCollectionImpl : public BalloonCollection,
                              public content::NotificationObserver {
 public:
  // An interface to display balloons on the screen.
  // This is used for unit tests to inject a mock ui implementation.
  class NotificationUI {
   public:
    NotificationUI() {}
    virtual ~NotificationUI() {}

    // Add, remove, resize and show the balloon.
    virtual void Add(Balloon* balloon) = 0;
    virtual bool Update(Balloon* balloon) = 0;
    virtual void Remove(Balloon* balloon) = 0;
    virtual void Show(Balloon* balloon) = 0;

    // Resize notification from webkit.
    virtual void ResizeNotification(Balloon* balloon,
                                    const gfx::Size& size) = 0;

    // Sets the active view.
    virtual void SetActiveView(BalloonViewImpl* view) = 0;
   private:
    DISALLOW_COPY_AND_ASSIGN(NotificationUI);
  };

  BalloonCollectionImpl();
  virtual ~BalloonCollectionImpl();

  // BalloonCollectionInterface overrides
  virtual void Add(const Notification& notification,
                   Profile* profile) OVERRIDE;
  virtual bool RemoveById(const std::string& id) OVERRIDE;
  virtual bool RemoveBySourceOrigin(const GURL& origin) OVERRIDE;
  virtual void RemoveAll() OVERRIDE;
  virtual bool HasSpace() const OVERRIDE;
  virtual void ResizeBalloon(Balloon* balloon, const gfx::Size& size) OVERRIDE;
  virtual void SetPositionPreference(PositionPreference position) OVERRIDE {}
  virtual void DisplayChanged() OVERRIDE {}
  virtual void OnBalloonClosed(Balloon* source) OVERRIDE;
  virtual const Balloons& GetActiveBalloons() OVERRIDE;

  // NotificationObserver overrides:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Adds a callback for WebUI message. Returns true if the callback
  // is succssfully registered, or false otherwise. It fails to add if
  // there is no notification that matches NotificationDelegate::id(),
  // or a callback for given message already exists. The callback
  // object is owned and deleted by callee.
  bool AddWebUIMessageCallback(
      const Notification& notification,
      const std::string& message,
      const BalloonViewHost::MessageCallback& callback);

  // Adds new system notification.
  // |sticky| is used to indicate that the notification
  // is sticky and cannot be dismissed by a user.
  void AddSystemNotification(const Notification& notification,
                             Profile* profile,
                             bool sticky);

  // Updates the notification's content. It uses
  // NotificationDelegate::id() to check the equality of notifications.
  // Returns true if the notification has been updated. False if
  // no corresponding notification is found. This will not change the
  // visibility of the notification.
  bool UpdateNotification(const Notification& notification);

  // Updates and shows the notification. It will open the notification panel
  // if it's closed or minimized, and scroll the viewport so that
  // the updated notification is visible.
  bool UpdateAndShowNotification(const Notification& notification);

  // base_ is embedded, so this is a simple accessor for the number of
  // balloons in the collection.
  int count() const { return base_.count(); }

  // Injects notification ui. Used to inject a mock implementation in tests.
  void set_notification_ui(NotificationUI* ui) {
    notification_ui_.reset(ui);
  }

  NotificationUI* notification_ui() {
    return notification_ui_.get();
  }

 protected:
  // Creates a new balloon. Overridable by unit tests.  The caller is
  // responsible for freeing the pointer returned.
  virtual Balloon* MakeBalloon(const Notification& notification,
                               Profile* profile);

  // Base implementation for the collection of active balloons.
  BalloonCollectionBase base_;

 private:
  friend class NotificationPanelTester;

  // Shutdown the notification ui.
  void Shutdown();

  Balloon* FindBalloon(const Notification& notification) {
    return base_.FindBalloon(notification);
  }

  scoped_ptr<NotificationUI> notification_ui_;

  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(BalloonCollectionImpl);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_NOTIFICATIONS_BALLOON_COLLECTION_IMPL_H_
