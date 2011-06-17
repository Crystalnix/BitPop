// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/webdriver/commands/response.h"

#include "base/base64.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"

namespace webdriver {

namespace {

// Error message taken from:
// http://code.google.com/p/selenium/wiki/JsonWireProtocol#Response_Status_Codes
const char* const kStatusKey = "status";
const char* const kValueKey = "value";
const char* const kMessageKey = "message";
const char* const kScreenKey = "screen";
const char* const kClassKey = "class";
const char* const kStackTraceKey = "stackTrace";
const char* const kStackTraceFileNameKey = "fileName";
const char* const kStackTraceClassNameKey = "className";
const char* const kStackTraceMethodNameKey = "methodName";
const char* const kStackTraceLineNumberKey = "lineNumber";

}  // namespace

Response::Response() {
  SetStatus(kSuccess);
  SetValue(new DictionaryValue());
}

Response::~Response() {}

ErrorCode Response::GetStatus() const {
  int status;
  if (!data_.GetInteger(kStatusKey, &status))
    NOTREACHED();
  return static_cast<ErrorCode>(status);
}

void Response::SetStatus(ErrorCode status) {
  data_.SetInteger(kStatusKey, status);
}

const Value* Response::GetValue() const {
  Value* out = NULL;
  LOG_IF(WARNING, !data_.Get(kValueKey, &out))
      << "Accessing unset response value.";  // Should never happen.
  return out;
}

void Response::SetValue(Value* value) {
  data_.Set(kValueKey, value);
}

void Response::SetError(ErrorCode error_code, const std::string& message,
                        const std::string& file, int line) {
  DictionaryValue* error = new DictionaryValue;
  error->SetString(kMessageKey, message);

  DictionaryValue* stack = new DictionaryValue;
  stack->SetString(kStackTraceFileNameKey, file);
  stack->SetString(kStackTraceClassNameKey, "");
  stack->SetString(kStackTraceMethodNameKey, "");
  stack->SetInteger(kStackTraceLineNumberKey, line);
  ListValue* stack_list = new ListValue;
  stack_list->Append(stack);
  error->Set(kStackTraceKey, stack_list);

  SetStatus(error_code);
  SetValue(error);
}

void Response::SetError(ErrorCode error_code,
                        const std::string& message,
                        const std::string& file,
                        int line,
                        const std::string& png) {
  DictionaryValue* error = new DictionaryValue;

  error->SetString(kMessageKey, message);
  error->SetString(kStackTraceFileNameKey, file);
  error->SetInteger(kStackTraceLineNumberKey, line);
  std::string base64_png;

  // Convert the raw binary data to base 64 encoding for webdriver.
  if (!base::Base64Encode(png, &base64_png)) {
    LOG(ERROR) << "Failed to encode screenshot to base64 "
               << "sending back an empty string instead.";
  } else {
    error->SetString(kScreenKey, base64_png);
  }

  SetStatus(error_code);
  SetValue(error);
}


void Response::SetField(const std::string& key, Value* value) {
  data_.Set(key, value);
}

std::string Response::ToJSON() const {
  std::string json;
  base::JSONWriter::Write(&data_, false, &json);
  return json;
}

}  // namespace webdriver
