// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/ppapi/host_globals.h"

#include <limits>

#include "base/logging.h"
#include "base/rand_util.h"
#include "base/utf_string_conversions.h"
#include "ppapi/shared_impl/api_id.h"
#include "ppapi/shared_impl/function_group_base.h"
#include "ppapi/shared_impl/id_assignment.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebString.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebConsoleMessage.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebElement.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPluginContainer.h"
#include "webkit/plugins/ppapi/plugin_module.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"
#include "webkit/plugins/ppapi/ppb_cursor_control_impl.h"
#include "webkit/plugins/ppapi/ppb_flash_clipboard_impl.h"
#include "webkit/plugins/ppapi/ppb_font_impl.h"
#include "webkit/plugins/ppapi/ppb_text_input_impl.h"
#include "webkit/plugins/ppapi/resource_creation_impl.h"

using ppapi::CheckIdType;
using ppapi::MakeTypedId;
using ppapi::PPIdType;
using WebKit::WebConsoleMessage;
using WebKit::WebString;

namespace webkit {
namespace ppapi {

namespace {

typedef std::set<WebKit::WebPluginContainer*> ContainerSet;

// Adds all WebPluginContainers associated with the given module to the set.
void GetAllContainersForModule(PluginModule* module,
                               ContainerSet* containers) {
  const PluginModule::PluginInstanceSet& instances =
      module->GetAllInstances();
  for (PluginModule::PluginInstanceSet::const_iterator i = instances.begin();
       i != instances.end(); ++i)
    containers->insert((*i)->container());
}

WebConsoleMessage::Level LogLevelToWebLogLevel(PP_LogLevel_Dev level) {
  switch (level) {
    case PP_LOGLEVEL_TIP:
      return WebConsoleMessage::LevelTip;
    case PP_LOGLEVEL_LOG:
      return WebConsoleMessage::LevelLog;
    case PP_LOGLEVEL_WARNING:
      return WebConsoleMessage::LevelWarning;
    case PP_LOGLEVEL_ERROR:
    default:
      return WebConsoleMessage::LevelError;
  }
}

WebConsoleMessage MakeLogMessage(PP_LogLevel_Dev level,
                                 const std::string& source,
                                 const std::string& message) {
  std::string result = source;
  if (!result.empty())
    result.append(": ");
  result.append(message);
  return WebConsoleMessage(LogLevelToWebLogLevel(level),
                           WebString(UTF8ToUTF16(result)));
}

}  // namespace

struct HostGlobals::InstanceData {
  InstanceData() : instance(0) {}

  // Non-owning pointer to the instance object. When a PluginInstance is
  // destroyed, it will notify us and we'll delete all associated data.
  PluginInstance* instance;

