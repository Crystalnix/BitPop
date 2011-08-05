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

void Response::SetError(Error* error) {
  DictionaryValue* error_dict = new DictionaryValue();
  error_dict->SetString(kMessageKey, error->ToString());

  SetStatus(error->code());
  SetValue(error_dict);
  delete error;
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
