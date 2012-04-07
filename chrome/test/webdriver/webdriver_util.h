// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_TEST_WEBDRIVER_WEBDRIVER_UTIL_H_
#define CHROME_TEST_WEBDRIVER_WEBDRIVER_UTIL_H_
#pragma once

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/values.h"
#include "chrome/test/automation/value_conversion_traits.h"
#include "chrome/test/webdriver/webdriver_error.h"

class AutomationId;
class FilePath;
class WebViewId;

namespace webdriver {

// Generates a random, 32-character hexidecimal ID.
std::string GenerateRandomID();

// Returns the equivalent JSON string for the given value.
std::string JsonStringify(const base::Value* value);

// Returns the JSON string for the given value, with the exception that
// long strings are shortened for easier display.
std::string JsonStringifyForDisplay(const base::Value* value);

// Returns the string representation of the given type, for display purposes.
const char* GetJsonTypeName(base::Value::Type type);

// Converts the automation ID to a string.
std::string AutomationIdToString(const AutomationId& id);

// Converts the string to an automation ID and returns true on success.
bool StringToAutomationId(const std::string& string_id, AutomationId* id);

// Converts the web view ID to a string.
std::string WebViewIdToString(const WebViewId& view_id);

// Converts the string to a web view ID and returns true on success.
bool StringToWebViewId(const std::string& string_id, WebViewId* view_id);

// Flattens the given list of strings into one.
Error* FlattenStringArray(const ListValue* src, string16* dest);

#if defined(OS_MACOSX)
// Gets the paths to the user and local application directory.
void GetApplicationDirs(std::vector<FilePath>* app_dirs);
#endif

// Parses a given value.
class ValueParser {
 public:
  virtual ~ValueParser();
  virtual bool Parse(base::Value* value) const = 0;

 protected:
  ValueParser();

 private:
  DISALLOW_COPY_AND_ASSIGN(ValueParser);
};

// Define a special type and constant that allows users to skip parsing a value.
// Useful when wanting to skip parsing for one value out of many in a list.
enum SkipParsing { };
extern SkipParsing* kSkipParsing;

// Parses a given value using the |ValueConversionTraits| to the appropriate
// type. This assumes that a direct conversion can be performed without
// pulling the value out of a dictionary or list.
template <typename T>
class DirectValueParser : public ValueParser {
 public:
  explicit DirectValueParser(T* t) : t_(t) { }

  virtual ~DirectValueParser() { }

  virtual bool Parse(base::Value* value) const OVERRIDE {
    return ValueConversionTraits<T>::SetFromValue(value, t_);
  }

 private:
  T* t_;
  DISALLOW_COPY_AND_ASSIGN(DirectValueParser);
};

// Convenience function for creating a DirectValueParser.
template <typename T>
DirectValueParser<T>* CreateDirectValueParser(T* t) {
  return new DirectValueParser<T>(t);
}

}  // namespace webdriver

// Value conversion traits for SkipParsing, which just return true.
template <>
struct ValueConversionTraits<webdriver::SkipParsing> {
  static bool SetFromValue(const base::Value* value,
                           const webdriver::SkipParsing* t);
  static bool CanConvert(const base::Value* value);
};

#endif  // CHROME_TEST_WEBDRIVER_WEBDRIVER_UTIL_H_
