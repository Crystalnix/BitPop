// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/facebook_chat/facebook_chat_item.h"

#include "base/logging.h"
#include "chrome/browser/facebook_chat/facebook_chat_manager.h"

FacebookChatItem::FacebookChatItem(FacebookChatManager *manager,
                                   const std::string &jid,
                                   const std::string &username,
                                   Status status)
: jid_(jid),
  username_(username),
  status_(status),
  needsActivation_(false),
  manager_(manager)
{
}

FacebookChatItem::~FacebookChatItem() {
  state_ = REMOVING;
  UpdateObservers();
}

std::string FacebookChatItem::jid() const {
  return jid_;
}

std::string FacebookChatItem::username() const {
  return username_;
}

FacebookChatItem::Status FacebookChatItem::status() const {
  return status_;
}

unsigned int FacebookChatItem::num_notifications() const {
  return unreadMessages_.size();
}

FacebookChatItem::State FacebookChatItem::state() const {
  return state_;
}

bool FacebookChatItem::needs_activation() const {
  return needsActivation_;
}

void FacebookChatItem::set_needs_activation(bool value) {
  needsActivation_ = value;
}
void FacebookChatItem::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void FacebookChatItem::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void FacebookChatItem::UpdateObservers() {
  FOR_EACH_OBSERVER(Observer, observers_, OnChatUpdated(this));
}

void FacebookChatItem::Remove() {
  state_ = REMOVING;
  manager_->RemoveItem(jid_);
}

void FacebookChatItem::AddNewUnreadMessage(const std::string &message) {
  //numNotifications_++;
  unreadMessages_.push_back(message);

  state_ = NUM_NOTIFICATIONS_CHANGED;
  UpdateObservers();
}

void FacebookChatItem::ClearUnreadMessages() {
  //numNotifications_ = 0;
  unreadMessages_.clear();

  state_ = NUM_NOTIFICATIONS_CHANGED;
  UpdateObservers();
}

std::string FacebookChatItem::GetMessageAtIndex(unsigned int index) {
  DCHECK(index < unreadMessages_.size());
  return unreadMessages_.at(index);
}

void FacebookChatItem::ChangeStatus(const std::string &status) {
  if (status == "active")
    status_ = AVAILABLE;
  else if (status == "idle")
    status_ = IDLE;
  else if (status == "error")
    status_ = ERROR_STATUS;
  else if (status == "composing")
    status_ = COMPOSING;
  else
    status_ = OFFLINE;

  state_ = STATUS_CHANGED;
  UpdateObservers();
}
