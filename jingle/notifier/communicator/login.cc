// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jingle/notifier/communicator/login.h"

#include <string>

#include "base/logging.h"
#include "base/rand_util.h"
#include "base/time.h"
#include "net/base/host_port_pair.h"
#include "talk/base/common.h"
#include "talk/base/firewallsocketserver.h"
#include "talk/base/logging.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/taskrunner.h"
#include "talk/xmllite/xmlelement.h"
#include "talk/xmpp/asyncsocket.h"
#include "talk/xmpp/prexmppauth.h"
#include "talk/xmpp/xmppclient.h"
#include "talk/xmpp/xmppclientsettings.h"
#include "talk/xmpp/xmppengine.h"

namespace notifier {

// Redirect valid for 5 minutes.
static const int kRedirectTimeoutMinutes = 5;

Login::Delegate::~Delegate() {}

Login::Login(Delegate* delegate,
             const buzz::XmppClientSettings& user_settings,
             const scoped_refptr<net::URLRequestContextGetter>&
                request_context_getter,
             const ServerList& servers,
             bool try_ssltcp_first,
             const std::string& auth_mechanism)
    : delegate_(delegate),
      login_settings_(user_settings,
                      request_context_getter,
                      servers,
                      try_ssltcp_first,
                      auth_mechanism) {
  net::NetworkChangeNotifier::AddIPAddressObserver(this);
  net::NetworkChangeNotifier::AddConnectionTypeObserver(this);
  // TODO(akalin): Add as DNSObserver once bug 130610 is fixed.
  ResetReconnectState();
}

Login::~Login() {
  net::NetworkChangeNotifier::RemoveConnectionTypeObserver(this);
  net::NetworkChangeNotifier::RemoveIPAddressObserver(this);
}

void Login::StartConnection() {
  DVLOG(1) << "Starting connection...";
  single_attempt_.reset(new SingleLoginAttempt(login_settings_, this));
}

void Login::UpdateXmppSettings(const buzz::XmppClientSettings& user_settings) {
  login_settings_.set_user_settings(user_settings);
}

// In the code below, we assume that calling a delegate method may end
// up in ourselves being deleted, so we always call it last.
//
// TODO(akalin): Add unit tests to enforce the behavior above.

void Login::OnConnect(base::WeakPtr<buzz::XmppTaskParentInterface> base_task) {
  ResetReconnectState();
  delegate_->OnConnect(base_task);
}

void Login::OnRedirect(const ServerInformation& redirect_server) {
  login_settings_.SetRedirectServer(redirect_server);
  // Drop the current connection, and start the login process again.
  StartConnection();
  delegate_->OnTransientDisconnection();
}

void Login::OnCredentialsRejected() {
  TryReconnect();
  delegate_->OnCredentialsRejected();
}

void Login::OnSettingsExhausted() {
  TryReconnect();
  delegate_->OnTransientDisconnection();
}

void Login::OnIPAddressChanged() {
  DVLOG(1) << "Detected IP address change";
  OnNetworkEvent();
}

void Login::OnConnectionTypeChanged(
    net::NetworkChangeNotifier::ConnectionType type) {
  DVLOG(1) << "Detected connection type change";
  OnNetworkEvent();
}

void Login::OnDNSChanged(unsigned detail) {
  DVLOG(1) << "Detected DNS change";
  OnNetworkEvent();
}

void Login::OnNetworkEvent() {
  // Reconnect in 1 to 9 seconds (vary the time a little to try to
  // avoid spikey behavior on network hiccups).
  reconnect_interval_ = base::TimeDelta::FromSeconds(base::RandInt(1, 9));
  TryReconnect();
  delegate_->OnTransientDisconnection();
}

void Login::ResetReconnectState() {
  reconnect_interval_ =
      base::TimeDelta::FromSeconds(base::RandInt(5, 25));
  reconnect_timer_.Stop();
}

void Login::TryReconnect() {
  DCHECK_GT(reconnect_interval_.InSeconds(), 0);
  single_attempt_.reset();
  reconnect_timer_.Stop();
  DVLOG(1) << "Reconnecting in "
           << reconnect_interval_.InSeconds() << " seconds";
  reconnect_timer_.Start(
      FROM_HERE, reconnect_interval_, this, &Login::DoReconnect);
}

void Login::DoReconnect() {
  // Double reconnect time up to 30 minutes.
  const base::TimeDelta kMaxReconnectInterval =
      base::TimeDelta::FromMinutes(30);
  reconnect_interval_ *= 2;
  if (reconnect_interval_ > kMaxReconnectInterval)
    reconnect_interval_ = kMaxReconnectInterval;
  DVLOG(1) << "Reconnecting...";
  StartConnection();
}

}  // namespace notifier
