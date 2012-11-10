// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefs/testing_pref_store.h"

#include "base/memory/scoped_ptr.h"
#include "base/values.h"

TestingPrefStore::TestingPrefStore()
    : read_only_(true),
      init_complete_(false) {
}

PrefStore::ReadResult TestingPrefStore::GetValue(const std::string& key,
                                                 const Value** value) const {
  return prefs_.GetValue(key, value) ? READ_OK : READ_NO_VALUE;
}

PrefStore::ReadResult TestingPrefStore::GetMutableValue(const std::string& key,
                                                        Value** value) {
  return prefs_.GetValue(key, value) ? READ_OK : READ_NO_VALUE;
}

void TestingPrefStore::AddObserver(PrefStore::Observer* observer) {
  observers_.AddObserver(observer);
}

void TestingPrefStore::RemoveObserver(PrefStore::Observer* observer) {
  observers_.RemoveObserver(observer);
}

size_t TestingPrefStore::NumberOfObservers() const {
  return observers_.size();
}

bool TestingPrefStore::IsInitializationComplete() const {
  return init_complete_;
}

void TestingPrefStore::SetValue(const std::string& key, Value* value) {
  if (prefs_.SetValue(key, value))
    NotifyPrefValueChanged(key);
}

void TestingPrefStore::SetValueSilently(const std::string& key, Value* value) {
  prefs_.SetValue(key, value);
}

void TestingPrefStore::RemoveValue(const std::string& key) {
  if (prefs_.RemoveValue(key))
    NotifyPrefValueChanged(key);
}

void TestingPrefStore::MarkNeedsEmptyValue(const std::string& key) {
}

bool TestingPrefStore::ReadOnly() const {
  return read_only_;
}

PersistentPrefStore::PrefReadError TestingPrefStore::GetReadError() const {
  return PersistentPrefStore::PREF_READ_ERROR_NONE;
}

PersistentPrefStore::PrefReadError TestingPrefStore::ReadPrefs() {
  NotifyInitializationCompleted();
  return PersistentPrefStore::PREF_READ_ERROR_NONE;
}

void TestingPrefStore::ReadPrefsAsync(ReadErrorDelegate* error_delegate_raw) {
  scoped_ptr<ReadErrorDelegate> error_delegate(error_delegate_raw);
  NotifyInitializationCompleted();
}

void TestingPrefStore::SetInitializationCompleted() {
  init_complete_ = true;
  NotifyInitializationCompleted();
}

void TestingPrefStore::NotifyPrefValueChanged(const std::string& key) {
  FOR_EACH_OBSERVER(Observer, observers_, OnPrefValueChanged(key));
}

void TestingPrefStore::NotifyInitializationCompleted() {
  FOR_EACH_OBSERVER(Observer, observers_, OnInitializationCompleted(true));
}

void TestingPrefStore::ReportValueChanged(const std::string& key) {
  FOR_EACH_OBSERVER(Observer, observers_, OnPrefValueChanged(key));
}

void TestingPrefStore::SetString(const std::string& key,
                                 const std::string& value) {
  SetValue(key, Value::CreateStringValue(value));
}

void TestingPrefStore::SetInteger(const std::string& key, int value) {
  SetValue(key, Value::CreateIntegerValue(value));
}

void TestingPrefStore::SetBoolean(const std::string& key, bool value) {
  SetValue(key, Value::CreateBooleanValue(value));
}

bool TestingPrefStore::GetString(const std::string& key,
                                 std::string* value) const {
  const Value* stored_value;
  if (!prefs_.GetValue(key, &stored_value) || !stored_value)
    return false;

  return stored_value->GetAsString(value);
}

bool TestingPrefStore::GetInteger(const std::string& key, int* value) const {
  const Value* stored_value;
  if (!prefs_.GetValue(key, &stored_value) || !stored_value)
    return false;

  return stored_value->GetAsInteger(value);
}

bool TestingPrefStore::GetBoolean(const std::string& key, bool* value) const {
  const Value* stored_value;
  if (!prefs_.GetValue(key, &stored_value) || !stored_value)
    return false;

  return stored_value->GetAsBoolean(value);
}

void TestingPrefStore::set_read_only(bool read_only) {
  read_only_ = read_only;
}

TestingPrefStore::~TestingPrefStore() {}