  // Lazily allocated function proxies for the different interfaces.
  scoped_ptr< ::ppapi::FunctionGroupBase >
      function_proxies[::ppapi::API_ID_COUNT];
};

HostGlobals* HostGlobals::host_globals_ = NULL;

HostGlobals::HostGlobals() : ::ppapi::PpapiGlobals() {
  DCHECK(!host_globals_);
  host_globals_ = this;
}

HostGlobals::HostGlobals(::ppapi::PpapiGlobals::ForTest for_test)
    : ::ppapi::PpapiGlobals(for_test) {
  DCHECK(!host_globals_);
}

HostGlobals::~HostGlobals() {
  DCHECK(host_globals_ == this || !host_globals_);
  host_globals_ = NULL;
}

::ppapi::ResourceTracker* HostGlobals::GetResourceTracker() {
  return &resource_tracker_;
}

::ppapi::VarTracker* HostGlobals::GetVarTracker() {
  return &host_var_tracker_;
}

::ppapi::CallbackTracker* HostGlobals::GetCallbackTrackerForInstance(
    PP_Instance instance) {
  std::map<PP_Instance, linked_ptr<InstanceData> >::iterator found =
      instance_map_.find(instance);
  if (found == instance_map_.end())
    return NULL;

  return found->second->instance->module()->GetCallbackTracker();
}

::ppapi::FunctionGroupBase* HostGlobals::GetFunctionAPI(PP_Instance pp_instance,
                                                        ::ppapi::ApiID id) {
  // Get the instance object. This also ensures that the instance data is in
  // the map, since we need it below.
  PluginInstance* instance = GetInstance(pp_instance);
  if (!instance)
    return NULL;

  // The instance one is special, since it's just implemented by the instance
  // object.
  if (id == ::ppapi::API_ID_PPB_INSTANCE)
    return instance;

  scoped_ptr< ::ppapi::FunctionGroupBase >& proxy =
      instance_map_[pp_instance]->function_proxies[id];
  if (proxy.get())
    return proxy.get();

  switch (id) {
    case ::ppapi::API_ID_PPB_CURSORCONTROL:
      proxy.reset(new PPB_CursorControl_Impl(instance));
      break;
    case ::ppapi::API_ID_PPB_FONT:
      proxy.reset(new PPB_Font_FunctionImpl(instance));
      break;
    case ::ppapi::API_ID_PPB_TEXT_INPUT:
      proxy.reset(new PPB_TextInput_Impl(instance));
      break;
    case ::ppapi::API_ID_RESOURCE_CREATION:
      proxy.reset(new ResourceCreationImpl(instance));
      break;
    case ::ppapi::API_ID_PPB_FLASH_CLIPBOARD:
      proxy.reset(new PPB_Flash_Clipboard_Impl(instance));
      break;
    default:
      NOTREACHED();
  }

  return proxy.get();
}

PP_Module HostGlobals::GetModuleForInstance(PP_Instance instance) {
  PluginInstance* inst = GetInstance(instance);
  if (!inst)
    return 0;
  return inst->module()->pp_module();
}

base::Lock* HostGlobals::GetProxyLock() {
  // We do not lock on the host side.
  return NULL;
}

void HostGlobals::LogWithSource(PP_Instance instance,
                                PP_LogLevel_Dev level,
                                const std::string& source,
                                const std::string& value) {
  PluginInstance* instance_object = HostGlobals::Get()->GetInstance(instance);
  if (instance_object) {
    instance_object->container()->element().document().frame()->
        addMessageToConsole(MakeLogMessage(level, source, value));
  } else {
    BroadcastLogWithSource(0, level, source, value);
  }
}

void HostGlobals::BroadcastLogWithSource(PP_Module pp_module,
                                         PP_LogLevel_Dev level,
                                         const std::string& source,
                                         const std::string& value) {
  // Get the unique containers associated with the broadcast. This prevents us
  // from sending the same message to the same console when there are two
  // instances on the page.
  ContainerSet containers;
  PluginModule* module = GetModule(pp_module);
  if (module) {
    GetAllContainersForModule(module, &containers);
  } else {
    // Unknown module, get containers for all modules.
    for (ModuleMap::const_iterator i = module_map_.begin();
         i != module_map_.end(); ++i) {
      GetAllContainersForModule(i->second, &containers);
    }
  }

  WebConsoleMessage message = MakeLogMessage(level, source, value);
  for (ContainerSet::iterator i = containers.begin();
       i != containers.end(); ++i)
     (*i)->element().document().frame()->addMessageToConsole(message);
}

PP_Module HostGlobals::AddModule(PluginModule* module) {
#ifndef NDEBUG
  // Make sure we're not adding one more than once.
  for (ModuleMap::const_iterator i = module_map_.begin();
       i != module_map_.end(); ++i)
    DCHECK(i->second != module);
#endif

  // See AddInstance.
  PP_Module new_module;
  do {
    new_module = MakeTypedId(static_cast<PP_Module>(base::RandUint64()),
                             ::ppapi::PP_ID_TYPE_MODULE);
  } while (!new_module ||
           module_map_.find(new_module) != module_map_.end());
  module_map_[new_module] = module;
  return new_module;
}

void HostGlobals::ModuleDeleted(PP_Module module) {
  DLOG_IF(ERROR, !CheckIdType(module, ::ppapi::PP_ID_TYPE_MODULE))
      << module << " is not a PP_Module.";
  ModuleMap::iterator found = module_map_.find(module);
  if (found == module_map_.end()) {
    NOTREACHED();
    return;
  }
  module_map_.erase(found);
}

PluginModule* HostGlobals::GetModule(PP_Module module) {
  DLOG_IF(ERROR, !CheckIdType(module, ::ppapi::PP_ID_TYPE_MODULE))
      << module << " is not a PP_Module.";
  ModuleMap::iterator found = module_map_.find(module);
  if (found == module_map_.end())
    return NULL;
  return found->second;
}

PP_Instance HostGlobals::AddInstance(PluginInstance* instance) {
  DCHECK(instance_map_.find(instance->pp_instance()) == instance_map_.end());

  // Use a random number for the instance ID. This helps prevent some
  // accidents. See also AddModule below.
  //
  // Need to make sure the random number isn't a duplicate or 0.
  PP_Instance new_instance;
  do {
    new_instance = MakeTypedId(static_cast<PP_Instance>(base::RandUint64()),
                               ::ppapi::PP_ID_TYPE_INSTANCE);
  } while (!new_instance ||
           instance_map_.find(new_instance) != instance_map_.end() ||
           !instance->module()->ReserveInstanceID(new_instance));

  instance_map_[new_instance] = linked_ptr<InstanceData>(new InstanceData);
  instance_map_[new_instance]->instance = instance;

  resource_tracker_.DidCreateInstance(new_instance);
  return new_instance;
}

void HostGlobals::InstanceDeleted(PP_Instance instance) {
  resource_tracker_.DidDeleteInstance(instance);
  host_var_tracker_.ForceFreeNPObjectsForInstance(instance);
  instance_map_.erase(instance);
}

void HostGlobals::InstanceCrashed(PP_Instance instance) {
  resource_tracker_.DidDeleteInstance(instance);
  host_var_tracker_.ForceFreeNPObjectsForInstance(instance);
}

PluginInstance* HostGlobals::GetInstance(PP_Instance instance) {
  DLOG_IF(ERROR, !CheckIdType(instance, ::ppapi::PP_ID_TYPE_INSTANCE))
      << instance << " is not a PP_Instance.";
  InstanceMap::iterator found = instance_map_.find(instance);
  if (found == instance_map_.end())
    return NULL;
  return found->second->instance;
}

bool HostGlobals::IsHostGlobals() const {
  return true;
}

}  // namespace ppapi
}  // namespace webkit
