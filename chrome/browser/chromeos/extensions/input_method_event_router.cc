// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "input_method_event_router.h"

#include <algorithm>

#include "base/json/json_writer.h"
#include "base/values.h"
#include "chrome/browser/chromeos/web_socket_proxy_controller.h"
#include "chrome/browser/extensions/extension_event_names.h"
#include "chrome/browser/extensions/extension_event_router.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile.h"

namespace {

// Prefix, which is used by XKB.
const char kXkbPrefix[] = "xkb:";

}  // namespace

namespace chromeos {

ExtensionInputMethodEventRouter::ExtensionInputMethodEventRouter() {
  input_method::InputMethodManager::GetInstance()->AddObserver(this);
}

ExtensionInputMethodEventRouter::~ExtensionInputMethodEventRouter() {
  input_method::InputMethodManager::GetInstance()->RemoveObserver(this);
}

void ExtensionInputMethodEventRouter::InputMethodChanged(
    input_method::InputMethodManager *manager,
    const input_method::InputMethodDescriptor &current_input_method,
    size_t num_active_input_methods) {
  Profile *profile = ProfileManager::GetDefaultProfile();
  ExtensionEventRouter *router = profile->GetExtensionEventRouter();

  if (!router->HasEventListener(extension_event_names::kOnInputMethodChanged))
    return;

  ListValue args;
  StringValue *input_method_name =
      new StringValue(GetInputMethodForXkb(current_input_method.id()));
  args.Append(input_method_name);
  std::string args_json;
  base::JSONWriter::Write(&args, false, &args_json);

  // The router will only send the event to extensions that are listening.
  router->DispatchEventToRenderers(
      extension_event_names::kOnInputMethodChanged,
      args_json, profile, GURL());
}

void ExtensionInputMethodEventRouter::ActiveInputMethodsChanged(
    input_method::InputMethodManager *manager,
    const input_method::InputMethodDescriptor & current_input_method,
    size_t num_active_input_methods) {
}

void ExtensionInputMethodEventRouter::PropertyListChanged(
    input_method::InputMethodManager *manager,
    const input_method::ImePropertyList & current_ime_properties) {
}

std::string ExtensionInputMethodEventRouter::GetInputMethodForXkb(
    const std::string& xkb_id) {
  size_t prefix_length = std::string(kXkbPrefix).length();
  DCHECK(xkb_id.substr(0, prefix_length) == kXkbPrefix);
  return xkb_id.substr(prefix_length);
}

}  // namespace chromeos
