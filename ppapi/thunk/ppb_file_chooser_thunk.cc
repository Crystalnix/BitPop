// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/c/dev/ppb_file_chooser_dev.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/thunk/thunk.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/ppb_file_chooser_api.h"
#include "ppapi/thunk/resource_creation_api.h"

namespace ppapi {
namespace thunk {

namespace {

PP_Resource Create(PP_Instance instance,
                   const PP_FileChooserOptions_Dev* options) {
  EnterFunction<ResourceCreationAPI> enter(instance, true);
  if (enter.failed())
    return 0;
  return enter.functions()->CreateFileChooser(instance, options);
}

PP_Bool IsFileChooser(PP_Resource resource) {
  EnterResource<PPB_FileChooser_API> enter(resource, false);
  return PP_FromBool(enter.succeeded());
}

int32_t Show(PP_Resource chooser, PP_CompletionCallback callback) {
  EnterResource<PPB_FileChooser_API> enter(chooser, true);
  if (enter.failed())
    return PP_ERROR_BADRESOURCE;
  return enter.object()->Show(callback);
}

PP_Resource GetNextChosenFile(PP_Resource chooser) {
  EnterResource<PPB_FileChooser_API> enter(chooser, true);
  if (enter.failed())
    return 0;
  return enter.object()->GetNextChosenFile();
}

const PPB_FileChooser_Dev g_ppb_file_chooser_thunk = {
  &Create,
  &IsFileChooser,
  &Show,
  &GetNextChosenFile
};

}  // namespace

const PPB_FileChooser_Dev* GetPPB_FileChooser_Thunk() {
  return &g_ppb_file_chooser_thunk;
}

}  // namespace thunk
}  // namespace ppapi
