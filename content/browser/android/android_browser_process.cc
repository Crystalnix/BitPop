// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/android/android_browser_process.h"

#include "base/android/jni_string.h"
#include "base/debug/debugger.h"
#include "base/logging.h"
#include "content/browser/android/content_startup_flags.h"
#include "content/public/common/content_constants.h"
#include "jni/AndroidBrowserProcess_jni.h"

using base::android::ConvertJavaStringToUTF8;

namespace content {

static void SetCommandLineFlags(JNIEnv*env,
                                jclass clazz,
                                jint max_render_process_count) {
  SetContentCommandLineFlags(max_render_process_count);
}

static jboolean IsOfficialBuild(JNIEnv* env, jclass clazz) {
#if defined(OFFICIAL_BUILD)
  return true;
#else
  return false;
#endif
}

bool RegisterAndroidBrowserProcess(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

}  // namespace
