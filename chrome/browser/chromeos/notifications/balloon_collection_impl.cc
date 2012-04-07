// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/notifications/balloon_collection_impl.h"

#include <algorithm>

#include "base/logging.h"
#include "chrome/browser/chromeos/notifications/balloon_view.h"
#include "chrome/browser/chromeos/notifications/notification_panel.h"
#include "chrome/browser/notifications/balloon.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/notification_service.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/size.h"

namespace {

// Margin from the edge of the work area
const int kVerticalEdgeMargin = 5;
const int kHorizontalEdgeMargin = 5;

}  // namespace

namespace chromeos {

BalloonCollectionImpl::BalloonCollectionImpl()
    : notification_ui_(new NotificationPanel()) {
  registrar_.Add(this, chrome::NOTIFICATION_BROWSER_CLOSED,
                 content::NotificationService::AllSources());
}

BalloonCollectionImpl::~BalloonCollectionImpl() {
  Shutdown();
}

void BalloonCollectionImpl::Add(const Notification& notification,
                                Profile* profile) {
  Balloon* new_balloon = MakeBalloon(notification, profile);
  base_.Add(new_balloon, false);
  new_balloon->Show();
  notification_ui_->Add(new_balloon);

  // There may be no listener in a unit test.
  if (space_change_listener_)
    space_change_listener_->OnBalloonSpaceChanged();

  // This is used only for testing.
  if (!on_collection_changed_callback_.is_null())
    on_collection_changed_callback_.Run();
}

bool BalloonCollectionImpl::AddWebUIMessageCallback(
    const Notification& notification,
    const std::string& message,
    const BalloonViewHost::MessageCallback& callback) {
  Balloon* balloon = FindBalloon(notification);
  if (!balloon)
    return false;

  BalloonViewHost* host =
      static_cast<BalloonViewHost*>(balloon->view()->GetHost());
  return host->AddWebUIMessageCallback(message, callback);
}

// Called from SystemNotification::Show for system notifications.
void BalloonCollectionImpl::AddSystemNotification(
    const Notification& notification,
    Profile* profile,
    bool sticky) {
  Balloon* new_balloon = new Balloon(notification, profile, this);
  new_balloon->set_view(
      new chromeos::BalloonViewImpl(
          sticky, false /*no controls*/, true /*enable webui*/));
  base_.Add(new_balloon, false);
  new_balloon->Show();
  notification_ui_->Add(new_balloon);

  // There may be no listener in a unit test.
  if (space_change_listener_)
    space_change_listener_->OnBalloonSpaceChanged();
}

bool BalloonCollectionImpl::UpdateNotification(
    const Notification& notification) {
  Balloon* balloon = FindBalloon(notification);
  if (!balloon)
    return false;
  balloon->Update(notification);
  notification_ui_->Update(balloon);
  return true;
}

bool BalloonCollectionImpl::UpdateAndShowNotification(
    const Notification& notification) {
  Balloon* balloon = FindBalloon(notification);
  if (!balloon)
    return false;
  balloon->Update(notification);
  bool updated = notification_ui_->Update(balloon);
  DCHECK(updated);
  notification_ui_->Show(balloon);
  return true;
}

bool BalloonCollectionImpl::RemoveById(const std::string& id) {
  return base_.CloseById(id);
}

bool BalloonCollectionImpl::RemoveBySourceOrigin(const GURL& origin) {
  return base_.CloseAllBySourceOrigin(origin);
}

void BalloonCollectionImpl::RemoveAll() {
  base_.CloseAll();
}

bool BalloonCollectionImpl::HasSpace() const {
  return true;
}

void BalloonCollectionImpl::ResizeBalloon(Balloon* balloon,
                                          const gfx::Size& size) {
  notification_ui_->ResizeNotification(balloon, size);
}

void BalloonCollectionImpl::OnBalloonClosed(Balloon* source) {
  notification_ui_->Remove(source);
  base_.Remove(source);  // Deletes |source|.

  // There may be no listener in a unit test.
  if (space_change_listener_)
    space_change_listener_->OnBalloonSpaceChanged();

  // This is used only for testing.
  if (!on_collection_changed_callback_.is_null())
    on_collection_changed_callback_.Run();
}

const BalloonCollectionImpl::Balloons&
    BalloonCollectionImpl::GetActiveBalloons() {
  return base_.balloons();
}

void BalloonCollectionImpl::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(type == chrome::NOTIFICATION_BROWSER_CLOSED);
  bool app_closing = *content::Details<bool>(details).ptr();
  // When exiting, we need to shutdown all renderers in
  // BalloonViewImpl before IO thread gets deleted in the
  // BrowserProcessImpl's destructor.  See http://crbug.com/40810
  // for details.
  if (app_closing)
    RemoveAll();
}

void BalloonCollectionImpl::Shutdown() {
  // We need to remove the panel first because deleting
  // views that are not owned by parent will not remove
  // themselves from the parent.
  DVLOG(1) << "Shutting down notification UI";
  notification_ui_.reset();
}

// Called from BalloonCollectionImpl::Add for non system notifications.
Balloon* BalloonCollectionImpl::MakeBalloon(const Notification& notification,
                                            Profile* profile) {
  Balloon* new_balloon = new Balloon(notification, profile, this);
  new_balloon->set_view(new chromeos::BalloonViewImpl(
      false /*not sticky*/, true /*has controls*/, false /*no web ui*/));
  return new_balloon;
}

}  // namespace chromeos

// static
BalloonCollection* BalloonCollection::Create() {
  return new chromeos::BalloonCollectionImpl();
}
