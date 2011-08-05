// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_PROXY_PROXY_MODULE_H_
#define PPAPI_PROXY_PROXY_MODULE_H_

#include <string>

#include "base/basictypes.h"

template<typename T> struct DefaultSingletonTraits;

namespace pp {
namespace proxy {

class PluginDispatcher;
class PluginResource;

class ProxyModule {
 public:
  // The global singleton getter.
  static ProxyModule* GetInstance();

  // TODO(viettrungluu): Generalize this for use with other plugins if it proves
  // necessary. (Currently, we can't do this easily, since we can't tell from
  // |PpapiPluginMain()| which plugin will be loaded.)
  const std::string& GetFlashCommandLineArgs();
  void SetFlashCommandLineArgs(const std::string& args);

 private:
  friend struct DefaultSingletonTraits<ProxyModule>;

  std::string flash_command_line_args_;

  ProxyModule();
  ~ProxyModule();

  DISALLOW_COPY_AND_ASSIGN(ProxyModule);
};

}  // namespace proxy
}  // namespace pp

#endif  // PPAPI_PROXY_PROXY_MODULE_H_
