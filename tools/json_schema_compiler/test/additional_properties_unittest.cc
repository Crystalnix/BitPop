// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/json_schema_compiler/test/additional_properties.h"

#include "testing/gtest/include/gtest/gtest.h"

using namespace test::api::additional_properties;

TEST(JsonSchemaCompilerAdditionalPropertiesTest,
    AdditionalPropertiesTypePopulate) {
  {
    scoped_ptr<ListValue> list_value(new ListValue());
    list_value->Append(Value::CreateStringValue("asdf"));
    list_value->Append(Value::CreateIntegerValue(4));
    scoped_ptr<DictionaryValue> type_value(new DictionaryValue());
    type_value->SetString("string", "value");
    type_value->SetInteger("other", 9);
    type_value->Set("another", list_value.release());
    scoped_ptr<AdditionalPropertiesType> type(new AdditionalPropertiesType());
    EXPECT_TRUE(AdditionalPropertiesType::Populate(*type_value, type.get()));
    EXPECT_EQ("value", type->string);
    EXPECT_TRUE(type_value->Remove("string", NULL));
    EXPECT_TRUE(type->additional_properties.Equals(type_value.get()));
  }
  {
    scoped_ptr<DictionaryValue> type_value(new DictionaryValue());
    type_value->SetInteger("string", 3);
    scoped_ptr<AdditionalPropertiesType> type(new AdditionalPropertiesType());
    EXPECT_FALSE(AdditionalPropertiesType::Populate(*type_value, type.get()));
  }
}

TEST(JsonSchemaCompilerAdditionalPropertiesTest,
    AdditionalPropertiesParamsCreate) {
  scoped_ptr<DictionaryValue> param_object_value(new DictionaryValue());
  param_object_value->SetString("str", "a");
  param_object_value->SetInteger("num", 1);
  scoped_ptr<ListValue> params_value(new ListValue());
  params_value->Append(param_object_value->DeepCopy());
  scoped_ptr<AdditionalProperties::Params> params(
      AdditionalProperties::Params::Create(*params_value));
  EXPECT_TRUE(params.get());
  EXPECT_TRUE(params->param_object.additional_properties.Equals(
      param_object_value.get()));
}

TEST(JsonSchemaCompilerAdditionalPropertiesTest,
    ReturnAdditionalPropertiesResultCreate) {
  DictionaryValue additional;
  additional.SetString("key", "value");
  ReturnAdditionalProperties::Results::ResultObject result_object;
  result_object.integer = 5;
  result_object.additional_properties.MergeDictionary(&additional);
  scoped_ptr<ListValue> results =
      ReturnAdditionalProperties::Results::Create(result_object);
  DictionaryValue* result_dict = NULL;
  EXPECT_TRUE(results->GetDictionary(0, &result_dict));

  Value* int_temp_value_out = NULL;
  int int_temp = 0;
  EXPECT_TRUE(result_dict->Remove("integer", &int_temp_value_out));
  scoped_ptr<Value> int_temp_value(int_temp_value_out);
  EXPECT_TRUE(int_temp_value->GetAsInteger(&int_temp));
  EXPECT_EQ(5, int_temp);

  EXPECT_TRUE(result_dict->Equals(&additional));
}
