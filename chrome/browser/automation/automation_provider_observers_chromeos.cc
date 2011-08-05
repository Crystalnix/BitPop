// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/automation/automation_provider_observers.h"

#include "base/values.h"
#include "chrome/browser/automation/automation_provider.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/login/authentication_notification_details.h"
#include "chrome/browser/chromeos/login/existing_user_controller.h"
#include "chrome/browser/chromeos/login/screen_locker.h"
#include "content/common/notification_service.h"

using chromeos::CrosLibrary;
using chromeos::NetworkLibrary;

NetworkManagerInitObserver::NetworkManagerInitObserver(
    AutomationProvider* automation)
    : automation_(automation->AsWeakPtr()) {}

NetworkManagerInitObserver::~NetworkManagerInitObserver() {
    CrosLibrary::Get()->GetNetworkLibrary()->RemoveNetworkManagerObserver(this);
}

bool NetworkManagerInitObserver::Init() {
  if (!CrosLibrary::Get()->EnsureLoaded()) {
    // If cros library fails to load, don't wait for the network
    // library to finish initializing, because it'll wait forever.
    automation_->OnNetworkLibraryInit();
    return false;
  }

  CrosLibrary::Get()->GetNetworkLibrary()->AddNetworkManagerObserver(this);
  return true;
}

void NetworkManagerInitObserver::OnNetworkManagerChanged(NetworkLibrary* obj) {
  if (!obj->wifi_scanning()) {
    automation_->OnNetworkLibraryInit();
    delete this;
  }
}

LoginObserver::LoginObserver(chromeos::ExistingUserController* controller,
                             AutomationProvider* automation,
                             IPC::Message* reply_message)
    : controller_(controller),
      automation_(automation->AsWeakPtr()),
      reply_message_(reply_message) {
  controller_->set_login_status_consumer(this);
  registrar_.Add(this, NotificationType::LOAD_STOP,
                 NotificationService::AllSources());
}

LoginObserver::~LoginObserver() {
  controller_->set_login_status_consumer(NULL);
}

void LoginObserver::OnLoginFailure(const chromeos::LoginFailure& error) {
  scoped_ptr<DictionaryValue> return_value(new DictionaryValue);
  return_value->SetString("error_string", error.GetErrorString());
  AutomationJSONReply(automation_, reply_message_.release())
      .SendSuccess(return_value.get());
  delete this;
}

void LoginObserver::OnLoginSuccess(
    const std::string& username,
    const std::string& password,
    const GaiaAuthConsumer::ClientLoginResult& credentials,
    bool pending_requests) {
  controller_->set_login_status_consumer(NULL);
}

void LoginObserver::Observe(NotificationType type,
                            const NotificationSource& source,
                            const NotificationDetails& details) {
  AutomationJSONReply(automation_, reply_message_.release()).SendSuccess(NULL);
  delete this;
}

ScreenLockUnlockObserver::ScreenLockUnlockObserver(
    AutomationProvider* automation,
    IPC::Message* reply_message,
    bool lock_screen)
    : automation_(automation->AsWeakPtr()),
      reply_message_(reply_message),
      lock_screen_(lock_screen) {
  registrar_.Add(this, NotificationType::SCREEN_LOCK_STATE_CHANGED,
                 NotificationService::AllSources());
}

ScreenLockUnlockObserver::~ScreenLockUnlockObserver() {}

void ScreenLockUnlockObserver::Observe(NotificationType type,
                                       const NotificationSource& source,
                                       const NotificationDetails& details) {
  DCHECK(type == NotificationType::SCREEN_LOCK_STATE_CHANGED);
  if (automation_) {
    AutomationJSONReply reply(automation_, reply_message_.release());
    bool is_screen_locked = *Details<bool>(details).ptr();
    if (lock_screen_ == is_screen_locked)
      reply.SendSuccess(NULL);
    else
      reply.SendError("Screen lock failure.");
  }
  delete this;
}

ScreenUnlockObserver::ScreenUnlockObserver(AutomationProvider* automation,
                                           IPC::Message* reply_message)
    : ScreenLockUnlockObserver(automation, reply_message, false) {
  chromeos::ScreenLocker::default_screen_locker()->SetLoginStatusConsumer(this);
}

