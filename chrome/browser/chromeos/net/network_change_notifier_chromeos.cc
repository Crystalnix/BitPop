// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/net/network_change_notifier_chromeos.h"

#include "base/bind.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/dbus/dbus_thread_manager.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace {

// Delay for online change notification reporting.
const int kOnlineNotificationDelayMS = 500;
const int kInitialNotificationCheckDelayMS = 1000;

bool IsOnline(chromeos::ConnectionState state) {
  return state == chromeos::STATE_ONLINE ||
         state == chromeos::STATE_PORTAL;
}

}

namespace chromeos {

NetworkChangeNotifierChromeos::NetworkChangeNotifierChromeos()
    : has_active_network_(false),
      connection_state_(chromeos::STATE_UNKNOWN),
      is_online_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_factory_(this)) {
  BrowserThread::PostDelayedTask(
         BrowserThread::UI, FROM_HERE,
         base::Bind(
             &NetworkChangeNotifierChromeos::UpdateInitialState, this),
         kInitialNotificationCheckDelayMS);
}

NetworkChangeNotifierChromeos::~NetworkChangeNotifierChromeos() {
}

void NetworkChangeNotifierChromeos::Init() {
  chromeos::NetworkLibrary* network_library =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  network_library->AddNetworkManagerObserver(this);

  DBusThreadManager::Get()->GetPowerManagerClient()->AddObserver(this);

  UpdateNetworkState(network_library);
}

void NetworkChangeNotifierChromeos::Shutdown() {
  weak_factory_.InvalidateWeakPtrs();

  if (!chromeos::CrosLibrary::Get())
    return;

  chromeos::NetworkLibrary* lib =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  lib->RemoveNetworkManagerObserver(this);
  lib->RemoveObserverForAllNetworks(this);

  DBusThreadManager::Get()->GetPowerManagerClient()->RemoveObserver(this);
}

void NetworkChangeNotifierChromeos::PowerChanged(
    const PowerSupplyStatus& status) {
}

void NetworkChangeNotifierChromeos::SystemResumed() {
  // Force invalidation of various net resources on system resume.
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(
          &NetworkChangeNotifier::NotifyObserversOfIPAddressChange));
}


void NetworkChangeNotifierChromeos::OnNetworkManagerChanged(
    chromeos::NetworkLibrary* cros) {
  UpdateNetworkState(cros);
}

bool NetworkChangeNotifierChromeos::IsCurrentlyOffline() const {
  return !IsOnline(connection_state_);
}

void NetworkChangeNotifierChromeos::OnNetworkChanged(
    chromeos::NetworkLibrary* cros,
    const chromeos::Network* network) {
  CHECK(network);

  // Active network changed?
  if (network->service_path() != service_path_)
    UpdateNetworkState(cros);
  else
    UpdateConnectivityState(network);
}

void NetworkChangeNotifierChromeos::UpdateNetworkState(
    chromeos::NetworkLibrary* lib) {
  const chromeos::Network* network = lib->active_network();

  if (network) {
    VLOG(2) << "UpdateNetworkState: type = "
            << network->type()
            << ", dev = "
            << network->device_path()
            << ", state_ = "
            << network->state();
  }

  // Check if active network was added, removed or changed.
  if ((!network && has_active_network_) ||
      (network && (!has_active_network_ ||
                   network->service_path() != service_path_ ||
                   network->ip_address() != ip_address_))) {
    if (has_active_network_)
      lib->RemoveObserverForAllNetworks(this);
    if (!network) {
      has_active_network_ = false;
      service_path_.clear();
      ip_address_.clear();
    } else {
      has_active_network_ = true;
      service_path_ = network->service_path();
      ip_address_ = network->ip_address();
    }
    UpdateConnectivityState(network);
    // If there is an active network, add observer to track its changes.
    if (network)
      lib->AddNetworkObserver(network->service_path(), this);

    DVLOG(1) << "NotifyObserversOfIPAddressChange!!";
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(
            &NetworkChangeNotifier::NotifyObserversOfIPAddressChange));
  }
}

void NetworkChangeNotifierChromeos::UpdateConnectivityState(
      const chromeos::Network* network) {
  if (network) {
    VLOG(2) << "UpdateConnectivityState: nt = "
            << network->type()
            << ", dev = "
            << network->device_path()
            << ", ns = "
            << network->state()
            << ", cs_ = "
            << connection_state_;
  }

  // We don't care about all transitions of ConnectionState.  OnlineStateChange
  // notification should trigger if
  //   a) we were online and went offline
  //   b) we were offline and went online
  //   c) switched to/from captive portal
  chromeos::ConnectionState new_connection_state =
      network ? network->connection_state() : chromeos::STATE_UNKNOWN;

  bool is_online = (new_connection_state == chromeos::STATE_ONLINE);
  bool was_online = (connection_state_ == chromeos::STATE_ONLINE);
  bool is_portal = (new_connection_state == chromeos::STATE_PORTAL);
  bool was_portal = (connection_state_ == chromeos::STATE_PORTAL);
  VLOG(2) << "UpdateConnectivityState: n_cs = "
          << new_connection_state
          << ", is_online = "
          << is_online
          << ", was_online = "
          << was_online
          << ", is_portal = "
          << is_portal
          << ", was_portal = "
          << was_portal;
  connection_state_ = new_connection_state;
  if (is_online != was_online || is_portal != was_portal) {
    ReportOnlineStateChange(IsOnline(connection_state_));
  }
  VLOG(2) << "UpdateConnectivityState: new_cs = "
          << new_connection_state
          << ", end_cs_ = "
          << connection_state_;
}

void NetworkChangeNotifierChromeos::ReportOnlineStateChange(bool is_online) {
  VLOG(2) << "ReportOnlineStateChange: " << (is_online ? "online" : "offline");
  if (weak_factory_.HasWeakPtrs()) {
    DVLOG(1) << "ReportOnlineStateChange: has pending task";
    // If we are trying to report the same state, just continue as planned.
    // If the online state had changed since we queued the reporting task,
    // then cancel it. This should help us avoid transient edge reporting
    // while switching between connection types (i.e. wifi->ethernet).
    if (is_online != is_online_) {
      weak_factory_.InvalidateWeakPtrs();
      DVLOG(1) << "ReportOnlineStateChange: canceled pending task";
    }
    return;
  }

  is_online_ = is_online;
  BrowserThread::PostDelayedTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(
          &NetworkChangeNotifierChromeos::ReportOnlineStateChangeOnUIThread,
          weak_factory_.GetWeakPtr()),
      kOnlineNotificationDelayMS);
}

void NetworkChangeNotifierChromeos::ReportOnlineStateChangeOnUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DVLOG(1) << "OnlineStatusReportThreadTask: firing notification!";
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(
         &NetworkChangeNotifierChromeos::NotifyObserversOfOnlineStateChange));
}

// static
void NetworkChangeNotifierChromeos::UpdateInitialState(
    NetworkChangeNotifierChromeos* self) {
  chromeos::NetworkLibrary* net =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  self->UpdateNetworkState(net);
}

}  // namespace chromeos
