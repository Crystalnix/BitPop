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
#include "base/memory/ref_counted.h"
#include "content/browser/browser_thread.h"
#include "chrome/browser/facebook_chat/facebook_chat_item.h"
#include "chrome/browser/facebook_chat/facebook_chat_create_info.h"


class Profile;

class FacebookChatManager : public base::RefCountedThreadSafe<FacebookChatManager,
                                        BrowserThread::DeleteOnUIThread> {
  public:
    FacebookChatManager();
    virtual ~FacebookChatManager();

    void Shutdown();

    FacebookChatItem* CreateFacebookChat(const FacebookChatCreateInfo &info);
    void StartChat(const std::string &jid);

    class Observer {
      public:
        virtual void ChatAdded(const std::string &jid) = 0;
        virtual void ChatRemoved();
        virtual void ChatWindowOpened(FacebookChatItem *windowChatItem) = 0;
        virtual void ChatWindowHidden(FacebookChatItem *windowChatItem) = 0;

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
    bool Init(Profile* profile);

  private:
    void NotifyModelChanged();
    void ActivateItem(FacebookChatItem *item);

    typedef std::set<FacebookChatItem*> ChatSet;
    typedef base::hash_map<std::string, FacebookChatItem*> ChatMap;
    
    ChatSet chats_;
    ChatMap jid_chats_map_;

    Profile *profile_;

    bool shutdown_needed_;

    ObserverList<Observer> observers_;

    DISALLOW_COPY_AND_ASSIGN(FacebookChatManager);
};

#endif  // CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHAT_MANAGER_H_
