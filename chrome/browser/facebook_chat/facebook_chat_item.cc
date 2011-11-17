// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/facebook_chat/facebook_chat_item.h"
#include "chrome/browser/facebook_chat/facebook_chat_manager.h"

FacebookChatItem::FacebookChatItem(FacebookChatManager *manager,
                                   const std::string &jid,
                                   const std::string &username,
                                   Status status)
: jid_(jid),
  username_(username),
  status_(status),
  numNotifications_(0),
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
  return numNotifications_;
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
  numNotifications_++;
  state_ = NUM_NOTIFICATIONS_CHANGED;
  UpdateObservers();
}

void FacebookChatItem::ClearUnreadMessages() {
  numNotifications_ = 0;
  state_ = NUM_NOTIFICATIONS_CHANGED;
  UpdateObservers();
}
