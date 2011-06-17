// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/notifier/invalidation_notifier.h"

#include "base/logging.h"
#include "base/message_loop_proxy.h"
#include "chrome/browser/sync/notifier/sync_notifier_observer.h"
#include "chrome/browser/sync/protocol/service_constants.h"
#include "chrome/browser/sync/syncable/model_type_payload_map.h"
#include "jingle/notifier/base/const_communicator.h"
#include "jingle/notifier/base/notifier_options_util.h"
#include "jingle/notifier/communicator/connection_options.h"
#include "net/base/host_port_pair.h"
#include "net/url_request/url_request_context.h"
#include "talk/xmpp/jid.h"
#include "talk/xmpp/xmppclientsettings.h"

namespace sync_notifier {

InvalidationNotifier::InvalidationNotifier(
    const notifier::NotifierOptions& notifier_options,
    const std::string& client_info)
    : state_(STOPPED),
      notifier_options_(notifier_options),
      client_info_(client_info) {
  DCHECK_EQ(notifier::NOTIFICATION_SERVER,
            notifier_options.notification_method);
  DCHECK(notifier_options_.request_context_getter);
  // TODO(akalin): Replace NonThreadSafe checks with IO thread checks.
  DCHECK(notifier_options_.request_context_getter->GetIOMessageLoopProxy()->
      BelongsToCurrentThread());
}

InvalidationNotifier::~InvalidationNotifier() {
  DCHECK(non_thread_safe_.CalledOnValidThread());
}

void InvalidationNotifier::AddObserver(SyncNotifierObserver* observer) {
  DCHECK(non_thread_safe_.CalledOnValidThread());
  observers_.AddObserver(observer);
}

void InvalidationNotifier::RemoveObserver(SyncNotifierObserver* observer) {
  DCHECK(non_thread_safe_.CalledOnValidThread());
  observers_.RemoveObserver(observer);
}

void InvalidationNotifier::SetState(const std::string& state) {
  DCHECK(non_thread_safe_.CalledOnValidThread());
  invalidation_state_ = state;
}

void InvalidationNotifier::UpdateCredentials(
    const std::string& email, const std::string& token) {
  DCHECK(non_thread_safe_.CalledOnValidThread());
  VLOG(1) << "Updating credentials for " << email;
  buzz::XmppClientSettings xmpp_client_settings =
      notifier::MakeXmppClientSettings(notifier_options_,
                                       email, token, SYNC_SERVICE_NAME);
  if (state_ >= CONNECTING) {
    login_->UpdateXmppSettings(xmpp_client_settings);
  } else {
    notifier::ConnectionOptions options;
    VLOG(1) << "First time updating credentials: connecting";
    login_.reset(
        new notifier::Login(this,
                            xmpp_client_settings,
                            notifier::ConnectionOptions(),
                            notifier_options_.request_context_getter,
                            notifier::GetServerList(notifier_options_),
                            notifier_options_.try_ssltcp_first,
                            notifier_options_.auth_mechanism));
    login_->StartConnection();
    state_ = CONNECTING;
  }
}

void InvalidationNotifier::UpdateEnabledTypes(
    const syncable::ModelTypeSet& types) {
  DCHECK(non_thread_safe_.CalledOnValidThread());
  invalidation_client_.RegisterTypes(types);
}

void InvalidationNotifier::SendNotification() {
  DCHECK(non_thread_safe_.CalledOnValidThread());
}

void InvalidationNotifier::OnConnect(
    base::WeakPtr<talk_base::Task> base_task) {
  DCHECK(non_thread_safe_.CalledOnValidThread());
  VLOG(1) << "OnConnect";
  if (state_ >= STARTED) {
    invalidation_client_.ChangeBaseTask(base_task);
  } else {
    VLOG(1) << "First time connecting: starting invalidation client";
    // TODO(akalin): Make cache_guid() part of the client ID.  If we
    // do so and we somehow propagate it up to the server somehow, we
    // can make it so that we won't receive any notifications that
    // were generated from our own changes.
    const std::string kClientId = "invalidation_notifier";
    invalidation_client_.Start(
        kClientId, client_info_, invalidation_state_, this, this, base_task);
    invalidation_state_.clear();
    state_ = STARTED;
  }
}

void InvalidationNotifier::OnDisconnect() {
  DCHECK(non_thread_safe_.CalledOnValidThread());
  VLOG(1) << "OnDisconnect";
}

void InvalidationNotifier::OnInvalidate(
    const syncable::ModelTypePayloadMap& type_payloads) {
  DCHECK(non_thread_safe_.CalledOnValidThread());
  FOR_EACH_OBSERVER(SyncNotifierObserver, observers_,
                    OnIncomingNotification(type_payloads));
}

void InvalidationNotifier::OnSessionStatusChanged(bool has_session) {
  FOR_EACH_OBSERVER(SyncNotifierObserver, observers_,
                    OnNotificationStateChange(has_session));
}

void InvalidationNotifier::WriteState(const std::string& state) {
  DCHECK(non_thread_safe_.CalledOnValidThread());
  VLOG(1) << "WriteState";
  FOR_EACH_OBSERVER(SyncNotifierObserver, observers_, StoreState(state));
}

}  // namespace sync_notifier
