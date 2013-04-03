// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHAT_MANAGER_H_
#define CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHAT_MANAGER_H_
#pragma once

#include <string>
#include <set>

#include "base/basictypes.h"
#include "base/hash_tables.h"
#include "base/observer_list.h"
#include "chrome/browser/facebook_chat/facebook_chat_item.h"
#include "chrome/browser/facebook_chat/facebook_chat_create_info.h"
#include "chrome/browser/profiles/profile_keyed_service.h"

class Browser;
class Profile;

class FacebookChatManager : public ProfileKeyedService {
  public:
    FacebookChatManager();

    virtual void Shutdown() OVERRIDE;

    FacebookChatItem* GetItem(const std::string &jid);

    FacebookChatItem* CreateFacebookChat(const FacebookChatCreateInfo &info);

    void RemoveItem(const std::string &jid);

    void AddNewUnreadMessage(const std::string &jid,
        const std::string &message);

    void ChangeItemStatus(const std::string &jid,
        const std::string &status);

    class Observer {
      public:
        virtual void ModelChanged() = 0;

        virtual void ManagerIsGoingDown() {}
      private:
        virtual ~Observer() {}
    };

    // Allow objects to observe the download creation process.
    void AddObserver(Observer* observer);

    // Remove a download observer from ourself.
    void RemoveObserver(Observer* observer);

    // Returns true if initialized properly.
    bool Init(Profile *profile);

    int total_unread() const;
    std::string global_my_uid() const { return global_my_uid_; }
    void set_global_my_uid(const std::string& uid) { global_my_uid_ = uid; }

  protected:
    virtual ~FacebookChatManager();
  private:
    void NotifyModelChanged();

    typedef std::set<FacebookChatItem*> ChatSet;
    typedef base::hash_map<std::string, FacebookChatItem*> ChatMap;

    std::string global_my_uid_;
    ChatSet chats_;
    ChatMap jid_chats_map_;

    Profile *profile_;

    bool shutdown_needed_;

    ObserverList<Observer> observers_;

    DISALLOW_COPY_AND_ASSIGN(FacebookChatManager);
};

#endif  // CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHAT_MANAGER_H_
