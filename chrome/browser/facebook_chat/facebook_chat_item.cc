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
  active_(false),
  highlighted_(false),
  manager_(manager)
{
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

bool FacebookChatItem::active() const {
  return active_;
}

bool FacebookChatItem::highlighted() const {
  return highlighted_;
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

void FacebookChatItem::Activate() {
  if (!active_) {
    active_ = true;
    UpdateObservers();
  }
}

void FacebookChatItem::Deactivate() {
  if (active_) {
    active_ = false;
    UpdateObservers();
  }
}

void FacebookChatItem::SetHighlight() {
  if (!highlighted_) {
    highlighted_ = true;
    UpdateObservers();
  }
}

void FacebookChatItem::RemoveHighlight() {
  if (highlighted_) {
    highlighted_ = false;
    UpdateObservers();
  }
}
  

