// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The XmppSignalStrategy encapsulates all the logic to perform the signaling
// STUN/ICE for jingle via a direct XMPP connection.
//
// This class is not threadsafe.

#ifndef REMOTING_JINGLE_GLUE_XMPP_SIGNAL_STRATEGY_H_
#define REMOTING_JINGLE_GLUE_XMPP_SIGNAL_STRATEGY_H_

#include "remoting/jingle_glue/signal_strategy.h"

#include <vector>

#include "base/compiler_specific.h"
#include "base/observer_list.h"
#include "base/threading/non_thread_safe.h"
#include "third_party/libjingle/source/talk/base/sigslot.h"
#include "third_party/libjingle/source/talk/xmpp/xmppclient.h"

namespace remoting {

class JingleThread;

class XmppSignalStrategy : public base::NonThreadSafe,
                           public SignalStrategy,
                           public buzz::XmppStanzaHandler,
                           public sigslot::has_slots<> {
 public:
  XmppSignalStrategy(JingleThread* thread,
                     const std::string& username,
                     const std::string& auth_token,
                     const std::string& auth_token_service);
  virtual ~XmppSignalStrategy();

  // SignalStrategy interface.
  virtual void Connect() OVERRIDE;
  virtual void Disconnect() OVERRIDE;
  virtual State GetState() const OVERRIDE;
  virtual std::string GetLocalJid() const OVERRIDE;
  virtual void AddListener(Listener* listener) OVERRIDE;
  virtual void RemoveListener(Listener* listener) OVERRIDE;
  virtual bool SendStanza(buzz::XmlElement* stanza) OVERRIDE;
  virtual std::string GetNextId() OVERRIDE;

  // buzz::XmppStanzaHandler interface.
  virtual bool HandleStanza(const buzz::XmlElement* stanza) OVERRIDE;

  // This method is used to update the auth info (for example when the OAuth
  // access token is renewed). It is OK to call this even when we are in the
  // CONNECTED state. It will be used on the next Connect() call.
  void SetAuthInfo(const std::string& username,
                   const std::string& auth_token,
                   const std::string& auth_token_service);

  // Use this method to override the default resource name used (optional).
  // This will be used on the next Connect() call.
  void SetResourceName(const std::string& resource_name);

 private:
  static buzz::PreXmppAuth* CreatePreXmppAuth(
      const buzz::XmppClientSettings& settings);

  void OnConnectionStateChanged(buzz::XmppEngine::State state);
  void SetState(State new_state);

  JingleThread* thread_;

  std::string username_;
  std::string auth_token_;
  std::string auth_token_service_;
  std::string resource_name_;
  buzz::XmppClient* xmpp_client_;

  State state_;

  ObserverList<Listener> listeners_;

  DISALLOW_COPY_AND_ASSIGN(XmppSignalStrategy);
};

}  // namespace remoting

#endif  // REMOTING_JINGLE_GLUE_XMPP_SIGNAL_STRATEGY_H_
