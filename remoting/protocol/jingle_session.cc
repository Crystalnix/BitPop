// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/jingle_session.h"

#include "base/bind.h"
#include "base/rand_util.h"
#include "base/stl_util.h"
#include "base/string_number_conversions.h"
#include "base/time.h"
#include "remoting/base/constants.h"
#include "remoting/jingle_glue/iq_sender.h"
#include "remoting/protocol/authenticator.h"
#include "remoting/protocol/channel_authenticator.h"
#include "remoting/protocol/content_description.h"
#include "remoting/protocol/jingle_messages.h"
#include "remoting/protocol/jingle_session_manager.h"
#include "remoting/protocol/session_config.h"
#include "third_party/libjingle/source/talk/p2p/base/candidate.h"
#include "third_party/libjingle/source/talk/xmllite/xmlelement.h"

using buzz::XmlElement;

namespace remoting {
namespace protocol {

namespace {
// Delay after candidate creation before sending transport-info
// message. This is neccessary to be able to pack multiple candidates
// into one transport-info messages. The value needs to be greater
// than zero because ports are opened asynchronously in the browser
// process.
const int kTransportInfoSendDelayMs = 2;

// How long we should wait for a response from the other end. This
// value is used for all requests include |session-initiate| and
// |transport-info|.
const int kMessageResponseTimeoutSeconds = 10;

ErrorCode AuthRejectionReasonToErrorCode(
    Authenticator::RejectionReason reason) {
  switch (reason) {
    case Authenticator::INVALID_CREDENTIALS:
      return AUTHENTICATION_FAILED;
    case Authenticator::PROTOCOL_ERROR:
      return INCOMPATIBLE_PROTOCOL;
  }
  NOTREACHED();
  return UNKNOWN_ERROR;
}

}  // namespace

JingleSession::JingleSession(JingleSessionManager* session_manager)
    : session_manager_(session_manager),
      event_handler_(NULL),
      state_(INITIALIZING),
      error_(OK),
      config_is_set_(false) {
}

JingleSession::~JingleSession() {
  STLDeleteContainerPointers(pending_requests_.begin(),
                             pending_requests_.end());
  STLDeleteContainerPairSecondPointers(channels_.begin(), channels_.end());
  session_manager_->SessionDestroyed(this);
}

void JingleSession::SetEventHandler(Session::EventHandler* event_handler) {
  DCHECK(CalledOnValidThread());
  DCHECK(event_handler);
  event_handler_ = event_handler;
}

ErrorCode JingleSession::error() {
  DCHECK(CalledOnValidThread());
  return error_;
}

void JingleSession::StartConnection(
    const std::string& peer_jid,
    scoped_ptr<Authenticator> authenticator,
    scoped_ptr<CandidateSessionConfig> config) {
  DCHECK(CalledOnValidThread());
  DCHECK(authenticator.get());
  DCHECK_EQ(authenticator->state(), Authenticator::MESSAGE_READY);

  peer_jid_ = peer_jid;
  authenticator_ = authenticator.Pass();
  candidate_config_ = config.Pass();

  // Generate random session ID. There are usually not more than 1
  // concurrent session per host, so a random 64-bit integer provides
  // enough entropy. In the worst case connection will fail when two
  // clients generate the same session ID concurrently.
  session_id_ = base::Int64ToString(base::RandGenerator(kint64max));

  // Send session-initiate message.
  JingleMessage message(peer_jid_, JingleMessage::SESSION_INITIATE,
                        session_id_);
  message.from = session_manager_->signal_strategy_->GetLocalJid();
  message.description.reset(
      new ContentDescription(candidate_config_->Clone(),
                             authenticator_->GetNextMessage()));
  SendMessage(message);

  SetState(CONNECTING);
}

void JingleSession::InitializeIncomingConnection(
    const JingleMessage& initiate_message,
    scoped_ptr<Authenticator> authenticator) {
  DCHECK(CalledOnValidThread());
  DCHECK(initiate_message.description.get());
  DCHECK(authenticator.get());
  DCHECK_EQ(authenticator->state(), Authenticator::WAITING_MESSAGE);

  peer_jid_ = initiate_message.from;
  authenticator_ = authenticator.Pass();
  session_id_ = initiate_message.sid;
  candidate_config_ = initiate_message.description->config()->Clone();

  SetState(CONNECTING);
}

void JingleSession::AcceptIncomingConnection(
    const JingleMessage& initiate_message) {
  DCHECK(config_is_set_);

  // Process the first authentication message.
  const buzz::XmlElement* first_auth_message =
      initiate_message.description->authenticator_message();

  if (!first_auth_message) {
    CloseInternal(INCOMPATIBLE_PROTOCOL);
    return;
  }

  DCHECK_EQ(authenticator_->state(), Authenticator::WAITING_MESSAGE);
  authenticator_->ProcessMessage(first_auth_message);
  if (authenticator_->state() == Authenticator::REJECTED) {
    CloseInternal(AuthRejectionReasonToErrorCode(
        authenticator_->rejection_reason()));
    return;
  }

  // Send the session-accept message.
  JingleMessage message(peer_jid_, JingleMessage::SESSION_ACCEPT,
                        session_id_);

  scoped_ptr<buzz::XmlElement> auth_message;
  if (authenticator_->state() == Authenticator::MESSAGE_READY)
    auth_message = authenticator_->GetNextMessage();

  message.description.reset(
      new ContentDescription(CandidateSessionConfig::CreateFrom(config_),
                             auth_message.Pass()));
  SendMessage(message);

  // Update state.
  SetState(CONNECTED);

  if (authenticator_->state() == Authenticator::ACCEPTED) {
    SetState(AUTHENTICATED);
  } else {
    DCHECK_EQ(authenticator_->state(), Authenticator::WAITING_MESSAGE);
  }

  return;
}

void JingleSession::CreateStreamChannel(
      const std::string& name,
      const StreamChannelCallback& callback) {
  DCHECK(!channels_[name]);

  scoped_ptr<ChannelAuthenticator> channel_authenticator =
      authenticator_->CreateChannelAuthenticator();
  scoped_ptr<StreamTransport> channel =
      session_manager_->transport_factory_->CreateStreamTransport();
  channel->Initialize(name, this, channel_authenticator.Pass());
  channel->Connect(callback);
  channels_[name] = channel.release();
}

void JingleSession::CreateDatagramChannel(
    const std::string& name,
    const DatagramChannelCallback& callback) {
  DCHECK(!channels_[name]);

  scoped_ptr<ChannelAuthenticator> channel_authenticator =
      authenticator_->CreateChannelAuthenticator();
  scoped_ptr<DatagramTransport> channel =
      session_manager_->transport_factory_->CreateDatagramTransport();
  channel->Initialize(name, this, channel_authenticator.Pass());
  channel->Connect(callback);
  channels_[name] = channel.release();
}

void JingleSession::CancelChannelCreation(const std::string& name) {
  ChannelsMap::iterator it = channels_.find(name);
  if (it != channels_.end() && !it->second->is_connected()) {
    delete it->second;
    DCHECK(!channels_[name]);
  }
}

const std::string& JingleSession::jid() {
  DCHECK(CalledOnValidThread());
  return peer_jid_;
}

const CandidateSessionConfig* JingleSession::candidate_config() {
  DCHECK(CalledOnValidThread());
  return candidate_config_.get();
}

const SessionConfig& JingleSession::config() {
  DCHECK(CalledOnValidThread());
  return config_;
}

void JingleSession::set_config(const SessionConfig& config) {
  DCHECK(CalledOnValidThread());
  DCHECK(!config_is_set_);
  config_ = config;
  config_is_set_ = true;
}

void JingleSession::Close() {
  DCHECK(CalledOnValidThread());

  CloseInternal(OK);
}

void JingleSession::OnTransportCandidate(Transport* transport,
                                         const cricket::Candidate& candidate) {
  pending_candidates_.push_back(JingleMessage::NamedCandidate(
      transport->name(), candidate));

  if (!transport_infos_timer_.IsRunning()) {
    // Delay sending the new candidates in case we get more candidates
    // that we can send in one message.
    transport_infos_timer_.Start(
        FROM_HERE, base::TimeDelta::FromMilliseconds(kTransportInfoSendDelayMs),
        this, &JingleSession::SendTransportInfo);
  }
}

void JingleSession::OnTransportRouteChange(Transport* transport,
                                           const TransportRoute& route) {
  if (event_handler_)
    event_handler_->OnSessionRouteChange(transport->name(), route);
}

void JingleSession::OnTransportReady(Transport* transport, bool ready) {
  if (event_handler_)
    event_handler_->OnSessionChannelReady(transport->name(), ready);
}

void JingleSession::OnTransportDeleted(Transport* transport) {
  ChannelsMap::iterator it = channels_.find(transport->name());
  DCHECK_EQ(it->second, transport);
  channels_.erase(it);
}

void JingleSession::SendMessage(const JingleMessage& message) {
  scoped_ptr<IqRequest> request = session_manager_->iq_sender()->SendIq(
      message.ToXml(),
      base::Bind(&JingleSession::OnMessageResponse,
                 base::Unretained(this), message.action));
  if (request.get()) {
    request->SetTimeout(
        base::TimeDelta::FromSeconds(kMessageResponseTimeoutSeconds));
    pending_requests_.push_back(request.release());
  } else {
    LOG(ERROR) << "Failed to send a "
               << JingleMessage::GetActionName(message.action) << " message";
  }
}

void JingleSession::OnMessageResponse(
    JingleMessage::ActionType request_type,
    IqRequest* request,
    const buzz::XmlElement* response) {
  std::string type_str = JingleMessage::GetActionName(request_type);
  CleanupPendingRequests(request);

  if (!response) {
    LOG(ERROR) << type_str << " request timed out.";
    CloseInternal(SIGNALING_TIMEOUT);
  } else {
    const std::string& type = response->Attr(buzz::QName("", "type"));
    if (type != "result") {
      LOG(ERROR) << "Received error in response to " << type_str
                 << " message: \"" << response->Str()
                 << "\". Terminating the session.";

      switch (request_type) {
        case JingleMessage::SESSION_INFO:
          // session-info is used for the new authentication protocol,
          // and wasn't previously supported.
          CloseInternal(INCOMPATIBLE_PROTOCOL);
          break;

        default:
          // TODO(sergeyu): There may be different reasons for error
          // here. Parse the response stanza to find failure reason.
          CloseInternal(PEER_IS_OFFLINE);
      }
    }
  }
}

void JingleSession::CleanupPendingRequests(IqRequest* request) {
  DCHECK(!pending_requests_.empty());
  DCHECK(request);

  // This method is called whenever a response to |request| is
  // received. Here we delete that request and all requests that were
  // sent before it. The idea here is that if we send messages A, B
  // and C and then suddenly receive response to C then it means that
  // either A and B messages or the corresponding response messages
  // were somehow lost. E.g. that may happen when the client switches
  // from one network to another. The best way to handle that case is
  // to ignore errors and timeouts for A and B by deleting the
  // corresponding IqRequest objects.
  while (!pending_requests_.empty() && pending_requests_.front() != request) {
    delete pending_requests_.front();
    pending_requests_.pop_front();
  }

  // Delete the |request| itself.
  DCHECK_EQ(request, pending_requests_.front());
  delete request;
  if (!pending_requests_.empty())
    pending_requests_.pop_front();
}

void JingleSession::OnIncomingMessage(const JingleMessage& message,
                                      const ReplyCallback& reply_callback) {
  DCHECK(CalledOnValidThread());

  if (message.from != peer_jid_) {
    // Ignore messages received from a different Jid.
    reply_callback.Run(JingleMessageReply::INVALID_SID);
    return;
  }

  switch (message.action) {
    case JingleMessage::SESSION_ACCEPT:
      OnAccept(message, reply_callback);
      break;

    case JingleMessage::SESSION_INFO:
      OnSessionInfo(message, reply_callback);
      break;

    case JingleMessage::TRANSPORT_INFO:
      reply_callback.Run(JingleMessageReply::NONE);
      ProcessTransportInfo(message);
      break;

    case JingleMessage::SESSION_TERMINATE:
      OnTerminate(message, reply_callback);
      break;

    default:
      reply_callback.Run(JingleMessageReply::UNEXPECTED_REQUEST);
  }
}

void JingleSession::OnAccept(const JingleMessage& message,
                             const ReplyCallback& reply_callback) {
  if (state_ != CONNECTING) {
    reply_callback.Run(JingleMessageReply::UNEXPECTED_REQUEST);
    return;
  }

  reply_callback.Run(JingleMessageReply::NONE);

  const buzz::XmlElement* auth_message =
      message.description->authenticator_message();
  if (!auth_message) {
    DLOG(WARNING) << "Received session-accept without authentication message "
                  << auth_message->Str();
    CloseInternal(INCOMPATIBLE_PROTOCOL);
    return;
  }

  DCHECK(authenticator_->state() == Authenticator::WAITING_MESSAGE);
  authenticator_->ProcessMessage(auth_message);

  if (!InitializeConfigFromDescription(message.description.get())) {
    CloseInternal(INCOMPATIBLE_PROTOCOL);
    return;
  }

  // In case there is transport information in the accept message.
  ProcessTransportInfo(message);

  SetState(CONNECTED);

  // Process authentication.
  if (authenticator_->state() == Authenticator::ACCEPTED) {
    SetState(AUTHENTICATED);
  } else {
    ProcessAuthenticationStep();
  }
}

void JingleSession::OnSessionInfo(const JingleMessage& message,
                                  const ReplyCallback& reply_callback) {
  if (!message.info.get() ||
      !Authenticator::IsAuthenticatorMessage(message.info.get())) {
    reply_callback.Run(JingleMessageReply::UNSUPPORTED_INFO);
    return;
  }

  if (state_ != CONNECTED ||
      authenticator_->state() != Authenticator::WAITING_MESSAGE) {
    LOG(WARNING) << "Received unexpected authenticator message "
                 << message.info->Str();
    reply_callback.Run(JingleMessageReply::UNEXPECTED_REQUEST);
    CloseInternal(INCOMPATIBLE_PROTOCOL);
    return;
  }

  reply_callback.Run(JingleMessageReply::NONE);

  authenticator_->ProcessMessage(message.info.get());
  ProcessAuthenticationStep();
}

void JingleSession::ProcessTransportInfo(const JingleMessage& message) {
  for (std::list<JingleMessage::NamedCandidate>::const_iterator it =
           message.candidates.begin();
       it != message.candidates.end(); ++it) {
    ChannelsMap::iterator channel = channels_.find(it->name);
    if (channel == channels_.end()) {
      LOG(WARNING) << "Received candidate for unknown channel " << it->name;
      continue;
    }
    channel->second->AddRemoteCandidate(it->candidate);
  }
}

void JingleSession::OnTerminate(const JingleMessage& message,
                                const ReplyCallback& reply_callback) {
  if (state_ != CONNECTING && state_ != CONNECTED && state_ != AUTHENTICATED) {
    LOG(WARNING) << "Received unexpected session-terminate message.";
    reply_callback.Run(JingleMessageReply::UNEXPECTED_REQUEST);
    return;
  }

  reply_callback.Run(JingleMessageReply::NONE);

  switch (message.reason) {
    case JingleMessage::SUCCESS:
      if (state_ == CONNECTING) {
        error_ = SESSION_REJECTED;
      } else {
        error_ = OK;
      }
      break;
    case JingleMessage::DECLINE:
      error_ = AUTHENTICATION_FAILED;
      break;
    case JingleMessage::CANCEL:
      error_ = HOST_OVERLOAD;
      break;
    case JingleMessage::GENERAL_ERROR:
      error_ = CHANNEL_CONNECTION_ERROR;
      break;
    case JingleMessage::INCOMPATIBLE_PARAMETERS:
      error_ = INCOMPATIBLE_PROTOCOL;
      break;
    default:
      error_ = UNKNOWN_ERROR;
  }

  if (error_ != OK) {
    SetState(FAILED);
  } else {
    SetState(CLOSED);
  }
}

bool JingleSession::InitializeConfigFromDescription(
    const ContentDescription* description) {
  DCHECK(description);

  if (!description->config()->GetFinalConfig(&config_)) {
    LOG(ERROR) << "session-accept does not specify configuration";
    return false;
  }
  if (!candidate_config()->IsSupported(config_)) {
    LOG(ERROR) << "session-accept specifies an invalid configuration";
    return false;
  }

  return true;
}

void JingleSession::ProcessAuthenticationStep() {
  DCHECK_EQ(state_, CONNECTED);

  if (authenticator_->state() == Authenticator::MESSAGE_READY) {
    JingleMessage message(peer_jid_, JingleMessage::SESSION_INFO, session_id_);
    message.info = authenticator_->GetNextMessage();
    DCHECK(message.info.get());
    SendMessage(message);
  }
  DCHECK_NE(authenticator_->state(), Authenticator::MESSAGE_READY);

  if (authenticator_->state() == Authenticator::ACCEPTED) {
    SetState(AUTHENTICATED);
  } else if (authenticator_->state() == Authenticator::REJECTED) {
    CloseInternal(AuthRejectionReasonToErrorCode(
        authenticator_->rejection_reason()));
  }
}

void JingleSession::SendTransportInfo() {
  JingleMessage message(peer_jid_, JingleMessage::TRANSPORT_INFO, session_id_);
  message.candidates.swap(pending_candidates_);
  SendMessage(message);
}

void JingleSession::CloseInternal(ErrorCode error) {
  DCHECK(CalledOnValidThread());

  if (state_ == CONNECTING || state_ == CONNECTED || state_ == AUTHENTICATED) {
    // Send session-terminate message with the appropriate error code.
    JingleMessage::Reason reason;
    switch (error) {
      case OK:
        reason = JingleMessage::SUCCESS;
        break;
      case SESSION_REJECTED:
      case AUTHENTICATION_FAILED:
        reason = JingleMessage::DECLINE;
        break;
      case INCOMPATIBLE_PROTOCOL:
        reason = JingleMessage::INCOMPATIBLE_PARAMETERS;
        break;
      case HOST_OVERLOAD:
        reason = JingleMessage::CANCEL;
        break;
      default:
        reason = JingleMessage::GENERAL_ERROR;
    }

    JingleMessage message(peer_jid_, JingleMessage::SESSION_TERMINATE,
                          session_id_);
    message.reason = reason;
    SendMessage(message);
  }

  error_ = error;

  if (state_ != FAILED && state_ != CLOSED) {
    if (error != OK) {
      SetState(FAILED);
    } else {
      SetState(CLOSED);
    }
  }
}

void JingleSession::SetState(State new_state) {
  DCHECK(CalledOnValidThread());

  if (new_state != state_) {
    DCHECK_NE(state_, CLOSED);
    DCHECK_NE(state_, FAILED);

    state_ = new_state;
    if (event_handler_)
      event_handler_->OnSessionStateChange(new_state);
  }
}

}  // namespace protocol
}  // namespace remoting
