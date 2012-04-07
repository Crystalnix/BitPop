// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_GLUE_USER_AGENT_H_
#define WEBKIT_GLUE_USER_AGENT_H_

#include <string>

#include "base/basictypes.h"

namespace webkit_glue {

// Builds a User-agent compatible string that describes the OS and CPU type.
std::string BuildOSCpuInfo();

// Returns the WebKit version, in the form "major.minor (branch@revision)".
std::string GetWebKitVersion();

// The following 2 functions return the major and minor webkit versions.
int GetWebKitMajorVersion();
int GetWebKitMinorVersion();

// Helper function to generate a full user agent string from a short
// product name.
std::string BuildUserAgentFromProduct(const std::string& product);
}  // namespace webkit_glue

#endif  // WEBKIT_GLUE_USER_AGENT_H_