ScreenUnlockObserver::~ScreenUnlockObserver() {
  chromeos::ScreenLocker* screen_locker =
      chromeos::ScreenLocker::default_screen_locker();
  if (screen_locker)
    screen_locker->SetLoginStatusConsumer(NULL);
}

void ScreenUnlockObserver::OnLoginFailure(const chromeos::LoginFailure& error) {
  if (automation_) {
    scoped_ptr<DictionaryValue> return_value(new DictionaryValue);
    return_value->SetString("error_string", error.GetErrorString());
    AutomationJSONReply(automation_, reply_message_.release())
        .SendSuccess(return_value.get());
  }
  delete this;
}

NetworkScanObserver::NetworkScanObserver(AutomationProvider* automation,
                                         IPC::Message* reply_message)
    : automation_(automation), reply_message_(reply_message) {
  NetworkLibrary* network_library = CrosLibrary::Get()->GetNetworkLibrary();
  network_library->AddNetworkManagerObserver(this);
}

NetworkScanObserver::~NetworkScanObserver() {
  NetworkLibrary* network_library = CrosLibrary::Get()->GetNetworkLibrary();
  network_library->RemoveNetworkManagerObserver(this);
}

void NetworkScanObserver::OnNetworkManagerChanged(NetworkLibrary* obj) {
  if (obj->wifi_scanning())
    return;

  AutomationJSONReply(automation_, reply_message_).SendSuccess(NULL);
  delete this;
}

NetworkConnectObserver::NetworkConnectObserver(AutomationProvider* automation,
                                               IPC::Message* reply_message)
    : automation_(automation), reply_message_(reply_message) {
  NetworkLibrary* network_library = CrosLibrary::Get()->GetNetworkLibrary();
  network_library->AddNetworkManagerObserver(this);
}

NetworkConnectObserver::~NetworkConnectObserver() {
  NetworkLibrary* network_library = CrosLibrary::Get()->GetNetworkLibrary();
  network_library->RemoveNetworkManagerObserver(this);
}

void NetworkConnectObserver::OnNetworkManagerChanged(NetworkLibrary* obj) {
  const chromeos::WifiNetwork* wifi = GetWifiNetwork(obj);
  if (!wifi) {
    // The network was not found, and we assume it no longer exists.
    // This could be because the SSID is invalid, or the network went away.
    scoped_ptr<DictionaryValue> return_value(new DictionaryValue);
    return_value->SetString("error_string", "Network not found.");
    AutomationJSONReply(automation_, reply_message_)
        .SendSuccess(return_value.get());
    delete this;
    return;
  }

  if (wifi->failed()) {
    scoped_ptr<DictionaryValue> return_value(new DictionaryValue);
    return_value->SetString("error_string", wifi->GetErrorString());
    AutomationJSONReply(automation_, reply_message_)
        .SendSuccess(return_value.get());
    delete this;
  } else if (wifi->connected()) {
    AutomationJSONReply(automation_, reply_message_).SendSuccess(NULL);
    delete this;
  }

  // The network is in the NetworkLibrary's list, but there's no failure or
  // success condition, so just continue waiting for more network events.
}

ServicePathConnectObserver::ServicePathConnectObserver(
    AutomationProvider* automation, IPC::Message* reply_message,
    const std::string& service_path)
    : NetworkConnectObserver(automation, reply_message),
    service_path_(service_path) {}

const chromeos::WifiNetwork* ServicePathConnectObserver::GetWifiNetwork(
    NetworkLibrary* network_library) {
  return network_library->FindWifiNetworkByPath(service_path_);
}

SSIDConnectObserver::SSIDConnectObserver(
    AutomationProvider* automation, IPC::Message* reply_message,
    const std::string& ssid)
    : NetworkConnectObserver(automation, reply_message), ssid_(ssid) {}

const chromeos::WifiNetwork* SSIDConnectObserver::GetWifiNetwork(
    NetworkLibrary* network_library) {
  const chromeos::WifiNetworkVector& wifi_networks =
      network_library->wifi_networks();
  for (chromeos::WifiNetworkVector::const_iterator iter = wifi_networks.begin();
       iter != wifi_networks.end(); ++iter) {
    const chromeos::WifiNetwork* wifi = *iter;
    if (wifi->name() == ssid_)
      return wifi;
  }
  return NULL;
}
