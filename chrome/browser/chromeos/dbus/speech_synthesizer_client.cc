// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/dbus/speech_synthesizer_client.h"

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "chrome/browser/chromeos/system/runtime_environment.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_proxy.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

// TODO(chaitanyag): rename to "locale" after making equivalent change in
// Chrome OS code.
const char SpeechSynthesizerClient::kSpeechPropertyLocale[] = "name";

const char SpeechSynthesizerClient::kSpeechPropertyGender[] = "gender";
const char SpeechSynthesizerClient::kSpeechPropertyRate[] = "rate";
const char SpeechSynthesizerClient::kSpeechPropertyPitch[] = "pitch";
const char SpeechSynthesizerClient::kSpeechPropertyVolume[] = "volume";
const char SpeechSynthesizerClient::kSpeechPropertyEquals[] = "=";
const char SpeechSynthesizerClient::kSpeechPropertyDelimiter[] = ";";

class SpeechSynthesizerClientImpl : public SpeechSynthesizerClient {
 public:
  explicit SpeechSynthesizerClientImpl(dbus::Bus* bus)
      : proxy_(NULL),
        weak_ptr_factory_(this) {
    proxy_ = bus->GetObjectProxy(
        speech_synthesis::kSpeechSynthesizerServiceName,
        speech_synthesis::kSpeechSynthesizerServicePath);
  }
  virtual ~SpeechSynthesizerClientImpl() {}

  virtual void Speak(const std::string& text,
                     const std::string& properties) OVERRIDE {
    dbus::MethodCall method_call(speech_synthesis::kSpeechSynthesizerInterface,
                                 speech_synthesis::kSpeak);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(text);
    writer.AppendString(properties);
    proxy_->CallMethod(&method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                       base::Bind(&SpeechSynthesizerClientImpl::OnSpeak,
                                  weak_ptr_factory_.GetWeakPtr()));
  }

  virtual void StopSpeaking() OVERRIDE {
    dbus::MethodCall method_call(speech_synthesis::kSpeechSynthesizerInterface,
                                 speech_synthesis::kStop);
    proxy_->CallMethod(&method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                       base::Bind(&SpeechSynthesizerClientImpl::OnStopSpeaking,
                                  weak_ptr_factory_.GetWeakPtr()));
  }

  virtual void IsSpeaking(IsSpeakingCallback callback) OVERRIDE {
    dbus::MethodCall method_call(speech_synthesis::kSpeechSynthesizerInterface,
                                 speech_synthesis::kIsSpeaking);
    proxy_->CallMethod(&method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                       base::Bind(&SpeechSynthesizerClientImpl::OnIsSpeaking,
                                  weak_ptr_factory_.GetWeakPtr(),
                                  callback));
  }

 private:
  // Called when a response for Speak() is received
  void OnSpeak(dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Failed to speak.";
      return;
    }
    VLOG(1) << "Spoke: " << response->ToString();
  }

  // Called when a response for StopSpeaking() is received
  void OnStopSpeaking(dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << "Failed to stop speaking.";
      return;
    }
    VLOG(1) << "Stopped speaking: " << response->ToString();
  }

  // Called when a response for IsSpeaking() is received
  void OnIsSpeaking(IsSpeakingCallback callback, dbus::Response* response) {
    bool value = false;
    if (response) {
      dbus::MessageReader reader(response);
      reader.PopBool(&value);
    } else {
      LOG(ERROR) << "Failed to ask if it is speaking";
    }
    callback.Run(value);
  }

  dbus::ObjectProxy* proxy_;
  base::WeakPtrFactory<SpeechSynthesizerClientImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(SpeechSynthesizerClientImpl);
};

class SpeechSynthesizerClientStubImpl : public SpeechSynthesizerClient {
 public:
  SpeechSynthesizerClientStubImpl() {}
  virtual ~SpeechSynthesizerClientStubImpl() {}
  virtual void Speak(const std::string& text,
                     const std::string& properties) OVERRIDE {}
  virtual void StopSpeaking() OVERRIDE {}
  virtual void IsSpeaking(IsSpeakingCallback callback) OVERRIDE {
    callback.Run(false);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(SpeechSynthesizerClientStubImpl);
};

SpeechSynthesizerClient::SpeechSynthesizerClient() {
}

SpeechSynthesizerClient::~SpeechSynthesizerClient() {
}

// static
SpeechSynthesizerClient* SpeechSynthesizerClient::Create(dbus::Bus* bus) {
  if (system::runtime_environment::IsRunningOnChromeOS()) {
    return new SpeechSynthesizerClientImpl(bus);
  } else {
    return new SpeechSynthesizerClientStubImpl();
  }
}

}  // namespace chromeos
