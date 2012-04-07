// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/dbus/session_manager_client.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/string_util.h"
#include "chrome/browser/chromeos/system/runtime_environment.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_proxy.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

// The SessionManagerClient implementation used in production.
class SessionManagerClientImpl : public SessionManagerClient {
 public:
  explicit SessionManagerClientImpl(dbus::Bus* bus)
      : session_manager_proxy_(NULL),
        weak_ptr_factory_(this) {
    session_manager_proxy_ = bus->GetObjectProxy(
        login_manager::kSessionManagerServiceName,
        login_manager::kSessionManagerServicePath);

    // Monitor the D-Bus signal for owner key changes.
    session_manager_proxy_->ConnectToSignal(
        chromium::kChromiumInterface,
        chromium::kOwnerKeySetSignal,
        base::Bind(&SessionManagerClientImpl::OwnerKeySetReceived,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&SessionManagerClientImpl::SignalConnected,
                   weak_ptr_factory_.GetWeakPtr()));

    // Monitor the D-Bus signal for property changes.
    session_manager_proxy_->ConnectToSignal(
        chromium::kChromiumInterface,
        chromium::kPropertyChangeCompleteSignal,
        base::Bind(&SessionManagerClientImpl::PropertyChangeCompleteReceived,
                   weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&SessionManagerClientImpl::SignalConnected,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  virtual ~SessionManagerClientImpl() {
  }

  // SessionManagerClient override.
  virtual void AddObserver(Observer* observer) OVERRIDE {
    observers_.AddObserver(observer);
  }

  // SessionManagerClient override.
  virtual void RemoveObserver(Observer* observer) OVERRIDE {
    observers_.RemoveObserver(observer);
  }

  // SessionManagerClient override.
  virtual void EmitLoginPromptReady() OVERRIDE {
    dbus::MethodCall method_call(
        login_manager::kSessionManagerInterface,
        login_manager::kSessionManagerEmitLoginPromptReady);
    session_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&SessionManagerClientImpl::OnEmitLoginPromptReady,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  // SessionManagerClient override.
  virtual void EmitLoginPromptVisible() OVERRIDE {
    dbus::MethodCall method_call(
        login_manager::kSessionManagerInterface,
        login_manager::kSessionManagerEmitLoginPromptVisible);
    session_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&SessionManagerClientImpl::OnEmitLoginPromptVisible,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  // SessionManagerClient override.
  virtual void RestartJob(int pid, const std::string& command_line) OVERRIDE {
    dbus::MethodCall method_call(login_manager::kSessionManagerInterface,
                                 login_manager::kSessionManagerRestartJob);
    dbus::MessageWriter writer(&method_call);
    writer.AppendInt32(pid);
    writer.AppendString(command_line);
    session_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&SessionManagerClientImpl::OnRestartJob,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  // SessionManagerClient override.
  virtual void RestartEntd() OVERRIDE {
    dbus::MethodCall method_call(login_manager::kSessionManagerInterface,
                                 login_manager::kSessionManagerRestartEntd);
    session_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&SessionManagerClientImpl::OnRestartEntd,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  // SessionManagerClient override.
  virtual void StartSession(const std::string& user_email) OVERRIDE {
    dbus::MethodCall method_call(login_manager::kSessionManagerInterface,
                                 login_manager::kSessionManagerStartSession);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(user_email);
    writer.AppendString("");  // Unique ID is deprecated
    session_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&SessionManagerClientImpl::OnStartSession,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  // SessionManagerClient override.
  virtual void StopSession() OVERRIDE {
    dbus::MethodCall method_call(login_manager::kSessionManagerInterface,
                                 login_manager::kSessionManagerStopSession);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString("");  // Unique ID is deprecated
    session_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&SessionManagerClientImpl::OnStopSession,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  // SessionManagerClient override.
  virtual void RetrievePolicy(RetrievePolicyCallback callback) OVERRIDE {
    dbus::MethodCall method_call(login_manager::kSessionManagerInterface,
                                 login_manager::kSessionManagerRetrievePolicy);
    session_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&SessionManagerClientImpl::OnRetrievePolicy,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

  // SessionManagerClient override.
  virtual void StorePolicy(const std::string& policy_blob,
                           StorePolicyCallback callback) OVERRIDE {
    dbus::MethodCall method_call(login_manager::kSessionManagerInterface,
                                 login_manager::kSessionManagerStorePolicy);
    dbus::MessageWriter writer(&method_call);
    // static_cast does not work due to signedness.
    writer.AppendArrayOfBytes(
        reinterpret_cast<const uint8*>(policy_blob.data()), policy_blob.size());
    session_manager_proxy_->CallMethod(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&SessionManagerClientImpl::OnStorePolicy,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
  }

 private:
  // Called when kSessionManagerEmitLoginPromptReady method is complete.
  void OnEmitLoginPromptReady(dbus::Response* response) {
    LOG_IF(ERROR, !response)
        << "Failed to call "
        << login_manager::kSessionManagerEmitLoginPromptReady;
  }

  // Called when kSessionManagerEmitLoginPromptVisible method is complete.
  void OnEmitLoginPromptVisible(dbus::Response* response) {
    LOG_IF(ERROR, !response)
        << "Failed to call "
        << login_manager::kSessionManagerEmitLoginPromptVisible;
  }

  // Called when kSessionManagerRestartJob method is complete.
  void OnRestartJob(dbus::Response* response) {
    LOG_IF(ERROR, !response)
        << "Failed to call "
        << login_manager::kSessionManagerRestartJob;
  }

  // Called when kSessionManagerRestartEntd method is complete.
  void OnRestartEntd(dbus::Response* response) {
    LOG_IF(ERROR, !response)
        << "Failed to call "
        << login_manager::kSessionManagerRestartEntd;
  }

  // Called when kSessionManagerStartSession method is complete.
  void OnStartSession(dbus::Response* response) {
    LOG_IF(ERROR, !response)
        << "Failed to call "
        << login_manager::kSessionManagerStartSession;
  }

  // Called when kSessionManagerStopSession method is complete.
  void OnStopSession(dbus::Response* response) {
    LOG_IF(ERROR, !response)
        << "Failed to call "
        << login_manager::kSessionManagerStopSession;
  }

  // Called when kSessionManagerRetrievePolicy method is complete.
  void OnRetrievePolicy(RetrievePolicyCallback callback,
                        dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Failed to call "
                 << login_manager::kSessionManagerRetrievePolicy;
      callback.Run("");
      return;
    }
    dbus::MessageReader reader(response);
    uint8* values = NULL;
    size_t length = 0;
    if (!reader.PopArrayOfBytes(&values, &length)) {
      LOG(ERROR) << "Invalid response: " << response->ToString();
      callback.Run("");
      return;
    }
    // static_cast does not work due to signedness.
    std::string serialized_proto(reinterpret_cast<char*>(values), length);
    callback.Run(serialized_proto);
  }

  // Called when kSessionManagerStorePolicy method is complete.
  void OnStorePolicy(StorePolicyCallback callback, dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Failed to call "
                 << login_manager::kSessionManagerStorePolicy;
      return;
    }
    dbus::MessageReader reader(response);
    bool success = false;
    if (!reader.PopBool(&success)) {
      LOG(ERROR) << "Invalid response: " << response->ToString();
      return;
    }
    callback.Run(success);
  }

  // Called when the owner key set signal is received.
  void OwnerKeySetReceived(dbus::Signal* signal) {
    dbus::MessageReader reader(signal);
    std::string result_string;
    if (!reader.PopString(&result_string)) {
      LOG(ERROR) << "Invalid signal: " << signal->ToString();
      return;
    }
    const bool success = StartsWithASCII(result_string, "success", false);
    FOR_EACH_OBSERVER(Observer, observers_, OwnerKeySet(success));
  }

  // Called when the property change complete signal is received.
  void PropertyChangeCompleteReceived(dbus::Signal* signal) {
    dbus::MessageReader reader(signal);
    std::string result_string;
    if (!reader.PopString(&result_string)) {
      LOG(ERROR) << "Invalid signal: " << signal->ToString();
      return;
    }
    const bool success = StartsWithASCII(result_string, "success", false);
    FOR_EACH_OBSERVER(Observer, observers_, PropertyChangeComplete(success));
  }

  // Called when the object is connected to the signal.
  void SignalConnected(const std::string& interface_name,
                       const std::string& signal_name,
                       bool success) {
    LOG_IF(ERROR, !success) << "Failed to connect to " << signal_name;
  }

  dbus::ObjectProxy* session_manager_proxy_;
  ObserverList<Observer> observers_;
  base::WeakPtrFactory<SessionManagerClientImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(SessionManagerClientImpl);
};

// The SessionManagerClient implementation used on Linux desktop,
// which does nothing.
class SessionManagerClientStubImpl : public SessionManagerClient {
  // SessionManagerClient overrides.
  virtual void AddObserver(Observer* observer) OVERRIDE {}
  virtual void RemoveObserver(Observer* observer) OVERRIDE {}
  virtual void EmitLoginPromptReady() OVERRIDE {}
  virtual void EmitLoginPromptVisible() OVERRIDE {}
  virtual void RestartJob(int pid, const std::string& command_line) OVERRIDE {}
  virtual void RestartEntd() OVERRIDE {}
  virtual void StartSession(const std::string& user_email) OVERRIDE {}
  virtual void StopSession() OVERRIDE {}
  virtual void RetrievePolicy(RetrievePolicyCallback callback) OVERRIDE {
    callback.Run("");
  }
  virtual void StorePolicy(const std::string& policy_blob,
                           StorePolicyCallback callback) OVERRIDE {
    callback.Run(true);
  }

};

SessionManagerClient::SessionManagerClient() {
}

SessionManagerClient::~SessionManagerClient() {
}

SessionManagerClient* SessionManagerClient::Create(dbus::Bus* bus) {
  if (system::runtime_environment::IsRunningOnChromeOS()) {
    return new SessionManagerClientImpl(bus);
  } else {
    return new SessionManagerClientStubImpl();
  }
}

}  // namespace chromeos
