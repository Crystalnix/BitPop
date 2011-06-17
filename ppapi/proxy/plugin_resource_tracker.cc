// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/plugin_resource_tracker.h"

#include "base/logging.h"
#include "base/memory/singleton.h"
#include "ppapi/proxy/plugin_dispatcher.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/plugin_resource.h"
#include "ppapi/proxy/serialized_var.h"

namespace pp {
namespace proxy {

namespace {

// When non-NULL, this object overrides the ResourceTrackerSingleton.
PluginResourceTracker* g_resource_tracker_override = NULL;

}  // namespace

PluginResourceTracker::ResourceInfo::ResourceInfo() : ref_count(0) {
}

PluginResourceTracker::ResourceInfo::ResourceInfo(int rc,
                                                  linked_ptr<PluginResource> r)
    : ref_count(rc),
      resource(r) {
}

PluginResourceTracker::ResourceInfo::ResourceInfo(const ResourceInfo& other)
    : ref_count(other.ref_count),
      resource(other.resource) {
}

PluginResourceTracker::ResourceInfo::~ResourceInfo() {
}

PluginResourceTracker::ResourceInfo&
PluginResourceTracker::ResourceInfo::operator=(
    const ResourceInfo& other) {
  ref_count = other.ref_count;
  resource = other.resource;
  return *this;
}

// Start counting resources at a high number to avoid collisions with vars (to
// help debugging).
PluginResourceTracker::PluginResourceTracker()
    : last_resource_id_(0x00100000) {
}

PluginResourceTracker::~PluginResourceTracker() {
}

// static
void PluginResourceTracker::SetInstanceForTest(PluginResourceTracker* tracker) {
  g_resource_tracker_override = tracker;
}

// static
PluginResourceTracker* PluginResourceTracker::GetInstance() {
  if (g_resource_tracker_override)
    return g_resource_tracker_override;
  return Singleton<PluginResourceTracker>::get();
}

PluginResource* PluginResourceTracker::GetResourceObject(
    PP_Resource pp_resource) {
  ResourceMap::iterator found = resource_map_.find(pp_resource);
  if (found == resource_map_.end())
    return NULL;
  return found->second.resource.get();
}

PP_Resource PluginResourceTracker::AddResource(
    linked_ptr<PluginResource> object) {
  if (object->host_resource().is_null()) {
    // Prevent adding null resources or GetResourceObject(0) will return a
    // valid pointer!
    NOTREACHED();
    return 0;
  }

  PP_Resource plugin_resource = ++last_resource_id_;
  DCHECK(resource_map_.find(plugin_resource) == resource_map_.end());
  resource_map_[plugin_resource] = ResourceInfo(1, object);
  host_resource_map_[object->host_resource()] = plugin_resource;
  return plugin_resource;
}

void PluginResourceTracker::AddRefResource(PP_Resource resource) {
  ResourceMap::iterator found = resource_map_.find(resource);
  if (found == resource_map_.end()) {
    NOTREACHED();
    return;
  }
  found->second.ref_count++;
}

void PluginResourceTracker::ReleaseResource(PP_Resource resource) {
  ReleasePluginResourceRef(resource, true);
}

PP_Resource PluginResourceTracker::PluginResourceForHostResource(
    const HostResource& resource) const {
  HostResourceMap::const_iterator found = host_resource_map_.find(resource);
  if (found == host_resource_map_.end())
    return 0;
  return found->second;
}

void PluginResourceTracker::ReleasePluginResourceRef(
    const PP_Resource& resource,
    bool notify_browser_on_release) {
  ResourceMap::iterator found = resource_map_.find(resource);
  if (found == resource_map_.end())
    return;
  found->second.ref_count--;
  if (found->second.ref_count == 0) {
    // Keep a reference while removing in case the destructor ends up
    // re-entering. That way, when the destructor is called, it's out of the
    // maps.
    linked_ptr<PluginResource> plugin_resource = found->second.resource;
    PluginDispatcher* dispatcher =
        PluginDispatcher::GetForInstance(plugin_resource->instance());
    HostResource host_resource = plugin_resource->host_resource();
    host_resource_map_.erase(host_resource);
    resource_map_.erase(found);
    plugin_resource.reset();

    // dispatcher can be NULL if the plugin held on to a resource after the
    // instance was destroyed. In that case the browser-side resource has
    // already been freed correctly on the browser side.
    if (notify_browser_on_release && dispatcher) {
      dispatcher->Send(new PpapiHostMsg_PPBCore_ReleaseResource(
          INTERFACE_ID_PPB_CORE, host_resource));
    }
  }
}

}  // namespace proxy
}  // namespace pp
