// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/ppapi/host_var_tracker.h"

#include "base/logging.h"
#include "ppapi/c/pp_var.h"
#include "webkit/plugins/ppapi/host_array_buffer_var.h"
#include "webkit/plugins/ppapi/npobject_var.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"

using ppapi::ArrayBufferVar;
using ppapi::NPObjectVar;

namespace webkit {
namespace ppapi {

HostVarTracker::HostVarTracker() {
}

HostVarTracker::~HostVarTracker() {
}

ArrayBufferVar* HostVarTracker::CreateArrayBuffer(uint32 size_in_bytes) {
  return new HostArrayBufferVar(size_in_bytes);
}

void HostVarTracker::AddNPObjectVar(NPObjectVar* object_var) {
  DCHECK(CalledOnValidThread());

  InstanceMap::iterator found_instance = instance_map_.find(
      object_var->pp_instance());
  if (found_instance == instance_map_.end()) {
    // Lazily create the instance map.
    DCHECK(object_var->pp_instance() != 0);
    found_instance = instance_map_.insert(std::make_pair(
        object_var->pp_instance(),
        linked_ptr<NPObjectToNPObjectVarMap>(new NPObjectToNPObjectVarMap))).
            first;
  }
  NPObjectToNPObjectVarMap* np_object_map = found_instance->second.get();

  DCHECK(np_object_map->find(object_var->np_object()) ==
         np_object_map->end()) << "NPObjectVar already in map";
  np_object_map->insert(
      std::make_pair(object_var->np_object(), object_var->AsWeakPtr()));
}

void HostVarTracker::RemoveNPObjectVar(NPObjectVar* object_var) {
  DCHECK(CalledOnValidThread());

  InstanceMap::iterator found_instance = instance_map_.find(
      object_var->pp_instance());
  if (found_instance == instance_map_.end()) {
    NOTREACHED() << "NPObjectVar has invalid instance.";
    return;
  }
  NPObjectToNPObjectVarMap* np_object_map = found_instance->second.get();

  NPObjectToNPObjectVarMap::iterator found_object =
      np_object_map->find(object_var->np_object());
  if (found_object == np_object_map->end()) {
    NOTREACHED() << "NPObjectVar not registered.";
    return;
  }
  if (found_object->second != object_var) {
    NOTREACHED() << "NPObjectVar doesn't match.";
    return;
  }
  np_object_map->erase(found_object);

  // Clean up when the map is empty.
  if (np_object_map->empty())
    instance_map_.erase(found_instance);
}

NPObjectVar* HostVarTracker::NPObjectVarForNPObject(PP_Instance instance,
                                                    NPObject* np_object) {
  DCHECK(CalledOnValidThread());

  InstanceMap::iterator found_instance = instance_map_.find(instance);
  if (found_instance == instance_map_.end())
    return NULL;  // No such instance.
  NPObjectToNPObjectVarMap* np_object_map = found_instance->second.get();

  NPObjectToNPObjectVarMap::iterator found_object =
      np_object_map->find(np_object);
  if (found_object == np_object_map->end())
    return NULL;  // No such object.
  return found_object->second;
}

int HostVarTracker::GetLiveNPObjectVarsForInstance(PP_Instance instance) const {
  DCHECK(CalledOnValidThread());

  InstanceMap::const_iterator found = instance_map_.find(instance);
  if (found == instance_map_.end())
    return 0;
  return static_cast<int>(found->second->size());
}

void HostVarTracker::DidDeleteInstance(PP_Instance instance) {
  DCHECK(CalledOnValidThread());

  InstanceMap::iterator found_instance = instance_map_.find(instance);
  if (found_instance == instance_map_.end())
    return;  // Nothing to do.
  NPObjectToNPObjectVarMap* np_object_map = found_instance->second.get();

  // Force delete all var references. It's possible that deleting an object "A"
  // will cause it to delete another object "B" it references, thus removing "B"
  // from instance_map_. Therefore, we need to make a copy over which we can
  // iterate safely. Furthermore, the maps contain WeakPtrs so that we can
  // detect if the object is gone so that we don't dereference invalid memory.
  NPObjectToNPObjectVarMap np_object_map_copy = *np_object_map;
  NPObjectToNPObjectVarMap::iterator cur_var =
      np_object_map_copy.begin();
  while (cur_var != np_object_map_copy.end()) {
    NPObjectToNPObjectVarMap::iterator current = cur_var++;
    ForceReleaseNPObject(current->second);
    np_object_map->erase(current->first);
  }

  // Remove the record for this instance since it should be empty.
  DCHECK(np_object_map->empty());
  instance_map_.erase(found_instance);
}

void HostVarTracker::ForceReleaseNPObject(
    const base::WeakPtr< ::ppapi::NPObjectVar>& object) {
  // There's a chance that the object was already deleted before we got here.
  // See DidDeleteInstance for further explanation. If the object was deleted,
  // the WeakPtr will return NULL.
  if (!object.get())
    return;
  object->InstanceDeleted();
  VarMap::iterator iter = live_vars_.find(object->GetExistingVarID());
  if (iter == live_vars_.end()) {
    NOTREACHED();
    return;
  }
  iter->second.ref_count = 0;
  DCHECK(iter->second.track_with_no_reference_count == 0);
  DeleteObjectInfoIfNecessary(iter);
}

}  // namespace ppapi
}  // namespace webkit
