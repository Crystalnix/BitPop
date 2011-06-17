// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/ppapi/ppb_flash_impl.h"

#include <string>

#include "base/message_loop.h"
#include "base/time.h"
#include "googleurl/src/gurl.h"
#include "ppapi/c/private/ppb_flash.h"
#include "webkit/plugins/ppapi/common.h"
#include "webkit/plugins/ppapi/plugin_delegate.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"
#include "webkit/plugins/ppapi/ppb_url_request_info_impl.h"
#include "webkit/plugins/ppapi/resource_tracker.h"
#include "webkit/plugins/ppapi/var.h"

namespace webkit {
namespace ppapi {

namespace {

void SetInstanceAlwaysOnTop(PP_Instance pp_instance, PP_Bool on_top) {
  PluginInstance* instance = ResourceTracker::Get()->GetInstance(pp_instance);
  if (!instance)
    return;
  instance->set_always_on_top(PPBoolToBool(on_top));
}

PP_Var GetProxyForURL(PP_Instance pp_instance, const char* url) {
  PluginInstance* instance = ResourceTracker::Get()->GetInstance(pp_instance);
  if (!instance)
    return PP_MakeUndefined();

  GURL gurl(url);
  if (!gurl.is_valid())
    return PP_MakeUndefined();

  std::string proxy_host = instance->delegate()->ResolveProxy(gurl);
  if (proxy_host.empty())
    return PP_MakeUndefined();  // No proxy.
  return StringVar::StringToPPVar(instance->module(), proxy_host);
}

int32_t Navigate(PP_Resource request_id,
                 const char* target,
                 bool from_user_action) {
  scoped_refptr<PPB_URLRequestInfo_Impl> request(
      Resource::GetAs<PPB_URLRequestInfo_Impl>(request_id));
  if (!request)
    return PP_ERROR_BADRESOURCE;

  if (!target)
    return PP_ERROR_BADARGUMENT;

  PluginInstance* instance = request->instance();
  if (!instance)
    return PP_ERROR_FAILED;

  return instance->Navigate(request, target, from_user_action);
}

void RunMessageLoop(PP_Instance instance) {
  bool old_state = MessageLoop::current()->NestableTasksAllowed();
  MessageLoop::current()->SetNestableTasksAllowed(true);
  MessageLoop::current()->Run();
  MessageLoop::current()->SetNestableTasksAllowed(old_state);
}

void QuitMessageLoop(PP_Instance instance) {
  MessageLoop::current()->QuitNow();
}

double GetLocalTimeZoneOffset(PP_Instance pp_instance, PP_Time t) {
  PluginInstance* instance = ResourceTracker::Get()->GetInstance(pp_instance);
  if (!instance)
    return 0.0;

  // Evil hack. The time code handles exact "0" values as special, and produces
  // a "null" Time object. This will represent some date hundreds of years ago
  // and will give us funny results at 1970 (there are some tests where this
  // comes up, but it shouldn't happen in real life). To work around this
  // special handling, we just need to give it some nonzero value.
  if (t == 0.0)
    t = 0.0000000001;

  // We can't do the conversion here because on Linux, the localtime calls
  // require filesystem access prohibited by the sandbox.
  return instance->delegate()->GetLocalTimeZoneOffset(
      base::Time::FromDoubleT(t));
}

const PPB_Flash ppb_flash = {
  &SetInstanceAlwaysOnTop,
  &PPB_Flash_Impl::DrawGlyphs,
  &GetProxyForURL,
  &Navigate,
  &RunMessageLoop,
  &QuitMessageLoop,
  &GetLocalTimeZoneOffset
};

}  // namespace

// static
const PPB_Flash* PPB_Flash_Impl::GetInterface() {
  return &ppb_flash;
}

}  // namespace ppapi
}  // namespace webkit
