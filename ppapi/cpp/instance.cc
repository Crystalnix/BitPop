// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/cpp/instance.h"

#include "ppapi/c/dev/ppp_printing_dev.h"
#include "ppapi/c/ppb_instance.h"
#include "ppapi/c/ppb_messaging.h"
#include "ppapi/cpp/common.h"
#include "ppapi/cpp/dev/surface_3d_dev.h"
#include "ppapi/cpp/graphics_2d.h"
#include "ppapi/cpp/image_data.h"
#include "ppapi/cpp/logging.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/module_impl.h"
#include "ppapi/cpp/point.h"
#include "ppapi/cpp/resource.h"
#include "ppapi/cpp/var.h"

namespace pp {

namespace {

template <> const char* interface_name<PPB_Instance>() {
  return PPB_INSTANCE_INTERFACE;
}

template <> const char* interface_name<PPB_Messaging>() {
  return PPB_MESSAGING_INTERFACE;
}

}  // namespace

Instance::Instance(PP_Instance instance) : pp_instance_(instance) {
}

Instance::~Instance() {
  // Ensure that all per-instance objects have been removed. Generally, these
  // objects should have their lifetime scoped to the instance, such as being
  // instance members or even implemented by your instance sub-class directly.
  //
  // If they're not unregistered at this point, they will usually have a
  // dangling reference to the instance, which can cause a crash later.
  PP_DCHECK(interface_name_to_objects_.empty());
}

bool Instance::Init(uint32_t /*argc*/, const char* /*argn*/[],
                    const char* /*argv*/[]) {
  return true;
}

void Instance::DidChangeView(const pp::Rect& /*position*/,
                             const pp::Rect& /*clip*/) {
}

void Instance::DidChangeFocus(bool /*has_focus*/) {
}


bool Instance::HandleDocumentLoad(const URLLoader& /*url_loader*/) {
  return false;
}

bool Instance::HandleInputEvent(const PP_InputEvent& /*event*/) {
  return false;
}

void Instance::HandleMessage(const Var& /*message*/) {
  return;
}

Var Instance::GetSelectedText(bool /* html */) {
  return Var();
}

#ifndef PPAPI_INSTANCE_REMOVE_SCRIPTING
Var Instance::GetWindowObject() {
  if (!has_interface<PPB_Instance>())
    return Var();
  return Var(Var::PassRef(),
             get_interface<PPB_Instance>()->GetWindowObject(pp_instance()));
}

Var Instance::GetOwnerElementObject() {
  if (!has_interface<PPB_Instance>())
    return Var();
  return Var(Var::PassRef(),
             get_interface<PPB_Instance>()->GetOwnerElementObject(
                 pp_instance()));
}

Var Instance::ExecuteScript(const Var& script, Var* exception) {
  if (!has_interface<PPB_Instance>())
    return Var();
  return Var(Var::PassRef(),
             get_interface<PPB_Instance>()->ExecuteScript(
                 pp_instance(),
                 script.pp_var(),
                 Var::OutException(exception).get()));
}

Var Instance::GetInstanceObject() {
  return Var();
}
#endif

bool Instance::BindGraphics(const Graphics2D& graphics) {
  if (!has_interface<PPB_Instance>())
    return false;
  return PPBoolToBool(get_interface<PPB_Instance>()->BindGraphics(
      pp_instance(), graphics.pp_resource()));
}

bool Instance::BindGraphics(const Surface3D_Dev& graphics) {
  if (!has_interface<PPB_Instance>())
    return false;
  return PPBoolToBool(get_interface<PPB_Instance>()->BindGraphics(
      pp_instance(), graphics.pp_resource()));
}

bool Instance::IsFullFrame() {
  if (!has_interface<PPB_Instance>())
    return false;
  return PPBoolToBool(get_interface<PPB_Instance>()->IsFullFrame(
      pp_instance()));
}

void Instance::PostMessage(const Var& message) {
  if (!has_interface<PPB_Messaging>())
    return;
  get_interface<PPB_Messaging>()->PostMessage(pp_instance(),
                                              message.pp_var());
}

void Instance::AddPerInstanceObject(const std::string& interface_name,
                                    void* object) {
  // Ensure we're not trying to register more than one object per interface
  // type. Otherwise, we'll get confused in GetPerInstanceObject.
  PP_DCHECK(interface_name_to_objects_.find(interface_name) ==
            interface_name_to_objects_.end());
  interface_name_to_objects_[interface_name] = object;
}

void Instance::RemovePerInstanceObject(const std::string& interface_name,
                                       void* object) {
  InterfaceNameToObjectMap::iterator found = interface_name_to_objects_.find(
      interface_name);
  if (found == interface_name_to_objects_.end()) {
    // Attempting to unregister an object that doesn't exist or was already
    // unregistered.
    PP_DCHECK(false);
    return;
  }

  // Validate that we're removing the object we thing we are.
  PP_DCHECK(found->second == object);
  (void)object;  // Prevent warning in release mode.

  interface_name_to_objects_.erase(found);
}

// static
void* Instance::GetPerInstanceObject(PP_Instance instance,
                                     const std::string& interface_name) {
  Instance* that = Module::Get()->InstanceForPPInstance(instance);
  if (!that)
    return NULL;
  InterfaceNameToObjectMap::iterator found =
      that->interface_name_to_objects_.find(interface_name);
  if (found == that->interface_name_to_objects_.end())
    return NULL;
  return found->second;
}

}  // namespace pp
