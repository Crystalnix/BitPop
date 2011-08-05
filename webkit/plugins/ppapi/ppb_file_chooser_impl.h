// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_PLUGINS_PPAPI_PPB_FILE_CHOOSER_IMPL_H_
#define WEBKIT_PLUGINS_PPAPI_PPB_FILE_CHOOSER_IMPL_H_

#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "ppapi/c/dev/ppb_file_chooser_dev.h"
#include "ppapi/thunk/ppb_file_chooser_api.h"
#include "webkit/plugins/ppapi/resource.h"

struct PP_CompletionCallback;

namespace webkit {
namespace ppapi {

class PluginInstance;
class PPB_FileRef_Impl;
class TrackedCompletionCallback;

class PPB_FileChooser_Impl : public Resource,
                             public ::ppapi::thunk::PPB_FileChooser_API {
 public:
  PPB_FileChooser_Impl(PluginInstance* instance,
                       const PP_FileChooserOptions_Dev* options);
  virtual ~PPB_FileChooser_Impl();

  static PP_Resource Create(PP_Instance instance,
                            const PP_FileChooserOptions_Dev* options);

  // Resource overrides.
  virtual PPB_FileChooser_Impl* AsPPB_FileChooser_Impl();

  // ResourceObjectBase overrides.
  virtual ::ppapi::thunk::PPB_FileChooser_API* AsPPB_FileChooser_API() OVERRIDE;

  // Stores the list of selected files.
  void StoreChosenFiles(const std::vector<std::string>& files);

  // Check that |callback| is valid (only non-blocking operation is supported)
  // and that no callback is already pending. Returns |PP_OK| if okay, else
  // |PP_ERROR_...| to be returned to the plugin.
  int32_t ValidateCallback(const PP_CompletionCallback& callback);

  // Sets up |callback| as the pending callback. This should only be called once
  // it is certain that |PP_OK_COMPLETIONPENDING| will be returned.
  void RegisterCallback(const PP_CompletionCallback& callback);

  void RunCallback(int32_t result);

  // PPB_FileChooser_API implementation.
  virtual int32_t Show(PP_CompletionCallback callback) OVERRIDE;
  virtual PP_Resource GetNextChosenFile() OVERRIDE;

 private:
  PP_FileChooserMode_Dev mode_;
  std::string accept_mime_types_;
  scoped_refptr<TrackedCompletionCallback> callback_;
  std::vector< scoped_refptr<PPB_FileRef_Impl> > chosen_files_;
  size_t next_chosen_file_index_;
};

}  // namespace ppapi
}  // namespace webkit

#endif  // WEBKIT_PLUGINS_PPAPI_PPB_FILE_CHOOSER_IMPL_H_
