// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_JINGLE_SESSION_H_
#define REMOTING_PROTOCOL_JINGLE_SESSION_H_

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "net/base/completion_callback.h"
#include "remoting/protocol/authenticator.h"
#include "remoting/protocol/session.h"
#include "third_party/libjingle/source/talk/base/sigslot.h"
#include "third_party/libjingle/source/talk/p2p/base/session.h"

namespace remoting {
namespace protocol {

class JingleChannelConnector;
class JingleSessionManager;

// Implements protocol::Session that work over libjingle session (the
// cricket::Session object is passed to Init() method). Created
// by JingleSessionManager for incoming and outgoing connections.
class JingleSession : public protocol::Session,
                      public sigslot::has_slots<> {
 public:
  virtual ~JingleSession();

  // Session interface.
  virtual void SetStateChangeCallback(
      const StateChangeCallback& callback) OVERRIDE;
  virtual void SetRouteChangeCallback(
      const RouteChangeCallback& callback) OVERRIDE;
  virtual Error error() OVERRIDE;
  virtual void CreateStreamChannel(
      const std::string& name,
      const StreamChannelCallback& callback) OVERRIDE;
  virtual void CreateDatagramChannel(
      const std::string& name,
      const DatagramChannelCallback& callback) OVERRIDE;
  virtual void CancelChannelCreation(const std::string& name) OVERRIDE;
  virtual const std::string& jid() OVERRIDE;
  virtual const CandidateSessionConfig* candidate_config() OVERRIDE;
  virtual const SessionConfig& config() OVERRIDE;
  virtual void set_config(const SessionConfig& config) OVERRIDE;
  virtual void Close() OVERRIDE;

 private:
  friend class JingleDatagramConnector;
  friend class JingleSessionManager;
  friend class JingleStreamConnector;

  typedef std::map<std::string, JingleChannelConnector*> ChannelConnectorsMap;

  JingleSession(JingleSessionManager* jingle_session_manager,
                cricket::Session* cricket_session,
                scoped_ptr<Authenticator> authenticator);

  // Called by JingleSessionManager.
  void set_candidate_config(
      scoped_ptr<CandidateSessionConfig> candidate_config);

  // Sends session-initiate for new session.
  void SendSessionInitiate();

  // Close all the channels and terminate the session. |result|
  // defines error code that should returned to currently opened
  // channels. |error| specified new value for error().
  void CloseInternal(int result, Error error);

  bool HasSession(cricket::Session* cricket_session);
  cricket::Session* ReleaseSession();

  // Initialize the session configuration from a received connection response
  // stanza.
  bool InitializeConfigFromDescription(
      const cricket::SessionDescription* description);

  // Handlers for |cricket_session_| signals.
  void OnSessionState(cricket::BaseSession* session,
                      cricket::BaseSession::State state);
  void OnSessionError(cricket::BaseSession* session,
                      cricket::BaseSession::Error error);
  void OnSessionInfoMessage(cricket::Session* session,
                            const buzz::XmlElement* message);
  void OnTerminateReason(cricket::Session* session,
                         const std::string& reason);

  void OnInitiate();
  void OnAccept();
  void OnTerminate();

  // Notifies upper layer about incoming connection and
  // accepts/rejects connection.
  void AcceptConnection();

  void ProcessAuthenticationStep();

  void AddChannelConnector(const std::string& name,
                           JingleChannelConnector* connector);

  // Called by JingleChannelConnector when it has finished connecting
  // the channel, to remove itself from the table of pending connectors.  The
  // connector assumes responsibility for destroying itself after this call.
  void OnChannelConnectorFinished(const std::string& name,
                                  JingleChannelConnector* connector);

  void OnRouteChange(cricket::TransportChannel* channel,
                     const cricket::Candidate& candidate);

  const cricket::ContentInfo* GetContentInfo() const;

  void SetState(State new_state);

  static Error RejectionReasonToError(Authenticator::RejectionReason reason);

  static scoped_ptr<cricket::SessionDescription> CreateSessionDescription(
      scoped_ptr<CandidateSessionConfig> candidate_config,
      scoped_ptr<buzz::XmlElement> authenticator_message);

  // JingleSessionManager that created this session. Guaranteed to
  // exist throughout the lifetime of the session.
  JingleSessionManager* jingle_session_manager_;

  scoped_ptr<Authenticator> authenticator_;

  State state_;
  StateChangeCallback state_change_callback_;
  RouteChangeCallback route_change_callback_;

  Error error_;

  bool closing_;

  // JID of the other side. Set when the connection is initialized,
  // and never changed after that.
  std::string jid_;

  // The corresponding libjingle session.
  cricket::Session* cricket_session_;

  SessionConfig config_;
  bool config_set_;

  // These data members are only set on the receiving side.
  scoped_ptr<const CandidateSessionConfig> candidate_config_;

  // Channels that are currently being connected.
  ChannelConnectorsMap channel_connectors_;

  // Termination reason. Needs to be stored because
  // SignalReceivedTerminateReason handler is not allowed to destroy
  // the object.
  std::string terminate_reason_;

  base::WeakPtrFactory<JingleSession> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(JingleSession);
};

}  // namespace protocol

}  // namespace remoting

#endif  // REMOTING_PROTOCOL_JINGLE_SESSION_H_
