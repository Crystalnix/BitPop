// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_PEPPER_PLUGIN_REGISTRY_H_
#define CONTENT_COMMON_PEPPER_PLUGIN_REGISTRY_H_
#pragma once

#include <list>
#include <map>
#include <string>
#include <vector>

#include "base/file_path.h"
#include "ppapi/proxy/dispatcher.h"
#include "webkit/plugins/npapi/webplugininfo.h"
#include "webkit/plugins/ppapi/plugin_delegate.h"
#include "webkit/plugins/ppapi/plugin_module.h"

struct PepperPluginInfo {
  PepperPluginInfo();
  ~PepperPluginInfo();

  webkit::npapi::WebPluginInfo ToWebPluginInfo() const;

  // Indicates internal plugins for which there's not actually a library.
  // These plugins are implemented in the Chrome binary using a separate set
  // of entry points (see internal_entry_points below).
  // Defaults to false.
  bool is_internal;

  // True when this plugin should be run out of process. Defaults to false.
  bool is_out_of_process;

  // Whether the plugin is enabled.  Defaults to true.
  bool enabled;

  FilePath path;  // Internal plugins have "internal-[name]" as path.
  std::string name;
  std::string description;
  std::string version;
  std::vector<webkit::npapi::WebPluginMimeType> mime_types;

  // When is_internal is set, this contains the function pointers to the
  // entry points for the internal plugins.
  webkit::ppapi::PluginModule::EntryPoints internal_entry_points;
};

// This class holds references to all of the known pepper plugin modules.
//
// It keeps two lists. One list of preloaded in-process modules, and one list
// is a list of all live modules (some of which may be out-of-process and hence
// not preloaded).
class PepperPluginRegistry
    : public webkit::ppapi::PluginDelegate::ModuleLifetime,
      public pp::proxy::Dispatcher::Delegate {
 public:
  ~PepperPluginRegistry();

  static PepperPluginRegistry* GetInstance();

  // Computes the list of known pepper plugins.
  //
  // This method is static so that it can be used by the browser process, which
  // has no need to load the pepper plugin modules. It will re-compute the
  // plugin list every time it is called. Generally, code in the registry should
  // be using the cached plugin_list_ instead.
  static void ComputeList(std::vector<PepperPluginInfo>* plugins);

  // Loads the (native) libraries but does not initialize them (i.e., does not
  // call PPP_InitializeModule). This is needed by the zygote on Linux to get
  // access to the plugins before entering the sandbox.
  static void PreloadModules();

  // Retrieves the information associated with the given plugin path. The
  // return value will be NULL if there is no such plugin.
  //
  // The returned pointer is owned by the PluginRegistry.
  const PepperPluginInfo* GetInfoForPlugin(const FilePath& path) const;

  // Returns an existing loaded module for the given path. It will search for
  // both preloaded in-process or currently active (non crashed) out-of-process
  // plugins matching the given name. Returns NULL if the plugin hasn't been
  // loaded.
  webkit::ppapi::PluginModule* GetLiveModule(const FilePath& path);

  // Notifies the registry that a new non-preloaded module has been created.
  // This is normally called for out-of-process plugins. Once this is called,
  // the module is available to be returned by GetModule(). The module will
  // automatically unregister itself by calling PluginModuleDestroyed().
  void AddLiveModule(const FilePath& path, webkit::ppapi::PluginModule* module);

  // ModuleLifetime implementation.
  virtual void PluginModuleDead(webkit::ppapi::PluginModule* dead_module);

 private:
  PepperPluginRegistry();

  // Dispatcher::Delegate implementation.
  virtual MessageLoop* GetIPCMessageLoop();
  virtual base::WaitableEvent* GetShutdownEvent();
  virtual std::set<PP_Instance>* GetGloballySeenInstanceIDSet();

  // All known pepper plugins.
  std::vector<PepperPluginInfo> plugin_list_;

  // Plugins that have been preloaded so they can be executed in-process in
  // the renderer (the sandbox prevents on-demand loading).
  typedef std::map<FilePath, scoped_refptr<webkit::ppapi::PluginModule> >
      OwningModuleMap;
  OwningModuleMap preloaded_modules_;

  // A list of non-owning pointers to all currently-live plugin modules. This
  // includes both preloaded ones in preloaded_modules_, and out-of-process
  // modules whose lifetime is managed externally. This will contain only
  // non-crashed modules. If an out-of-process module crashes, it may
  // continue as long as there are WebKit references to it, but it will not
  // appear in this list.
  typedef std::map<FilePath, webkit::ppapi::PluginModule*> NonOwningModuleMap;
  NonOwningModuleMap live_modules_;

  DISALLOW_COPY_AND_ASSIGN(PepperPluginRegistry);
};

#endif  // CONTENT_COMMON_PEPPER_PLUGIN_REGISTRY_H_
