// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/android/media_jni_registrar.h"

#include "base/basictypes.h"
#include "base/android/jni_android.h"
#include "base/android/jni_registrar.h"

#include "media/base/android/media_player_bridge.h"

namespace media {

static base::android::RegistrationMethod kMediaRegisteredMethods[] = {
  { "MediaPlayerListener",
    MediaPlayerBridge::RegisterMediaPlayerListener },
};

bool RegisterJni(JNIEnv* env) {
  return base::android::RegisterNativeMethods(
      env, kMediaRegisteredMethods, arraysize(kMediaRegisteredMethods));
}

}  // namespace media
