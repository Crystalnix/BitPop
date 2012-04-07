// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_EXTENSIONS_TTS_CUSTOM_BINDINGS_H_
#define CHROME_RENDERER_EXTENSIONS_TTS_CUSTOM_BINDINGS_H_
#pragma once

#include "chrome/renderer/extensions/chrome_v8_extension.h"

namespace extensions {

// Implements custom bindings for the tts API.
class TTSCustomBindings : public ChromeV8Extension {
 public:
  TTSCustomBindings(int dependency_count, const char** dependencies);

  virtual v8::Handle<v8::FunctionTemplate> GetNativeFunction(
      v8::Handle<v8::String> name) OVERRIDE;
};

}  // extensions

#endif  // CHROME_RENDERER_EXTENSIONS_TTS_CUSTOM_BINDINGS_H_
