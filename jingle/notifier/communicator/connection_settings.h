// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef JINGLE_NOTIFIER_COMMUNICATOR_CONNECTION_SETTINGS_H_
#define JINGLE_NOTIFIER_COMMUNICATOR_CONNECTION_SETTINGS_H_

#include <deque>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "talk/xmpp/xmppclientsettings.h"

namespace notifier {

class ConnectionSettings {
 public:
  ConnectionSettings();
  ~ConnectionSettings();

  cricket::ProtocolType protocol() { return protocol_; }
  const talk_base::SocketAddress& server() const { return server_; }

  void set_protocol(cricket::ProtocolType protocol) { protocol_ = protocol; }
  talk_base::SocketAddress* mutable_server() { return &server_; }

  void FillXmppClientSettings(buzz::XmppClientSettings* xcs) const;

 private:
  cricket::ProtocolType protocol_;  // PROTO_TCP, PROTO_SSLTCP, etc.
  talk_base::SocketAddress server_;  // Server.
  // Need copy constructor due to use in stl deque.
};

class ConnectionSettingsList {
 public:
  ConnectionSettingsList();
  ~ConnectionSettingsList();

  int GetCount() { return list_.size(); }
  ConnectionSettings* GetSettings(size_t index) { return &list_[index]; }

  void ClearPermutations() {
    list_.clear();
    iplist_seen_.clear();
  }

  void AddPermutations(const std::string& hostname,
                       const std::vector<uint32>& iplist,
                       uint16 port,
                       bool special_port_magic,
                       bool try_ssltcp_first);
 private:
  void PermuteForAddress(const talk_base::SocketAddress& server,
                         bool special_port_magic,
                         bool try_ssltcp_first,
                         std::deque<ConnectionSettings>* list_temp);

  ConnectionSettings template_;
  std::deque<ConnectionSettings> list_;
  std::vector<uint32> iplist_seen_;
  DISALLOW_COPY_AND_ASSIGN(ConnectionSettingsList);
};

}  // namespace notifier

#endif  // JINGLE_NOTIFIER_COMMUNICATOR_CONNECTION_SETTINGS_H_
