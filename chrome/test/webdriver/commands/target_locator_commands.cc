// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/webdriver/commands/target_locator_commands.h"

#include "base/string_number_conversions.h"
#include "base/values.h"
#include "chrome/test/webdriver/commands/response.h"
#include "chrome/test/webdriver/error_codes.h"
#include "chrome/test/webdriver/session.h"
#include "chrome/test/webdriver/web_element_id.h"

namespace webdriver {

WindowHandleCommand::WindowHandleCommand(
    const std::vector<std::string>& path_segments,
    DictionaryValue* parameters)
    : WebDriverCommand(path_segments, parameters) {}

WindowHandleCommand::~WindowHandleCommand() {}

bool WindowHandleCommand::DoesGet() {
  return true;
}

void WindowHandleCommand::ExecuteGet(Response* const response) {
  response->SetStatus(kSuccess);
  response->SetValue(new StringValue(
      base::IntToString(session_->current_target().window_id)));
}

WindowHandlesCommand::WindowHandlesCommand(
    const std::vector<std::string>& path_segments,
    DictionaryValue* parameters)
    : WebDriverCommand(path_segments, parameters) {}

WindowHandlesCommand::~WindowHandlesCommand() {}

bool WindowHandlesCommand::DoesGet() {
  return true;
}

void WindowHandlesCommand::ExecuteGet(Response* const response) {
  std::vector<int> window_ids;
  if (!session_->GetWindowIds(&window_ids)) {
    SET_WEBDRIVER_ERROR(
        response, "Could not get window handles", kInternalServerError);
    return;
  }
  ListValue* id_list = new ListValue();
  for (size_t i = 0; i < window_ids.size(); ++i)
    id_list->Append(new StringValue(base::IntToString(window_ids[i])));
  response->SetStatus(kSuccess);
  response->SetValue(id_list);
}

WindowCommand::WindowCommand(
    const std::vector<std::string>& path_segments,
    DictionaryValue* parameters)
    : WebDriverCommand(path_segments, parameters) {}

WindowCommand::~WindowCommand() {}

bool WindowCommand::DoesPost() {
  return true;
}

bool WindowCommand::DoesDelete() {
  return true;
}

void WindowCommand::ExecutePost(Response* const response) {
  std::string name;
  if (!GetStringParameter("name", &name)) {
    SET_WEBDRIVER_ERROR(
        response, "Missing or invalid 'name' parameter", kBadRequest);
    return;
  }

  ErrorCode code = session_->SwitchToWindow(name);
  if (code != kSuccess) {
    SET_WEBDRIVER_ERROR(response, "Could not switch window", code);
    return;
  }
  response->SetStatus(kSuccess);
}

void WindowCommand::ExecuteDelete(Response* const response) {
  if (!session_->CloseWindow()) {
    SET_WEBDRIVER_ERROR(
        response, "Could not close window", kInternalServerError);
    return;
  }
  response->SetStatus(kSuccess);
}

SwitchFrameCommand::SwitchFrameCommand(
    const std::vector<std::string>& path_segments,
    DictionaryValue* parameters)
    : WebDriverCommand(path_segments, parameters) {}

SwitchFrameCommand::~SwitchFrameCommand() {}

bool SwitchFrameCommand::DoesPost() {
  return true;
}

void SwitchFrameCommand::ExecutePost(Response* const response) {
  std::string id;
  int index = 0;
  WebElementId element;
  if (GetStringParameter("id", &id)) {
    ErrorCode code = session_->SwitchToFrameWithNameOrId(id);
    if (code != kSuccess) {
      SET_WEBDRIVER_ERROR(response, "Could not switch to frame", code);
      return;
    }
  } else if (GetIntegerParameter("id", &index)) {
    ErrorCode code = session_->SwitchToFrameWithIndex(index);
    if (code != kSuccess) {
      SET_WEBDRIVER_ERROR(response, "Could not switch to frame", code);
      return;
    }
  } else if (GetWebElementParameter("id", &element)) {
    ErrorCode code = session_->SwitchToFrameWithElement(element);
    if (code != kSuccess) {
      SET_WEBDRIVER_ERROR(response, "Could not switch to frame", code);
      return;
    }
  } else if (IsNullParameter("id") || !HasParameter("id")) {
    // Treat null 'id' and no 'id' as the same.
    // See http://code.google.com/p/selenium/issues/detail?id=1479.
    session_->SwitchToTopFrame();
  } else {
    SET_WEBDRIVER_ERROR(
        response, "Invalid 'id' parameter", kBadRequest);
    return;
  }
  response->SetStatus(kSuccess);
}

bool SwitchFrameCommand::GetWebElementParameter(const std::string& key,
                                                WebElementId* out) const {
  DictionaryValue* value;
  if (!GetDictionaryParameter(key, &value))
    return false;

  WebElementId id(value);
  if (!id.is_valid())
    return false;

  *out = id;
  return true;
}

ActiveElementCommand::ActiveElementCommand(
    const std::vector<std::string>& path_segments,
    DictionaryValue* parameters)
    : WebDriverCommand(path_segments, parameters) {}

ActiveElementCommand::~ActiveElementCommand() {}

bool ActiveElementCommand::DoesPost() {
  return true;
}

void ActiveElementCommand::ExecutePost(Response* const response) {
  ListValue args;
  Value* result = NULL;
  ErrorCode status = session_->ExecuteScript(
      "return document.activeElement || document.body", &args, &result);
  response->SetStatus(status);
  response->SetValue(result);
}

}  // namespace webdriver
