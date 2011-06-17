/* Copyright (c) 2010 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef PPAPI_C_PPB_URL_LOADER_TRUSTED_H_
#define PPAPI_C_PPB_URL_LOADER_TRUSTED_H_

#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/c/pp_stdint.h"

#define PPB_URLLOADERTRUSTED_INTERFACE "PPB_URLLoaderTrusted;0.3"

// Callback that indicates the status of the download and upload for the
// given URLLoader resource.
typedef void (*PP_URLLoaderTrusted_StatusCallback)(
      PP_Instance pp_instance,
      PP_Resource pp_resource,
      int64_t bytes_sent,
      int64_t total_bytes_to_be_sent,
      int64_t bytes_received,
      int64_t total_bytes_to_be_received);

// Available only to trusted implementations.
struct PPB_URLLoaderTrusted {
  // Grant this URLLoader the capability to make unrestricted cross-origin
  // requests.
  void (*GrantUniversalAccess)(PP_Resource loader);

  // Registers that the given function will be called when the upload or
  // downloaded byte count has changed. This is not exposed on the untrusted
  // interface because it can be quite chatty and encourages people to write
  // feedback UIs that update as frequently as the progress updates.
  //
  // The other serious gotcha with this callback is that the callback must not
  // mutate the URL loader or cause it to be destroyed.
  //
  // However, the proxy layer needs this information to push to the other
  // process, so we expose it here. Only one callback can be set per URL
  // Loader. Setting to a NULL callback will disable it.
  void (*RegisterStatusCallback)(PP_Resource loader,
                                 PP_URLLoaderTrusted_StatusCallback cb);
};

#endif  // PPAPI_C_PPB_URL_LOADER_H_

