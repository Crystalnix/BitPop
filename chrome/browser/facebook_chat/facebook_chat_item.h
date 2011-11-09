// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_H_
#define CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_H_
#pragma once

#include <string>

#include "base/observer_list.h"

class FacebookChatManager;

class FacebookChatItem {
  public:
    enum Status {
      AVAILABLE,
      OFFLINE
    };
 
    FacebookChatItem(FacebookChatManager *manager,
        const std::string &jid,
        const std::string &username,
        Status status);
    virtual ~FacebookChatItem() {}

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

    void UpdateUsernameChanged(const std::string &new_username);
    void UpdateStatusChanged(Status new_status);
    void UpdateNewMessage();

    void Activate();
    void Deactivate();

    void SetHighlight();
    void RemoveHighlight();

    void AddObserver(Observer* observer);
    void RemoveObserver(Observer* observer);
  private:
    void UpdateObservers();

    std::string jid_;
    std::string username_;
    Status status_;

    unsigned int numNotifications_;

    bool active_;
    bool highlighted_;

    // Our owning chat manager
    FacebookChatManager *manager_;

    ObserverList<Observer> observers_;
};

#endif  // CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_H_

