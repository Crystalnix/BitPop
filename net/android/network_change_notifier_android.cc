// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/android/network_change_notifier_android.h"

#include "base/logging.h"
#include "base/android/jni_android.h"
#include "jni/NetworkChangeNotifier_jni.h"

namespace net {
namespace android {

NetworkChangeNotifier::NetworkChangeNotifier() {
  JNIEnv* env = base::android::AttachCurrentThread();
  CreateJavaObject(env);
}

NetworkChangeNotifier::~NetworkChangeNotifier() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_NetworkChangeNotifier_destroy(
      env, java_network_change_notifier_.obj());
}

void NetworkChangeNotifier::CreateJavaObject(JNIEnv* env) {
  java_network_change_notifier_.Reset(
      Java_NetworkChangeNotifier_create(
          env,
          base::android::GetApplicationContext(),
          reinterpret_cast<jint>(this)));
}

void NetworkChangeNotifier::NotifyObservers(JNIEnv* env, jobject obj) {
  NotifyObserversOfConnectionTypeChange();
}

net::NetworkChangeNotifier::ConnectionType
    NetworkChangeNotifier::GetCurrentConnectionType() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  // TODO(droger): Return something more detailed than CONNECTION_UNKNOWN.
  return Java_NetworkChangeNotifier_isConnected(
      env, java_network_change_notifier_.obj()) ?
          net::NetworkChangeNotifier::CONNECTION_UNKNOWN :
          net::NetworkChangeNotifier::CONNECTION_NONE;
}

// static
bool NetworkChangeNotifier::Register(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

}  // namespace android
}  // namespace net
