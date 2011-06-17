// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/ppapi/resource.h"

#include "base/logging.h"
#include "webkit/plugins/ppapi/callbacks.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"
#include "webkit/plugins/ppapi/plugin_module.h"
#include "webkit/plugins/ppapi/resource_tracker.h"

namespace webkit {
namespace ppapi {

Resource::Resource(PluginInstance* instance)
    : resource_id_(0), instance_(instance) {
}

Resource::~Resource() {
}

PP_Resource Resource::GetReference() {
  ResourceTracker *tracker = ResourceTracker::Get();
  if (resource_id_)
    tracker->AddRefResource(resource_id_);
  else
    resource_id_ = tracker->AddResource(this);
  return resource_id_;
}

PP_Resource Resource::GetReferenceNoAddRef() const {
  return resource_id_;
}

void Resource::LastPluginRefWasDeleted(bool instance_destroyed) {
  DCHECK(resource_id_ != 0);
  instance()->module()->GetCallbackTracker()->PostAbortForResource(
      resource_id_);
  resource_id_ = 0;

  if (instance_destroyed)
    instance_ = NULL;
}

#define DEFINE_TYPE_GETTER(RESOURCE)            \
  RESOURCE* Resource::As##RESOURCE() { return NULL; }
FOR_ALL_RESOURCES(DEFINE_TYPE_GETTER)
#undef DEFINE_TYPE_GETTER

}  // namespace ppapi
}  // namespace webkit

