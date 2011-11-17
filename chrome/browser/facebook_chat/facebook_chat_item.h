// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_H_
#define CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_H_
#pragma once

#include <string>
#include <list>

#include "base/observer_list.h"

class FacebookChatManager;

class FacebookChatItem {
  public:
    enum Status {
      AVAILABLE,
      OFFLINE
    };

    enum State {
      NORMAL = 0,
      REMOVING,
      ACTIVE_STATUS_CHANGED,
      HIGHLIGHT_STATUS_CHANGED,
      NUM_NOTIFICATIONS_CHANGED
    };

    FacebookChatItem(FacebookChatManager *manager,
        const std::string &jid,
        const std::string &username,
        Status status);
    virtual ~FacebookChatItem();

    class Observer {
      public:
        virtual void OnChatUpdated(FacebookChatItem *source) = 0;
      protected:
        virtual ~Observer() {}
    };

    std::string jid() const;
    std::string username() const;
    Status status() const;
    unsigned int num_notifications() const;
    bool active() const;
    bool highlighted() const;
    State state() const;
    bool needs_activation() const;
    void set_needs_activation(bool value);

    void Remove();

    void AddNewUnreadMessage(const std::string &message);
    void ClearUnreadMessages();

    void AddObserver(Observer* observer);
    void RemoveObserver(Observer* observer);
  private:
    friend class FacebookChatManager;

    void UpdateObservers();

    std::string jid_;
    std::string username_;
    Status status_;
    State state_;

    unsigned int numNotifications_;
    //std::list<FacebookUnreadMessage> unreadMessages_;
    
    bool needsActivation_;

    // Our owning chat manager
    FacebookChatManager *manager_;

    ObserverList<Observer> observers_;
};

#endif  // CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_H_

