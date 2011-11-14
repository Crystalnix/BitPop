// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/facebook_chat/facebook_chat_manager.h"

#include "chrome/browser/facebook_chat/facebook_chat_item.h"
#include "base/stl_util-inl.h"

namespace {
  const char kOfflineStatus[] = "offline";
  const char kAvailableStatus[] = "available";
}

FacebookChatManager::FacebookChatManager() :
    profile_(NULL),
    shutdown_needed_(false) {
}

FacebookChatManager::~FacebookChatManager() {
}

void FacebookChatManager::Shutdown() {
  if (!shutdown_needed_)
    return;
  shutdown_needed_ = false;

  FOR_EACH_OBSERVER(Observer, observers_, ManagerIsGoingDown());

  // TODO: review the following piece of code
  for (ChatSet::iterator it = chats_.begin(); it != chats_.end(); ) {
    // Uncomment this :-@
    // FacebookChatItem *item = *it;

    it++;

    // can do anything to destruct item
  }

  STLDeleteElements(&chats_);

  jid_chats_map_.clear();
}

bool FacebookChatManager::Init(Profile *profile) {
  DCHECK(profile);
  DCHECK(!shutdown_needed_)  << "FacebookChatManager already initialized.";
  shutdown_needed_ = true;

  profile_ = profile;

  return true;
}

FacebookChatItem* FacebookChatManager::CreateFacebookChat(
    const FacebookChatCreateInfo &info) {
  ChatMap::iterator it = jid_chats_map_.find(info.jid);
  if (it != jid_chats_map_.end())
    return it->second;

  FacebookChatItem::Status status = FacebookChatItem::OFFLINE;
  if (info.status == kAvailableStatus)
    status = FacebookChatItem::AVAILABLE;

  FacebookChatItem *item = new FacebookChatItem(this,
                                                info.jid,
                                                info.username,
                                                status);
  chats_.insert(item);
  jid_chats_map_[info.jid] = item;

  return item;
}

void FacebookChatManager::StartChat(const std::string &jid) {
  ChatMap::iterator it = jid_chats_map_.find(jid);
  if (it != jid_chats_map_.end()) {
    FacebookChatItem *item = it->second;
    ActivateItem(item);
  }
}

void FacebookChatManager::ActivateItem(FacebookChatItem *item) {
  for (ChatSet::iterator sit = chats_.begin(); sit != chats_.end(); sit++) {
    if (*sit != item)
      (*sit)->Deactivate();
  }
  item->Activate();
}

void FacebookChatManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
  observer->ModelChanged();
}

void FacebookChatManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void FacebookChatManager::NotifyModelChanged() {
  FOR_EACH_OBSERVER(Observer, observers_, ModelChanged());
}
