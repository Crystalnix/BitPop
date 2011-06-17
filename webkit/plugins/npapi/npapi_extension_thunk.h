// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_PLUGINS_NPAPI_NPAPI_EXTENSION_THUNK_H_
#define WEBKIT_PLUGINS_NPAPI_NPAPI_EXTENSION_THUNK_H_

#include "third_party/npapi/bindings/npapi_extensions.h"

// This file implements forwarding for the NPAPI "Pepper" extensions through to
// the WebPluginDelegate associated with the plugin.

namespace webkit {
namespace npapi {

// Implements NPN_GetValue for the case of NPNVPepperExtensions. The function
// pointers in the returned structure implement all the extensions.
NPError GetPepperExtensionsFunctions(void* value);

}  // namespace npapi
}  // namespace webkit

#endif  // WEBKIT_PLUGINS_NPAPI_NPAPI_EXTENSION_THUNK_H_


