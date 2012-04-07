// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_input_method_api.h"

#include "base/values.h"
#include "chrome/browser/chromeos/input_method/input_method_manager.h"
#include "chrome/browser/chromeos/extensions/input_method_event_router.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"

GetInputMethodFunction::GetInputMethodFunction() {
}

GetInputMethodFunction::~GetInputMethodFunction() {
}

bool GetInputMethodFunction::RunImpl() {
#if !defined(OS_CHROMEOS)
  NOTREACHED();
#else
  chromeos::ExtensionInputMethodEventRouter* router =
      profile_->GetExtensionService()->input_method_event_router();
  chromeos::input_method::InputMethodManager* manager =
      chromeos::input_method::InputMethodManager::GetInstance();
  const std::string input_method =
      router->GetInputMethodForXkb(manager->current_input_method().id());
  result_.reset(Value::CreateStringValue(input_method));
  return true;
#endif
}
