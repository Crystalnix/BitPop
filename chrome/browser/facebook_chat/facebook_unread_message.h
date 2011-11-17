// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_UNREAD_MESSAGE_H_
#define CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_UNREAD_MESSAGE_H_
#pragma once

#include <string>
#include "base/observer_list.h"
#include "base/timer.h"
#include "base/scoped_ptr.h"

class FacebookUnreadMessage {
  public:
    FacebookUnreadMessage(const std::string &message);
    ~FacebookUnreadMessage();

    std::string message() const;
    bool isVisible() const;
    
    void StartCountdown();

    virtual void Show() = 0;
    virtual void Hide() = 0;

    // class Observer {
    //   public:
    //     virtual ShouldHide(FacebookUnreadMessage *unread_message) = 0;
    // };

    // void AddObserver(Observer *observer);
    // void RemoveObserver(Observer *observer);

  private:
    std::string message_;
    bool isVisible_;

    OneShotTimer<FacebookUnreadMessage> timer_;
    // ObserverList<Observer> observers_;
};

#endif  // CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_UNREAD_MESSAGE_H_
