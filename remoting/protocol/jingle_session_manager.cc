// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/jingle_session_manager.h"

#include <limits>

#include "base/base64.h"
#include "base/bind.h"
#include "base/message_loop_proxy.h"
#include "base/string_util.h"
#include "remoting/base/constants.h"
#include "remoting/jingle_glue/jingle_info_request.h"
#include "remoting/jingle_glue/jingle_signaling_connector.h"
#include "remoting/jingle_glue/signal_strategy.h"
#include "remoting/protocol/authenticator.h"
#include "third_party/libjingle/source/talk/base/basicpacketsocketfactory.h"
#include "third_party/libjingle/source/talk/p2p/base/constants.h"
#include "third_party/libjingle/source/talk/p2p/base/sessionmanager.h"
#include "third_party/libjingle/source/talk/p2p/base/transport.h"
#include "third_party/libjingle/source/talk/p2p/client/httpportallocator.h"
#include "third_party/libjingle/source/talk/xmllite/xmlelement.h"

using buzz::XmlElement;

namespace remoting {
namespace protocol {

JingleSessionManager::JingleSessionManager(
    base::MessageLoopProxy* message_loop)
    : message_loop_(message_loop),
      signal_strategy_(NULL),
      allow_nat_traversal_(false),
      ready_(false),
      http_port_allocator_(NULL),
      closed_(false) {
}

JingleSessionManager::~JingleSessionManager() {
  // Session manager can be destroyed only after all sessions are destroyed.
  DCHECK(sessions_.empty());
  Close();
}

void JingleSessionManager::Init(
    SignalStrategy* signal_strategy,
    SessionManager::Listener* listener,
    const NetworkSettings& network_settings) {
  DCHECK(CalledOnValidThread());

  DCHECK(signal_strategy);
  DCHECK(listener);

  signal_strategy_ = signal_strategy;
  listener_ = listener;
  allow_nat_traversal_ = network_settings.allow_nat_traversal;

  signal_strategy_->AddListener(this);

  if (!network_manager_.get()) {
    VLOG(1) << "Creating talk_base::NetworkManager.";
    network_manager_.reset(new talk_base::BasicNetworkManager());
  }
  if (!socket_factory_.get()) {
    VLOG(1) << "Creating talk_base::BasicPacketSocketFactory.";
    socket_factory_.reset(new talk_base::BasicPacketSocketFactory(
        talk_base::Thread::Current()));
  }

  // Initialize |port_allocator_|.

  // We always use PseudoTcp to provide a reliable channel. However
  // when it is used together with TCP the performance is very bad
  // so we explicitly disables TCP connections.
  int port_allocator_flags = cricket::PORTALLOCATOR_DISABLE_TCP;

  if (allow_nat_traversal_) {
    http_port_allocator_ = new cricket::HttpPortAllocator(
        network_manager_.get(), socket_factory_.get(), "transp2");
    port_allocator_.reset(http_port_allocator_);
  } else {
    port_allocator_flags |= cricket::PORTALLOCATOR_DISABLE_STUN |
        cricket::PORTALLOCATOR_DISABLE_RELAY;
    port_allocator_.reset(
        new cricket::BasicPortAllocator(
            network_manager_.get(), socket_factory_.get()));
  }
  port_allocator_->set_flags(port_allocator_flags);

  port_allocator_->SetPortRange(
      network_settings.min_port, network_settings.max_port);

  // Initialize |cricket_session_manager_|.
  cricket_session_manager_.reset(
      new cricket::SessionManager(port_allocator_.get()));
  cricket_session_manager_->AddClient(kChromotingXmlNamespace, this);

  jingle_signaling_connector_.reset(new JingleSignalingConnector(
      signal_strategy_, cricket_session_manager_.get()));

  OnSignalStrategyStateChange(signal_strategy_->GetState());
}

void JingleSessionManager::Close() {
  DCHECK(CalledOnValidThread());

  if (!closed_) {
    jingle_info_request_.reset();
    cricket_session_manager_->RemoveClient(kChromotingXmlNamespace);
    jingle_signaling_connector_.reset();
    signal_strategy_->RemoveListener(this);
    closed_ = true;
  }
}

void JingleSessionManager::set_authenticator_factory(
    scoped_ptr<AuthenticatorFactory> authenticator_factory) {
  DCHECK(CalledOnValidThread());
  DCHECK(authenticator_factory.get());
  DCHECK(!authenticator_factory_.get());
  authenticator_factory_ = authenticator_factory.Pass();
}

scoped_ptr<Session> JingleSessionManager::Connect(
    const std::string& host_jid,
    scoped_ptr<Authenticator> authenticator,
    scoped_ptr<CandidateSessionConfig> config,
    const Session::StateChangeCallback& state_change_callback) {
  DCHECK(CalledOnValidThread());

  cricket::Session* cricket_session = cricket_session_manager_->CreateSession(
      signal_strategy_->GetLocalJid(), kChromotingXmlNamespace);
  cricket_session->set_remote_name(host_jid);

  scoped_ptr<JingleSession> jingle_session(
      new JingleSession(this, cricket_session, authenticator.Pass()));
  jingle_session->set_candidate_config(config.Pass());
  jingle_session->SetStateChangeCallback(state_change_callback);
  sessions_.push_back(jingle_session.get());

  jingle_session->SendSessionInitiate();

  return jingle_session.PassAs<Session>();
}

void JingleSessionManager::OnSessionCreate(
    cricket::Session* cricket_session, bool incoming) {
  DCHECK(CalledOnValidThread());

  // Allow local connections.
  cricket_session->set_allow_local_ips(true);

  if (incoming) {
    JingleSession* jingle_session =
        new JingleSession(this, cricket_session, scoped_ptr<Authenticator>());
    sessions_.push_back(jingle_session);
  }
}

void JingleSessionManager::OnSessionDestroy(cricket::Session* cricket_session) {
  DCHECK(CalledOnValidThread());

  std::list<JingleSession*>::iterator it;
  for (it = sessions_.begin(); it != sessions_.end(); ++it) {
    if ((*it)->HasSession(cricket_session)) {
      (*it)->ReleaseSession();
      return;
    }
  }
}

void JingleSessionManager::OnSignalStrategyStateChange(
    SignalStrategy::State state) {
  if (state == SignalStrategy::CONNECTED) {
    // If NAT traversal is enabled then we need to request STUN/Relay info.
    if (allow_nat_traversal_) {
      jingle_info_request_.reset(new JingleInfoRequest(signal_strategy_));
      jingle_info_request_->Send(base::Bind(
          &JingleSessionManager::OnJingleInfo, base::Unretained(this)));
    } else if (!ready_) {
      ready_ = true;
      listener_->OnSessionManagerReady();
    }
  }
}

SessionManager::IncomingSessionResponse JingleSessionManager::AcceptConnection(
    JingleSession* jingle_session) {
  DCHECK(CalledOnValidThread());

  // Reject connection if we are closed.
  if (closed_)
    return SessionManager::DECLINE;

  IncomingSessionResponse response = SessionManager::DECLINE;
  listener_->OnIncomingSession(jingle_session, &response);
  return response;
}

scoped_ptr<Authenticator> JingleSessionManager::CreateAuthenticator(
    const std::string& jid, const buzz::XmlElement* auth_message) {
  DCHECK(CalledOnValidThread());

  if (!authenticator_factory_.get())
    return scoped_ptr<Authenticator>(NULL);
  return authenticator_factory_->CreateAuthenticator(jid, auth_message);
}

void JingleSessionManager::SessionDestroyed(JingleSession* jingle_session) {
  std::list<JingleSession*>::iterator it =
      std::find(sessions_.begin(), sessions_.end(), jingle_session);
  CHECK(it != sessions_.end());
  cricket::Session* cricket_session = jingle_session->ReleaseSession();
  cricket_session_manager_->DestroySession(cricket_session);
  sessions_.erase(it);
}

void JingleSessionManager::OnJingleInfo(
    const std::string& token,
    const std::vector<std::string>& relay_hosts,
    const std::vector<talk_base::SocketAddress>& stun_hosts) {
  DCHECK(CalledOnValidThread());

  if (http_port_allocator_) {
    // TODO(ajwong): Avoid string processing if log-level is low.
    std::string stun_servers;
    for (size_t i = 0; i < stun_hosts.size(); ++i) {
      stun_servers += stun_hosts[i].ToString() + "; ";
    }
    VLOG(1) << "Configuring with relay token: " << token
            << ", relays: " << JoinString(relay_hosts, ';')
            << ", stun: " << stun_servers;
    http_port_allocator_->SetRelayToken(token);
    http_port_allocator_->SetStunHosts(stun_hosts);
    http_port_allocator_->SetRelayHosts(relay_hosts);
  } else {
    LOG(WARNING) << "Jingle info found but no port allocator.";
  }

  if (!ready_) {
    ready_ = true;
    listener_->OnSessionManagerReady();
  }
}

// Parse content description generated by WriteContent().
bool JingleSessionManager::ParseContent(
    cricket::SignalingProtocol protocol,
    const XmlElement* element,
    const cricket::ContentDescription** content,
    cricket::ParseError* error) {
  *content = ContentDescription::ParseXml(element);
  return *content != NULL;
}

bool JingleSessionManager::WriteContent(
    cricket::SignalingProtocol protocol,
    const cricket::ContentDescription* content,
    XmlElement** elem,
    cricket::WriteError* error) {
  const ContentDescription* desc =
      static_cast<const ContentDescription*>(content);

  *elem = desc->ToXml();
  return true;
}

}  // namespace protocol
}  // namespace remoting
