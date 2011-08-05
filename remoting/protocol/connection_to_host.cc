// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/connection_to_host.h"

#include "base/callback.h"
#include "base/message_loop.h"
#include "remoting/base/constants.h"
#include "remoting/jingle_glue/http_port_allocator.h"
#include "remoting/jingle_glue/jingle_thread.h"
#include "remoting/protocol/auth_token_utils.h"
#include "remoting/protocol/client_message_dispatcher.h"
#include "remoting/protocol/client_stub.h"
#include "remoting/protocol/host_control_sender.h"
#include "remoting/protocol/input_sender.h"
#include "remoting/protocol/jingle_session_manager.h"
#include "remoting/protocol/video_reader.h"
#include "remoting/protocol/video_stub.h"
#include "remoting/protocol/util.h"

namespace remoting {
namespace protocol {

ConnectionToHost::ConnectionToHost(
    JingleThread* thread,
    talk_base::NetworkManager* network_manager,
    talk_base::PacketSocketFactory* socket_factory,
    PortAllocatorSessionFactory* session_factory)
    : state_(STATE_EMPTY),
      thread_(thread),
      network_manager_(network_manager),
      socket_factory_(socket_factory),
      port_allocator_session_factory_(session_factory),
      event_callback_(NULL),
      dispatcher_(new ClientMessageDispatcher()),
      client_stub_(NULL),
      video_stub_(NULL) {
}

ConnectionToHost::~ConnectionToHost() {
}

InputStub* ConnectionToHost::input_stub() {
  return input_stub_.get();
}

HostStub* ConnectionToHost::host_stub() {
  return host_stub_.get();
}

MessageLoop* ConnectionToHost::message_loop() {
  return thread_->message_loop();
}

void ConnectionToHost::Connect(const std::string& username,
                               const std::string& auth_token,
                               const std::string& auth_service,
                               const std::string& host_jid,
                               const std::string& access_code,
                               HostEventCallback* event_callback,
                               ClientStub* client_stub,
                               VideoStub* video_stub) {
  event_callback_ = event_callback;
  client_stub_ = client_stub;
  video_stub_ = video_stub;
  access_code_ = access_code;

  // Initialize |jingle_client_|.
  signal_strategy_.reset(
      new XmppSignalStrategy(thread_, username, auth_token, auth_service));
  jingle_client_ =
      new JingleClient(thread_, signal_strategy_.get(),
                       network_manager_.release(), socket_factory_.release(),
                       port_allocator_session_factory_.release(), this);
  jingle_client_->Init();

  // Save jid of the host. The actual connection is created later after
  // |jingle_client_| is connected.
  host_jid_ = host_jid;
}

void ConnectionToHost::ConnectSandboxed(scoped_refptr<XmppProxy> xmpp_proxy,
                                        const std::string& your_jid,
                                        const std::string& host_jid,
                                        const std::string& access_code,
                                        HostEventCallback* event_callback,
                                        ClientStub* client_stub,
                                        VideoStub* video_stub) {
  event_callback_ = event_callback;
  client_stub_ = client_stub;
  video_stub_ = video_stub;
  access_code_ = access_code;

  // Initialize |jingle_client_|.
  JavascriptSignalStrategy* strategy = new JavascriptSignalStrategy(your_jid);
  strategy->AttachXmppProxy(xmpp_proxy);
  signal_strategy_.reset(strategy);
  jingle_client_ =
      new JingleClient(thread_, signal_strategy_.get(),
                       network_manager_.release(), socket_factory_.release(),
                       port_allocator_session_factory_.release(), this);
  jingle_client_->Init();

  // Save jid of the host. The actual connection is created later after
  // |jingle_client_| is connected.
  host_jid_ = host_jid;
}

void ConnectionToHost::Disconnect() {
  if (MessageLoop::current() != message_loop()) {
    message_loop()->PostTask(
        FROM_HERE, NewRunnableMethod(this, &ConnectionToHost::Disconnect));
    return;
  }

  if (session_) {
    session_->Close(
        NewRunnableMethod(this, &ConnectionToHost::OnDisconnected));
  } else {
    OnDisconnected();
  }
}

void ConnectionToHost::InitSession() {
  DCHECK_EQ(message_loop(), MessageLoop::current());

  std::string jid = jingle_client_->GetFullJid();

  // Initialize chromotocol |session_manager_|.
  JingleSessionManager* session_manager =
      new JingleSessionManager(thread_);
  // TODO(ajwong): Make this a command switch when we're more stable.
  session_manager->set_allow_local_ips(true);
  session_manager->Init(
      jid, jingle_client_->session_manager(),
      NewCallback(this, &ConnectionToHost::OnNewSession),
      NULL, NULL);
  session_manager_ = session_manager;

  CandidateSessionConfig* candidate_config =
      CandidateSessionConfig::CreateDefault();

  std::string client_token =
      protocol::GenerateSupportAuthToken(jid, access_code_);

  // Initialize |session_|.
  session_ = session_manager_->Connect(
      host_jid_, client_token, candidate_config,
      NewCallback(this, &ConnectionToHost::OnSessionStateChange));
}

void ConnectionToHost::OnDisconnected() {
  session_ = NULL;

  if (session_manager_) {
    session_manager_->Close(
        NewRunnableMethod(this, &ConnectionToHost::OnServerClosed));
  } else {
    OnServerClosed();
  }
}

void ConnectionToHost::OnServerClosed() {
  session_manager_ = NULL;
  if (jingle_client_) {
    jingle_client_->Close();
    jingle_client_ = NULL;
  }
}

const SessionConfig* ConnectionToHost::config() {
  return session_->config();
}

// JingleClient::Callback interface.
void ConnectionToHost::OnStateChange(JingleClient* client,
                                     JingleClient::State state) {
  DCHECK_EQ(message_loop(), MessageLoop::current());
  DCHECK(client);
  DCHECK(event_callback_);

  if (state == JingleClient::CONNECTED) {
    VLOG(1) << "Connected as: " << client->GetFullJid();
    InitSession();
  } else if (state == JingleClient::CLOSED) {
    VLOG(1) << "Connection closed.";
    event_callback_->OnConnectionClosed(this);
  }
}

void ConnectionToHost::OnNewSession(Session* session,
    SessionManager::IncomingSessionResponse* response) {
  DCHECK_EQ(message_loop(), MessageLoop::current());
  // Client always rejects incoming sessions.
  *response = SessionManager::DECLINE;
}

void ConnectionToHost::OnSessionStateChange(
    Session::State state) {
  DCHECK_EQ(message_loop(), MessageLoop::current());
  DCHECK(event_callback_);

  switch (state) {
    case Session::FAILED:
      state_ = STATE_FAILED;
      event_callback_->OnConnectionFailed(this);
      break;

    case Session::CLOSED:
      state_ = STATE_CLOSED;
      event_callback_->OnConnectionClosed(this);
      break;

    case Session::CONNECTED:
      state_ = STATE_CONNECTED;
      // Initialize reader and writer.
      video_reader_.reset(VideoReader::Create(session_->config()));
      video_reader_->Init(session_, video_stub_);
      host_stub_.reset(new HostControlSender(session_->control_channel()));
      dispatcher_->Initialize(session_.get(), client_stub_);
      event_callback_->OnConnectionOpened(this);
      break;

    default:
      // Ignore the other states by default.
      break;
  }
}

void ConnectionToHost::OnClientAuthenticated() {
  // TODO(hclam): Don't send anything except authentication request if it is
  // not authenticated.
  state_ = STATE_AUTHENTICATED;

  // Create and enable the input stub now that we're authenticated.
  input_stub_.reset(new InputSender(session_->event_channel()));
}

ConnectionToHost::State ConnectionToHost::state() const {
  return state_;
}

}  // namespace protocol
}  // namespace remoting
