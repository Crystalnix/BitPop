// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/webdriver/webdriver_util.h"

#include "base/basictypes.h"
#include "base/format_macros.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/scoped_ptr.h"
#include "base/rand_util.h"
#include "base/stringprintf.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "base/third_party/icu/icu_utf.h"
#include "chrome/common/automation_id.h"
#include "chrome/test/automation/automation_json_requests.h"

using base::DictionaryValue;
using base::ListValue;
using base::Value;

namespace webdriver {

SkipParsing* kSkipParsing = NULL;

std::string GenerateRandomID() {
  uint64 msb = base::RandUint64();
  uint64 lsb = base::RandUint64();
  return base::StringPrintf("%016" PRIx64 "%016" PRIx64, msb, lsb);
}

std::string JsonStringify(const Value* value) {
  std::string json;
  base::JSONWriter::Write(value, false, &json);
  return json;
}

namespace {

// Truncates the given string to 40 characters, adding an ellipsis if
// truncation was necessary.
void TruncateString(std::string* data) {
  if (data->length() > 40) {
    data->resize(40);
    data->replace(37, 3, "...");
  }
}

// Truncates all strings contained in the given value.
void TruncateContainedStrings(Value* value) {
  ListValue* list;
  if (value->IsType(Value::TYPE_DICTIONARY)) {
    DictionaryValue* dict = static_cast<DictionaryValue*>(value);
    DictionaryValue::key_iterator key = dict->begin_keys();
    for (; key != dict->end_keys(); ++key) {
      Value* child;
      if (!dict->GetWithoutPathExpansion(*key, &child))
        continue;
      std::string data;
      if (child->GetAsString(&data)) {
        TruncateString(&data);
        dict->SetWithoutPathExpansion(*key, Value::CreateStringValue(data));
      } else {
        TruncateContainedStrings(child);
      }
    }
  } else if (value->GetAsList(&list)) {
    for (size_t i = 0; i < list->GetSize(); ++i) {
      Value* child;
      if (!list->Get(i, &child))
        continue;
      std::string data;
      if (child->GetAsString(&data)) {
        TruncateString(&data);
        list->Set(i, Value::CreateStringValue(data));
      } else {
        TruncateContainedStrings(child);
      }
    }
  }
}

}  // namespace

std::string JsonStringifyForDisplay(const Value* value) {
  scoped_ptr<Value> copy;
  if (value->IsType(Value::TYPE_STRING)) {
    std::string data;
    value->GetAsString(&data);
    TruncateString(&data);
    copy.reset(Value::CreateStringValue(data));
  } else {
    copy.reset(value->DeepCopy());
    TruncateContainedStrings(copy.get());
  }
  std::string json;
  base::JSONWriter::Write(copy.get(), true /* pretty_print */, &json);
  return json;
}

const char* GetJsonTypeName(Value::Type type) {
  switch (type) {
    case Value::TYPE_NULL:
      return "null";
    case Value::TYPE_BOOLEAN:
      return "boolean";
    case Value::TYPE_INTEGER:
      return "integer";
    case Value::TYPE_DOUBLE:
      return "double";
    case Value::TYPE_STRING:
      return "string";
    case Value::TYPE_BINARY:
      return "binary";
    case Value::TYPE_DICTIONARY:
      return "dictionary";
    case Value::TYPE_LIST:
      return "list";
  }
  return "unknown";
}

std::string AutomationIdToString(const AutomationId& id) {
  return base::StringPrintf("%d-%s", id.type(), id.id().c_str());
}

bool StringToAutomationId(const std::string& string_id, AutomationId* id) {
  std::vector<std::string> split_id;
  base::SplitString(string_id, '-', &split_id);
  if (split_id.size() != 2)
    return false;
  int type;
  if (!base::StringToInt(split_id[0], &type))
    return false;
  *id = AutomationId(static_cast<AutomationId::Type>(type), split_id[1]);
  return true;
}

std::string WebViewIdToString(const WebViewId& view_id) {
  return base::StringPrintf(
      "%s%s",
      view_id.old_style() ? "t" : "f",
      AutomationIdToString(view_id.GetId()).c_str());
}

bool StringToWebViewId(const std::string& string_id, WebViewId* view_id) {
  if (string_id.empty() || (string_id[0] != 'f' && string_id[0] != 't'))
    return false;
  bool old_style = string_id[0] == 't';
  AutomationId id;
  if (!StringToAutomationId(string_id.substr(1), &id))
    return false;

  if (old_style) {
    int tab_id;
    if (!base::StringToInt(id.id(), &tab_id))
      return false;
    *view_id = WebViewId::ForOldStyleTab(tab_id);
  } else {
    *view_id = WebViewId::ForView(id);
  }
  return true;
}

Error* FlattenStringArray(const ListValue* src, string16* dest) {
  string16 keys;
  for (size_t i = 0; i < src->GetSize(); ++i) {
    string16 keys_list_part;
    src->GetString(i, &keys_list_part);
    for (size_t j = 0; j < keys_list_part.size(); ++j) {
      if (CBU16_IS_SURROGATE(keys_list_part[j])) {
        return new Error(kBadRequest,
                         "ChromeDriver only supports characters in the BMP");
      }
    }
    keys.append(keys_list_part);
  }
  *dest = keys;
  return NULL;
}

ValueParser::ValueParser() { }

ValueParser::~ValueParser() { }

}  // namespace webdriver

bool ValueConversionTraits<webdriver::SkipParsing>::SetFromValue(
    const Value* value, const webdriver::SkipParsing* t) {
  return true;
}

bool ValueConversionTraits<webdriver::SkipParsing>::CanConvert(
    const Value* value) {
  return true;
}
