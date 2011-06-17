// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/notification_registrar.h"

#include <algorithm>

#include "base/logging.h"
#include "base/threading/platform_thread.h"
#include "content/common/notification_service.h"

namespace {

void CheckCalledOnValidThread(base::PlatformThreadId thread_id) {
  base::PlatformThreadId current_thread_id = base::PlatformThread::CurrentId();
  CHECK(current_thread_id == thread_id) << "called on invalid thread: "
                                        << thread_id << " vs. "
                                        << current_thread_id;
}

}  // namespace

struct NotificationRegistrar::Record {
  bool operator==(const Record& other) const;

  NotificationObserver* observer;
  NotificationType type;
  NotificationSource source;
  base::PlatformThreadId thread_id;
};

bool NotificationRegistrar::Record::operator==(const Record& other) const {
  return observer == other.observer &&
         type == other.type &&
         source == other.source;
  // thread_id is for debugging purpose and thus not compared here.
}

NotificationRegistrar::NotificationRegistrar() {
  // Force the NotificationService to be constructed (if it isn't already).
  // This ensures the NotificationService will be registered on the
  // AtExitManager before any objects which access it via NotificationRegistrar.
  // This in turn means it will be destroyed after these objects, so they will
  // never try to access the NotificationService after it's been destroyed.
  NotificationService::current();
}

NotificationRegistrar::~NotificationRegistrar() {
  RemoveAll();
}

void NotificationRegistrar::Add(NotificationObserver* observer,
                                NotificationType type,
                                const NotificationSource& source) {
  DCHECK(!IsRegistered(observer, type, source)) << "Duplicate registration.";

  Record record = { observer, type, source, base::PlatformThread::CurrentId() };
  registered_.push_back(record);

  NotificationService::current()->AddObserver(observer, type, source);
}

void NotificationRegistrar::Remove(NotificationObserver* observer,
                                   NotificationType type,
                                   const NotificationSource& source) {
  if (!IsRegistered(observer, type, source)) {
    NOTREACHED() << "Trying to remove unregistered observer of type " <<
        type.value << " from list of size " << registered_.size() << ".";
    return;
  }

  Record record = { observer, type, source };
  RecordVector::iterator found = std::find(
      registered_.begin(), registered_.end(), record);
  CheckCalledOnValidThread(found->thread_id);
  registered_.erase(found);

  // This can be NULL if our owner outlives the NotificationService, e.g. if our
  // owner is a Singleton.
  NotificationService* service = NotificationService::current();
  if (service)
    service->RemoveObserver(observer, type, source);
}

void NotificationRegistrar::RemoveAll() {
  // Early-exit if no registrations, to avoid calling
  // NotificationService::current.  If we've constructed an object with a
  // NotificationRegistrar member, but haven't actually used the notification
  // service, and we reach prgram exit, then calling current() below could try
  // to initialize the service's lazy TLS pointer during exit, which throws
  // wrenches at things.
  if (registered_.empty())
    return;


  // This can be NULL if our owner outlives the NotificationService, e.g. if our
  // owner is a Singleton.
  NotificationService* service = NotificationService::current();
  if (service) {
    for (size_t i = 0; i < registered_.size(); i++) {
      CheckCalledOnValidThread(registered_[i].thread_id);
      service->RemoveObserver(registered_[i].observer,
                              registered_[i].type,
                              registered_[i].source);
    }
  }
  registered_.clear();
}

bool NotificationRegistrar::IsEmpty() const {
  return registered_.empty();
}

bool NotificationRegistrar::IsRegistered(NotificationObserver* observer,
                                         NotificationType type,
                                         const NotificationSource& source) {
  Record record = { observer, type, source };
  return std::find(registered_.begin(), registered_.end(), record) !=
      registered_.end();
}
