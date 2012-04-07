// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_SANDBOX_METHODS_LINUX_H_
#define CONTENT_COMMON_SANDBOX_METHODS_LINUX_H_
#pragma once

// This is a list of sandbox IPC methods which the renderer may send to the
// sandbox host. See http://code.google.com/p/chromium/wiki/LinuxSandboxIPC
// This isn't the full list, values < 32 are reserved for methods called from
// Skia.
class LinuxSandbox {
 public:
  enum Methods {
    METHOD_GET_FONT_FAMILY_FOR_CHARS = 32,
    METHOD_LOCALTIME = 33,
    METHOD_GET_CHILD_WITH_INODE = 34,
    METHOD_GET_STYLE_FOR_STRIKE = 35,
    METHOD_MAKE_SHARED_MEMORY_SEGMENT = 36,
    METHOD_MATCH_WITH_FALLBACK = 37,
  };
};

#endif  // CONTENT_COMMON_SANDBOX_METHODS_LINUX_H_
