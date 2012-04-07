// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/status_icons/desktop_notification_balloon.h"

#include "base/bind.h"
#include "base/string_number_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/notifications/desktop_notification_service.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/notifications/notification_delegate.h"
#include "chrome/browser/notifications/notification_ui_manager.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/webui/web_ui_util.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace {

void CloseBalloon(const std::string& id) {
  g_browser_process->notification_ui_manager()->CancelById(id);
}

// Prefix added to the notification ids.
const char kNotificationPrefix[] = "desktop_notification_balloon.";

// Timeout for automatically dismissing the notification balloon.
const size_t kTimeoutSeconds = 6;

class DummyNotificationDelegate : public NotificationDelegate {
 public:
  explicit DummyNotificationDelegate(const std::string& id)
      : id_(kNotificationPrefix + id) {}
  virtual ~DummyNotificationDelegate() {}

  virtual void Display() OVERRIDE {
    MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&CloseBalloon, id()),
        base::TimeDelta::FromSeconds(kTimeoutSeconds));
  }
  virtual void Error() OVERRIDE {}
  virtual void Close(bool by_user) OVERRIDE {}
  virtual void Click() OVERRIDE {}
  virtual std::string id() const OVERRIDE { return id_; }

 private:
  std::string id_;
};

}  // anonymous namespace

int DesktopNotificationBalloon::id_count_ = 1;

DesktopNotificationBalloon::DesktopNotificationBalloon() {
}

DesktopNotificationBalloon::~DesktopNotificationBalloon() {
  if (notification_.get())
    CloseBalloon(notification_->notification_id());
}

void DesktopNotificationBalloon::DisplayBalloon(const SkBitmap& icon,
                                                const string16& title,
                                                const string16& contents) {
  GURL icon_url;
  if (!icon.empty())
    icon_url = GURL(web_ui_util::GetImageDataUrl(icon));

  GURL content_url(DesktopNotificationService::CreateDataUrl(
      icon_url, title, contents, WebKit::WebTextDirectionDefault));

  notification_.reset(new Notification(
      GURL(), content_url, string16(), string16(),
      new DummyNotificationDelegate(base::IntToString(id_count_++))));

  // Allowing IO access is required here to cover the corner case where
  // there is no last used profile and the default one is loaded.
  // IO access won't be required for normal uses.
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  g_browser_process->notification_ui_manager()->Add(
      *notification_.get(), ProfileManager::GetLastUsedProfile());
}
