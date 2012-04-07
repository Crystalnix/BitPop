// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefs/pref_member.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/value_conversions.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/common/chrome_notification_types.h"

using content::BrowserThread;

namespace subtle {

PrefMemberBase::PrefMemberBase()
    : observer_(NULL),
      prefs_(NULL),
      setting_value_(false) {
}

PrefMemberBase::~PrefMemberBase() {
  Destroy();
}


void PrefMemberBase::Init(const char* pref_name,
                          PrefService* prefs,
                          content::NotificationObserver* observer) {
  DCHECK(pref_name);
  DCHECK(prefs);
  DCHECK(pref_name_.empty());  // Check that Init is only called once.
  observer_ = observer;
  prefs_ = prefs;
  pref_name_ = pref_name;
  // Check that the preference is registered.
  DCHECK(prefs_->FindPreference(pref_name_.c_str()));

  // Add ourselves as a pref observer so we can keep our local value in sync.
  prefs_->AddPrefObserver(pref_name, this);
}

void PrefMemberBase::Destroy() {
  if (prefs_ && !pref_name_.empty()) {
    prefs_->RemovePrefObserver(pref_name_.c_str(), this);
    prefs_ = NULL;
  }
}

void PrefMemberBase::MoveToThread(BrowserThread::ID thread_id) {
  VerifyValuePrefName();
  // Load the value from preferences if it hasn't been loaded so far.
  if (!internal())
    UpdateValueFromPref();
  internal()->MoveToThread(thread_id);
}

void PrefMemberBase::Observe(int type,
                             const content::NotificationSource& source,
                             const content::NotificationDetails& details) {
  VerifyValuePrefName();
  DCHECK(chrome::NOTIFICATION_PREF_CHANGED == type);
  UpdateValueFromPref();
  if (!setting_value_ && observer_)
    observer_->Observe(type, source, details);
}

void PrefMemberBase::UpdateValueFromPref() const {
  VerifyValuePrefName();
  const PrefService::Preference* pref =
      prefs_->FindPreference(pref_name_.c_str());
  DCHECK(pref);
  if (!internal())
    CreateInternal();
  internal()->UpdateValue(pref->GetValue()->DeepCopy(), pref->IsManaged());
}

void PrefMemberBase::VerifyPref() const {
  VerifyValuePrefName();
  if (!internal())
    UpdateValueFromPref();
}

PrefMemberBase::Internal::Internal()
    : thread_id_(BrowserThread::UI),
      is_managed_(false) {
}
PrefMemberBase::Internal::~Internal() { }

bool PrefMemberBase::Internal::IsOnCorrectThread() const {
  // In unit tests, there may not be a UI thread.
  return (BrowserThread::CurrentlyOn(thread_id_) ||
          (thread_id_ == BrowserThread::UI &&
           !BrowserThread::IsMessageLoopValid(BrowserThread::UI)));
}

void PrefMemberBase::Internal::UpdateValue(Value* v, bool is_managed) const {
  scoped_ptr<Value> value(v);
  if (IsOnCorrectThread()) {
    bool rv = UpdateValueInternal(*value);
    DCHECK(rv);
    is_managed_ = is_managed;
  } else {
    bool rv = BrowserThread::PostTask(
        thread_id_, FROM_HERE,
        base::Bind(&PrefMemberBase::Internal::UpdateValue, this,
                   value.release(), is_managed));
    DCHECK(rv);
  }
}

void PrefMemberBase::Internal::MoveToThread(BrowserThread::ID thread_id) {
  CheckOnCorrectThread();
  thread_id_ = thread_id;
}

}  // namespace subtle

template <>
void PrefMember<bool>::UpdatePref(const bool& value) {
  prefs()->SetBoolean(pref_name().c_str(), value);
}

template <>
bool PrefMember<bool>::Internal::UpdateValueInternal(const Value& value) const {
  return value.GetAsBoolean(&value_);
}

template <>
void PrefMember<int>::UpdatePref(const int& value) {
  prefs()->SetInteger(pref_name().c_str(), value);
}

template <>
bool PrefMember<int>::Internal::UpdateValueInternal(const Value& value) const {
  return value.GetAsInteger(&value_);
}

template <>
void PrefMember<double>::UpdatePref(const double& value) {
  prefs()->SetDouble(pref_name().c_str(), value);
}

template <>
bool PrefMember<double>::Internal::UpdateValueInternal(const Value& value)
    const {
  return value.GetAsDouble(&value_);
}

template <>
void PrefMember<std::string>::UpdatePref(const std::string& value) {
  prefs()->SetString(pref_name().c_str(), value);
}

template <>
bool PrefMember<std::string>::Internal::UpdateValueInternal(const Value& value)
    const {
  return value.GetAsString(&value_);
}

template <>
void PrefMember<FilePath>::UpdatePref(const FilePath& value) {
  prefs()->SetFilePath(pref_name().c_str(), value);
}

template <>
bool PrefMember<FilePath>::Internal::UpdateValueInternal(const Value& value)
    const {
  return base::GetValueAsFilePath(value, &value_);
}
