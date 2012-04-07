// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_EXTENSIONS_EVENT_BINDINGS_H_
#define CHROME_RENDERER_EXTENSIONS_EVENT_BINDINGS_H_
#pragma once

class ExtensionDispatcher;

namespace v8 {
class Extension;
}

// This class deals with the javascript bindings related to Event objects.
class EventBindings {
 public:
  static v8::Extension* Get(ExtensionDispatcher* dispatcher);
};

#endif  // CHROME_RENDERER_EXTENSIONS_EVENT_BINDINGS_H_
