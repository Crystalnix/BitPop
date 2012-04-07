// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/tts_custom_bindings.h"

#include <string>

#include "chrome/common/extensions/extension_action.h"
#include "chrome/renderer/extensions/extension_dispatcher.h"
#include "grit/renderer_resources.h"
#include "v8/include/v8.h"

namespace extensions {

TTSCustomBindings::TTSCustomBindings(
    int dependency_count,
    const char** dependencies)
    : ChromeV8Extension(
          "extensions/tts_custom_bindings.js",
          IDR_TTS_CUSTOM_BINDINGS_JS,
          dependency_count,
          dependencies,
          NULL) {}

static v8::Handle<v8::Value> GetNextTTSEventId(const v8::Arguments& args) {
  // Note: this works because the TTS API only works in the
  // extension process, not content scripts.
  static int next_tts_event_id = 1;
  return v8::Integer::New(next_tts_event_id++);
}

v8::Handle<v8::FunctionTemplate>
TTSCustomBindings::GetNativeFunction(
    v8::Handle<v8::String> name) {
  if (name->Equals(v8::String::New("GetNextTTSEventId")))
    return v8::FunctionTemplate::New(GetNextTTSEventId);

  return ChromeV8Extension::GetNativeFunction(name);
}

}  // extensions
