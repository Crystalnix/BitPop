// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_CHROMOTING_HOST_H_
#define REMOTING_HOST_CHROMOTING_HOST_H_

#include <string>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/threading/thread.h"
#include "remoting/base/encoder.h"
#include "remoting/host/capturer.h"
#include "remoting/host/client_session.h"
#include "remoting/host/desktop_environment.h"
#include "remoting/host/host_key_pair.h"
#include "remoting/host/host_status_observer.h"
#include "remoting/host/ui_strings.h"
#include "remoting/jingle_glue/jingle_thread.h"
#include "remoting/jingle_glue/signal_strategy.h"
#include "remoting/protocol/authenticator.h"
#include "remoting/protocol/session_manager.h"
#include "remoting/protocol/connection_to_client.h"

namespace remoting {

namespace protocol {
class InputStub;
class SessionConfig;
class CandidateSessionConfig;
}  // namespace protocol

class Capturer;
class ChromotingHostContext;
class DesktopEnvironment;
class Encoder;
class ScreenRecorder;

// A class to implement the functionality of a host process.
//
// Here's the work flow of this class:
// 1. We should load the saved GAIA ID token or if this is the first
//    time the host process runs we should prompt user for the
//    credential. We will use this token or credentials to authenicate
//    and register the host.
//
// 2. We listen for incoming connection using libjingle. We will create
//    a ConnectionToClient object that wraps around linjingle for transport.
//    A ScreenRecorder is created with an Encoder and a Capturer.
//    A ConnectionToClient is added to the ScreenRecorder for transporting
//    the screen captures. An InputStub is created and registered with the
//    ConnectionToClient to receive mouse / keyboard events from the remote
//    client.
//    After we have done all the initialization we'll start the ScreenRecorder.
//    We'll then enter the running state of the host process.
//
// 3. When the user is disconnected, we will pause the ScreenRecorder
//    and try to terminate the threads we have created. This will allow
//    all pending tasks to complete. After all of that completed we
//    return to the idle state. We then go to step (2) if there a new
//    incoming connection.
class ChromotingHost : public base::RefCountedThreadSafe<ChromotingHost>,
                       public ClientSession::EventHandler,
                       public protocol::SessionManager::Listener {
 public:
  // The caller must ensure that |context|, |signal_strategy| and
  // |environment| out-live the host.
  ChromotingHost(ChromotingHostContext* context,
                 SignalStrategy* signal_strategy,
                 DesktopEnvironment* environment,
                 const protocol::NetworkSettings& network_settings);

  // Asynchronously start the host process.
  //
  // After this is invoked, the host process will connect to the talk
  // network and start listening for incoming connections.
  //
  // This method can only be called once during the lifetime of this object.
  void Start();

  // Asynchronously shutdown the host process. |shutdown_task| is
  // called after shutdown is completed.
  void Shutdown(const base::Closure& shutdown_task);

  // Add/Remove |observer| to/from the list of status observers. Both
  // methods can be called on the network thread only.
  void AddStatusObserver(HostStatusObserver* observer);
  void RemoveStatusObserver(HostStatusObserver* observer);

  // This method may be called only form
  // HostStatusObserver::OnClientAuthenticated() to reject the new
  // client.
  void RejectAuthenticatingClient();

  // Sets the authenticator factory to use for incoming
  // connections. Incoming connections are rejected until
  // authenticator factory is set. Must be called on the network
  // thread after the host is started. Must not be called more than
  // once per host instance because it may not be safe to delete
  // factory before all authenticators it created are deleted.
  void SetAuthenticatorFactory(
      scoped_ptr<protocol::AuthenticatorFactory> authenticator_factory);

  ////////////////////////////////////////////////////////////////////////////
  // ClientSession::EventHandler implementation.
  virtual void OnSessionAuthenticated(ClientSession* client) OVERRIDE;
  virtual void OnSessionAuthenticationFailed(ClientSession* client) OVERRIDE;
  virtual void OnSessionClosed(ClientSession* session) OVERRIDE;
  virtual void OnSessionSequenceNumber(ClientSession* session,
                                       int64 sequence_number) OVERRIDE;
  virtual void OnSessionIpAddress(ClientSession* session,
                                  const std::string& channel_name,
                                  const net::IPEndPoint& end_point) OVERRIDE;

  // SessionManager::Listener implementation.
  virtual void OnSessionManagerReady() OVERRIDE;
  virtual void OnIncomingSession(
      protocol::Session* session,
      protocol::SessionManager::IncomingSessionResponse* response) OVERRIDE;

  // Sets desired configuration for the protocol. Ownership of the
  // |config| is transferred to the object. Must be called before Start().
  void set_protocol_config(protocol::CandidateSessionConfig* config);

  // Notify all active client sessions that local input has been detected, and
  // that remote input should be ignored for a short time.
  void LocalMouseMoved(const SkIPoint& new_pos);

  // Pause or unpause the session. While the session is paused, remote input
  // is ignored.
  void PauseSession(bool pause);

  const UiStrings& ui_strings() { return ui_strings_; }

  // Set localized strings. Must be called before host is started.
  void SetUiStrings(const UiStrings& ui_strings);

 private:
  friend class base::RefCountedThreadSafe<ChromotingHost>;
  friend class ChromotingHostTest;

  typedef std::vector<ClientSession*> ClientList;

  enum State {
    kInitial,
    kStarted,
    kStopping,
    kStopped,
  };

  // Creates encoder for the specified configuration.
  static Encoder* CreateEncoder(const protocol::SessionConfig& config);

  virtual ~ChromotingHost();

  std::string GenerateHostAuthToken(const std::string& encoded_client_token);

  void AddAuthenticatedClient(ClientSession* client,
                              const protocol::SessionConfig& config,
                              const std::string& jid);

  void StopScreenRecorder();
  void OnScreenRecorderStopped();

  // Called from Shutdown() or OnScreenRecorderStopped() to finish shutdown.
  void ShutdownFinish();

  // Unless specified otherwise all members of this class must be
  // used on the network thread only.

  // Parameters specified when the host was created.
  ChromotingHostContext* context_;
  DesktopEnvironment* desktop_environment_;
  protocol::NetworkSettings network_settings_;

  // TODO(lambroslambrou): The following is a temporary fix for Me2Me
  // (crbug.com/105995), pending the AuthenticatorFactory work.
  // Cache the shared secret, in case SetSharedSecret() is called before the
  // session manager has been created.
  // The |have_shared_secret_| flag is to distinguish SetSharedSecret() not
  // being called at all, from being called with an empty string.
  std::string shared_secret_;
  bool have_shared_secret_;

  // Connection objects.
  SignalStrategy* signal_strategy_;
  scoped_ptr<protocol::SessionManager> session_manager_;

  // Must be used on the network thread only.
  ObserverList<HostStatusObserver> status_observers_;

  // The connections to remote clients.
  ClientList clients_;

  // Session manager for the host process.
  // TODO(sergeyu): Do we need to have one screen recorder per client?
  scoped_refptr<ScreenRecorder> recorder_;

  // Number of screen recorders that are currently being
  // stopped. Normally set to 0 or 1, but in some cases it may be
  // greater than 1, particularly if when second client can connect
  // immediately after previous one disconnected.
  int stopping_recorders_;

  // Tracks the internal state of the host.
  State state_;

  // Configuration of the protocol.
  scoped_ptr<protocol::CandidateSessionConfig> protocol_config_;

  // Flags used for RejectAuthenticatingClient().
  bool authenticating_client_;
  bool reject_authenticating_client_;

  // Stores list of tasks that should be executed when we finish
  // shutdown. Used only while |state_| is set to kStopping.
  std::vector<base::Closure> shutdown_tasks_;

  // TODO(sergeyu): The following members do not belong to
  // ChromotingHost and should be moved elsewhere.
  UiStrings ui_strings_;

  DISALLOW_COPY_AND_ASSIGN(ChromotingHost);
};

}  // namespace remoting

#endif  // REMOTING_HOST_CHROMOTING_HOST_H_
