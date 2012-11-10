// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// TODO(satorux):
// - Handle "disconnected" signal.

#include "dbus/bus.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/message_loop_proxy.h"
#include "base/stl_util.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "base/time.h"
#include "dbus/exported_object.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "dbus/scoped_dbus_error.h"

namespace dbus {

namespace {

// The class is used for watching the file descriptor used for D-Bus
// communication.
class Watch : public base::MessagePumpLibevent::Watcher {
 public:
  Watch(DBusWatch* watch)
      : raw_watch_(watch) {
    dbus_watch_set_data(raw_watch_, this, NULL);
  }

  ~Watch() {
    dbus_watch_set_data(raw_watch_, NULL, NULL);
  }

  // Returns true if the underlying file descriptor is ready to be watched.
  bool IsReadyToBeWatched() {
    return dbus_watch_get_enabled(raw_watch_);
  }

  // Starts watching the underlying file descriptor.
  void StartWatching() {
    const int file_descriptor = dbus_watch_get_unix_fd(raw_watch_);
    const int flags = dbus_watch_get_flags(raw_watch_);

    MessageLoopForIO::Mode mode = MessageLoopForIO::WATCH_READ;
    if ((flags & DBUS_WATCH_READABLE) && (flags & DBUS_WATCH_WRITABLE))
      mode = MessageLoopForIO::WATCH_READ_WRITE;
    else if (flags & DBUS_WATCH_READABLE)
      mode = MessageLoopForIO::WATCH_READ;
    else if (flags & DBUS_WATCH_WRITABLE)
      mode = MessageLoopForIO::WATCH_WRITE;
    else
      NOTREACHED();

    const bool persistent = true;  // Watch persistently.
    const bool success = MessageLoopForIO::current()->WatchFileDescriptor(
        file_descriptor,
        persistent,
        mode,
        &file_descriptor_watcher_,
        this);
    CHECK(success) << "Unable to allocate memory";
  }

  // Stops watching the underlying file descriptor.
  void StopWatching() {
    file_descriptor_watcher_.StopWatchingFileDescriptor();
  }

 private:
  // Implement MessagePumpLibevent::Watcher.
  virtual void OnFileCanReadWithoutBlocking(int file_descriptor) {
    const bool success = dbus_watch_handle(raw_watch_, DBUS_WATCH_READABLE);
    CHECK(success) << "Unable to allocate memory";
  }

  // Implement MessagePumpLibevent::Watcher.
  virtual void OnFileCanWriteWithoutBlocking(int file_descriptor) {
    const bool success = dbus_watch_handle(raw_watch_, DBUS_WATCH_WRITABLE);
    CHECK(success) << "Unable to allocate memory";
  }

  DBusWatch* raw_watch_;
  base::MessagePumpLibevent::FileDescriptorWatcher file_descriptor_watcher_;
};

// The class is used for monitoring the timeout used for D-Bus method
// calls.
//
// Unlike Watch, Timeout is a ref counted object, to ensure that |this| of
// the object is is alive when HandleTimeout() is called. It's unlikely
// but it may be possible that HandleTimeout() is called after
// Bus::OnRemoveTimeout(). That's why we don't simply delete the object in
// Bus::OnRemoveTimeout().
class Timeout : public base::RefCountedThreadSafe<Timeout> {
 public:
  Timeout(DBusTimeout* timeout)
      : raw_timeout_(timeout),
        monitoring_is_active_(false),
        is_completed(false) {
    dbus_timeout_set_data(raw_timeout_, this, NULL);
    AddRef();  // Balanced on Complete().
  }

  // Returns true if the timeout is ready to be monitored.
  bool IsReadyToBeMonitored() {
    return dbus_timeout_get_enabled(raw_timeout_);
  }

  // Starts monitoring the timeout.
  void StartMonitoring(dbus::Bus* bus) {
    bus->PostDelayedTaskToDBusThread(FROM_HERE,
                                     base::Bind(&Timeout::HandleTimeout,
                                                this),
                                     GetInterval());
    monitoring_is_active_ = true;
  }

  // Stops monitoring the timeout.
  void StopMonitoring() {
    // We cannot take back the delayed task we posted in
    // StartMonitoring(), so we just mark the monitoring is inactive now.
    monitoring_is_active_ = false;
  }

  // Returns the interval.
  base::TimeDelta GetInterval() {
    return base::TimeDelta::FromMilliseconds(
        dbus_timeout_get_interval(raw_timeout_));
  }

  // Cleans up the raw_timeout and marks that timeout is completed.
  // See the class comment above for why we are doing this.
  void Complete() {
    dbus_timeout_set_data(raw_timeout_, NULL, NULL);
    is_completed = true;
    Release();
  }

 private:
  friend class base::RefCountedThreadSafe<Timeout>;
  ~Timeout() {
  }

  // Handles the timeout.
  void HandleTimeout() {
    // If the timeout is marked completed, we should do nothing. This can
    // occur if this function is called after Bus::OnRemoveTimeout().
    if (is_completed)
      return;
    // Skip if monitoring is canceled.
    if (!monitoring_is_active_)
      return;

    const bool success = dbus_timeout_handle(raw_timeout_);
    CHECK(success) << "Unable to allocate memory";
  }

  DBusTimeout* raw_timeout_;
  bool monitoring_is_active_;
  bool is_completed;
};

}  // namespace

Bus::Options::Options()
  : bus_type(SESSION),
    connection_type(PRIVATE) {
}

Bus::Options::~Options() {
}

Bus::Bus(const Options& options)
    : bus_type_(options.bus_type),
      connection_type_(options.connection_type),
      dbus_thread_message_loop_proxy_(options.dbus_thread_message_loop_proxy),
      on_shutdown_(false /* manual_reset */, false /* initially_signaled */),
      connection_(NULL),
      origin_thread_id_(base::PlatformThread::CurrentId()),
      async_operations_set_up_(false),
      shutdown_completed_(false),
      num_pending_watches_(0),
      num_pending_timeouts_(0),
      address_(options.address) {
  // This is safe to call multiple times.
  dbus_threads_init_default();
  // The origin message loop is unnecessary if the client uses synchronous
  // functions only.
  if (MessageLoop::current())
    origin_message_loop_proxy_ =  MessageLoop::current()->message_loop_proxy();
}

Bus::~Bus() {
  DCHECK(!connection_);
  DCHECK(owned_service_names_.empty());
  DCHECK(match_rules_added_.empty());
  DCHECK(filter_functions_added_.empty());
  DCHECK(registered_object_paths_.empty());
  DCHECK_EQ(0, num_pending_watches_);
  // TODO(satorux): This check fails occasionally in browser_tests for tests
  // that run very quickly. Perhaps something does not have time to clean up.
  // Despite the check failing, the tests seem to run fine. crosbug.com/23416
  // DCHECK_EQ(0, num_pending_timeouts_);
}

ObjectProxy* Bus::GetObjectProxy(const std::string& service_name,
                                 const ObjectPath& object_path) {
  return GetObjectProxyWithOptions(service_name, object_path,
                                   ObjectProxy::DEFAULT_OPTIONS);
}

ObjectProxy* Bus::GetObjectProxyWithOptions(const std::string& service_name,
                                            const dbus::ObjectPath& object_path,
                                            int options) {
  AssertOnOriginThread();

  // Check if we already have the requested object proxy.
  const ObjectProxyTable::key_type key(service_name + object_path.value(),
                                       options);
  ObjectProxyTable::iterator iter = object_proxy_table_.find(key);
  if (iter != object_proxy_table_.end()) {
    return iter->second;
  }

  scoped_refptr<ObjectProxy> object_proxy =
      new ObjectProxy(this, service_name, object_path, options);
  object_proxy_table_[key] = object_proxy;

  return object_proxy.get();
}

ExportedObject* Bus::GetExportedObject(const ObjectPath& object_path) {
  AssertOnOriginThread();

  // Check if we already have the requested exported object.
  ExportedObjectTable::iterator iter = exported_object_table_.find(object_path);
  if (iter != exported_object_table_.end()) {
    return iter->second;
  }

  scoped_refptr<ExportedObject> exported_object =
      new ExportedObject(this, object_path);
  exported_object_table_[object_path] = exported_object;

  return exported_object.get();
}

void Bus::UnregisterExportedObject(const ObjectPath& object_path) {
  AssertOnOriginThread();

  // Remove the registered object from the table first, to allow a new
  // GetExportedObject() call to return a new object, rather than this one.
  ExportedObjectTable::iterator iter = exported_object_table_.find(object_path);
  if (iter == exported_object_table_.end())
    return;

  scoped_refptr<ExportedObject> exported_object = iter->second;
  exported_object_table_.erase(iter);

  // Post the task to perform the final unregistration to the D-Bus thread.
  // Since the registration also happens on the D-Bus thread in
  // TryRegisterObjectPath(), and the message loop proxy we post to is a
  // MessageLoopProxy which inherits from SequencedTaskRunner, there is a
  // guarantee that this will happen before any future registration call.
  PostTaskToDBusThread(FROM_HERE, base::Bind(
      &Bus::UnregisterExportedObjectInternal,
      this, exported_object));
}

void Bus::UnregisterExportedObjectInternal(
    scoped_refptr<dbus::ExportedObject> exported_object) {
  AssertOnDBusThread();

  exported_object->Unregister();
}

bool Bus::Connect() {
  // dbus_bus_get_private() and dbus_bus_get() are blocking calls.
  AssertOnDBusThread();

  // Check if it's already initialized.
  if (connection_)
    return true;

  ScopedDBusError error;
  if (bus_type_ == CUSTOM_ADDRESS) {
    if (connection_type_ == PRIVATE) {
      connection_ = dbus_connection_open_private(address_.c_str(), error.get());
    } else {
      connection_ = dbus_connection_open(address_.c_str(), error.get());
    }
  } else {
    const DBusBusType dbus_bus_type = static_cast<DBusBusType>(bus_type_);
    if (connection_type_ == PRIVATE) {
      connection_ = dbus_bus_get_private(dbus_bus_type, error.get());
    } else {
      connection_ = dbus_bus_get(dbus_bus_type, error.get());
    }
  }
  if (!connection_) {
    LOG(ERROR) << "Failed to connect to the bus: "
               << (dbus_error_is_set(error.get()) ? error.message() : "");
    return false;
  }
  // We shouldn't exit on the disconnected signal.
  dbus_connection_set_exit_on_disconnect(connection_, false);

  return true;
}

void Bus::ShutdownAndBlock() {
  AssertOnDBusThread();

  // Unregister the exported objects.
  for (ExportedObjectTable::iterator iter = exported_object_table_.begin();
       iter != exported_object_table_.end(); ++iter) {
    iter->second->Unregister();
  }

  // Release all service names.
  for (std::set<std::string>::iterator iter = owned_service_names_.begin();
       iter != owned_service_names_.end();) {
    // This is a bit tricky but we should increment the iter here as
    // ReleaseOwnership() may remove |service_name| from the set.
    const std::string& service_name = *iter++;
    ReleaseOwnership(service_name);
  }
  if (!owned_service_names_.empty()) {
    LOG(ERROR) << "Failed to release all service names. # of services left: "
               << owned_service_names_.size();
  }

  // Detach from the remote objects.
  for (ObjectProxyTable::iterator iter = object_proxy_table_.begin();
       iter != object_proxy_table_.end(); ++iter) {
    iter->second->Detach();
  }

  // Release object proxies and exported objects here. We should do this
  // here rather than in the destructor to avoid memory leaks due to
  // cyclic references.
  object_proxy_table_.clear();
  exported_object_table_.clear();

  // Private connection should be closed.
  if (connection_) {
    if (connection_type_ == PRIVATE)
      dbus_connection_close(connection_);
    // dbus_connection_close() won't unref.
    dbus_connection_unref(connection_);
  }

  connection_ = NULL;
  shutdown_completed_ = true;
}

void Bus::ShutdownOnDBusThreadAndBlock() {
  AssertOnOriginThread();
  DCHECK(dbus_thread_message_loop_proxy_.get());

  PostTaskToDBusThread(FROM_HERE, base::Bind(
      &Bus::ShutdownOnDBusThreadAndBlockInternal,
      this));

  // http://crbug.com/125222
  base::ThreadRestrictions::ScopedAllowWait allow_wait;

  // Wait until the shutdown is complete on the D-Bus thread.
  // The shutdown should not hang, but set timeout just in case.
  const int kTimeoutSecs = 3;
  const base::TimeDelta timeout(base::TimeDelta::FromSeconds(kTimeoutSecs));
  const bool signaled = on_shutdown_.TimedWait(timeout);
  LOG_IF(ERROR, !signaled) << "Failed to shutdown the bus";
}

void Bus::RequestOwnership(const std::string& service_name,
                           OnOwnershipCallback on_ownership_callback) {
  AssertOnOriginThread();

  PostTaskToDBusThread(FROM_HERE, base::Bind(
      &Bus::RequestOwnershipInternal,
      this, service_name, on_ownership_callback));
}

void Bus::RequestOwnershipInternal(const std::string& service_name,
                                   OnOwnershipCallback on_ownership_callback) {
  AssertOnDBusThread();

  bool success = Connect();
  if (success)
    success = RequestOwnershipAndBlock(service_name);

  PostTaskToOriginThread(FROM_HERE,
                         base::Bind(&Bus::OnOwnership,
                                    this,
                                    on_ownership_callback,
                                    service_name,
                                    success));
}

void Bus::OnOwnership(OnOwnershipCallback on_ownership_callback,
                      const std::string& service_name,
                      bool success) {
  AssertOnOriginThread();

  on_ownership_callback.Run(service_name, success);
}

bool Bus::RequestOwnershipAndBlock(const std::string& service_name) {
  DCHECK(connection_);
  // dbus_bus_request_name() is a blocking call.
  AssertOnDBusThread();

  // Check if we already own the service name.
  if (owned_service_names_.find(service_name) != owned_service_names_.end()) {
    return true;
  }

  ScopedDBusError error;
  const int result = dbus_bus_request_name(connection_,
                                           service_name.c_str(),
                                           DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                           error.get());
  if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
    LOG(ERROR) << "Failed to get the ownership of " << service_name << ": "
               << (dbus_error_is_set(error.get()) ? error.message() : "");
    return false;
  }
  owned_service_names_.insert(service_name);
  return true;
}

bool Bus::ReleaseOwnership(const std::string& service_name) {
  DCHECK(connection_);
  // dbus_bus_request_name() is a blocking call.
  AssertOnDBusThread();

  // Check if we already own the service name.
  std::set<std::string>::iterator found =
      owned_service_names_.find(service_name);
  if (found == owned_service_names_.end()) {
    LOG(ERROR) << service_name << " is not owned by the bus";
    return false;
  }

  ScopedDBusError error;
  const int result = dbus_bus_release_name(connection_, service_name.c_str(),
                                           error.get());
  if (result == DBUS_RELEASE_NAME_REPLY_RELEASED) {
    owned_service_names_.erase(found);
    return true;
  } else {
    LOG(ERROR) << "Failed to release the ownership of " << service_name << ": "
               << (error.is_set() ? error.message() : "");
    return false;
  }
}

bool Bus::SetUpAsyncOperations() {
  DCHECK(connection_);
  AssertOnDBusThread();

  if (async_operations_set_up_)
    return true;

  // Process all the incoming data if any, so that OnDispatchStatus() will
  // be called when the incoming data is ready.
  ProcessAllIncomingDataIfAny();

  bool success = dbus_connection_set_watch_functions(connection_,
                                                     &Bus::OnAddWatchThunk,
                                                     &Bus::OnRemoveWatchThunk,
                                                     &Bus::OnToggleWatchThunk,
                                                     this,
                                                     NULL);
  CHECK(success) << "Unable to allocate memory";

  success = dbus_connection_set_timeout_functions(connection_,
                                                  &Bus::OnAddTimeoutThunk,
                                                  &Bus::OnRemoveTimeoutThunk,
                                                  &Bus::OnToggleTimeoutThunk,
                                                  this,
                                                  NULL);
  CHECK(success) << "Unable to allocate memory";

  dbus_connection_set_dispatch_status_function(
      connection_,
      &Bus::OnDispatchStatusChangedThunk,
      this,
      NULL);

  async_operations_set_up_ = true;

  return true;
}

DBusMessage* Bus::SendWithReplyAndBlock(DBusMessage* request,
                                        int timeout_ms,
                                        DBusError* error) {
  DCHECK(connection_);
  AssertOnDBusThread();

  return dbus_connection_send_with_reply_and_block(
      connection_, request, timeout_ms, error);
}

void Bus::SendWithReply(DBusMessage* request,
                        DBusPendingCall** pending_call,
                        int timeout_ms) {
  DCHECK(connection_);
  AssertOnDBusThread();

  const bool success = dbus_connection_send_with_reply(
      connection_, request, pending_call, timeout_ms);
  CHECK(success) << "Unable to allocate memory";
}

void Bus::Send(DBusMessage* request, uint32* serial) {
  DCHECK(connection_);
  AssertOnDBusThread();

  const bool success = dbus_connection_send(connection_, request, serial);
  CHECK(success) << "Unable to allocate memory";
}

bool Bus::AddFilterFunction(DBusHandleMessageFunction filter_function,
                            void* user_data) {
  DCHECK(connection_);
  AssertOnDBusThread();

  std::pair<DBusHandleMessageFunction, void*> filter_data_pair =
      std::make_pair(filter_function, user_data);
  if (filter_functions_added_.find(filter_data_pair) !=
      filter_functions_added_.end()) {
    VLOG(1) << "Filter function already exists: " << filter_function
            << " with associated data: " << user_data;
    return false;
  }

  const bool success = dbus_connection_add_filter(
      connection_, filter_function, user_data, NULL);
  CHECK(success) << "Unable to allocate memory";
  filter_functions_added_.insert(filter_data_pair);
  return true;
}

bool Bus::RemoveFilterFunction(DBusHandleMessageFunction filter_function,
                               void* user_data) {
  DCHECK(connection_);
  AssertOnDBusThread();

  std::pair<DBusHandleMessageFunction, void*> filter_data_pair =
      std::make_pair(filter_function, user_data);
  if (filter_functions_added_.find(filter_data_pair) ==
      filter_functions_added_.end()) {
    VLOG(1) << "Requested to remove an unknown filter function: "
            << filter_function
            << " with associated data: " << user_data;
    return false;
  }

  dbus_connection_remove_filter(connection_, filter_function, user_data);
  filter_functions_added_.erase(filter_data_pair);
  return true;
}

void Bus::AddMatch(const std::string& match_rule, DBusError* error) {
  DCHECK(connection_);
  AssertOnDBusThread();

  if (match_rules_added_.find(match_rule) != match_rules_added_.end()) {
    VLOG(1) << "Match rule already exists: " << match_rule;
    return;
  }

  dbus_bus_add_match(connection_, match_rule.c_str(), error);
  match_rules_added_.insert(match_rule);
}

void Bus::RemoveMatch(const std::string& match_rule, DBusError* error) {
  DCHECK(connection_);
  AssertOnDBusThread();

  if (match_rules_added_.find(match_rule) == match_rules_added_.end()) {
    LOG(ERROR) << "Requested to remove an unknown match rule: " << match_rule;
    return;
  }

  dbus_bus_remove_match(connection_, match_rule.c_str(), error);
  match_rules_added_.erase(match_rule);
}

bool Bus::TryRegisterObjectPath(const ObjectPath& object_path,
                                const DBusObjectPathVTable* vtable,
                                void* user_data,
                                DBusError* error) {
  DCHECK(connection_);
  AssertOnDBusThread();

  if (registered_object_paths_.find(object_path) !=
      registered_object_paths_.end()) {
    LOG(ERROR) << "Object path already registered: " << object_path.value();
    return false;
  }

  const bool success = dbus_connection_try_register_object_path(
      connection_,
      object_path.value().c_str(),
      vtable,
      user_data,
      error);
  if (success)
    registered_object_paths_.insert(object_path);
  return success;
}

void Bus::UnregisterObjectPath(const ObjectPath& object_path) {
  DCHECK(connection_);
  AssertOnDBusThread();

  if (registered_object_paths_.find(object_path) ==
      registered_object_paths_.end()) {
    LOG(ERROR) << "Requested to unregister an unknown object path: "
               << object_path.value();
    return;
  }

  const bool success = dbus_connection_unregister_object_path(
      connection_,
      object_path.value().c_str());
  CHECK(success) << "Unable to allocate memory";
  registered_object_paths_.erase(object_path);
}

void Bus::ShutdownOnDBusThreadAndBlockInternal() {
  AssertOnDBusThread();

  ShutdownAndBlock();
  on_shutdown_.Signal();
}

void Bus::ProcessAllIncomingDataIfAny() {
  AssertOnDBusThread();

  // As mentioned at the class comment in .h file, connection_ can be NULL.
  if (!connection_ || !dbus_connection_get_is_connected(connection_))
    return;

  if (dbus_connection_get_dispatch_status(connection_) ==
      DBUS_DISPATCH_DATA_REMAINS) {
    while (dbus_connection_dispatch(connection_) ==
           DBUS_DISPATCH_DATA_REMAINS);
  }
}

void Bus::PostTaskToOriginThread(const tracked_objects::Location& from_here,
                                 const base::Closure& task) {
  DCHECK(origin_message_loop_proxy_.get());
  if (!origin_message_loop_proxy_->PostTask(from_here, task)) {
    LOG(WARNING) << "Failed to post a task to the origin message loop";
  }
}

void Bus::PostTaskToDBusThread(const tracked_objects::Location& from_here,
                               const base::Closure& task) {
  if (dbus_thread_message_loop_proxy_.get()) {
    if (!dbus_thread_message_loop_proxy_->PostTask(from_here, task)) {
      LOG(WARNING) << "Failed to post a task to the D-Bus thread message loop";
    }
  } else {
    DCHECK(origin_message_loop_proxy_.get());
    if (!origin_message_loop_proxy_->PostTask(from_here, task)) {
      LOG(WARNING) << "Failed to post a task to the origin message loop";
    }
  }
}

void Bus::PostDelayedTaskToDBusThread(
    const tracked_objects::Location& from_here,
    const base::Closure& task,
    base::TimeDelta delay) {
  if (dbus_thread_message_loop_proxy_.get()) {
    if (!dbus_thread_message_loop_proxy_->PostDelayedTask(
            from_here, task, delay)) {
      LOG(WARNING) << "Failed to post a task to the D-Bus thread message loop";
    }
  } else {
    DCHECK(origin_message_loop_proxy_.get());
    if (!origin_message_loop_proxy_->PostDelayedTask(
            from_here, task, delay)) {
      LOG(WARNING) << "Failed to post a task to the origin message loop";
    }
  }
}

bool Bus::HasDBusThread() {
  return dbus_thread_message_loop_proxy_.get() != NULL;
}

void Bus::AssertOnOriginThread() {
  DCHECK_EQ(origin_thread_id_, base::PlatformThread::CurrentId());
}

void Bus::AssertOnDBusThread() {
  base::ThreadRestrictions::AssertIOAllowed();

  if (dbus_thread_message_loop_proxy_.get()) {
    DCHECK(dbus_thread_message_loop_proxy_->BelongsToCurrentThread());
  } else {
    AssertOnOriginThread();
  }
}

dbus_bool_t Bus::OnAddWatch(DBusWatch* raw_watch) {
  AssertOnDBusThread();

  // watch will be deleted when raw_watch is removed in OnRemoveWatch().
  Watch* watch = new Watch(raw_watch);
  if (watch->IsReadyToBeWatched()) {
    watch->StartWatching();
  }
  ++num_pending_watches_;
  return true;
}

void Bus::OnRemoveWatch(DBusWatch* raw_watch) {
  AssertOnDBusThread();

  Watch* watch = static_cast<Watch*>(dbus_watch_get_data(raw_watch));
  delete watch;
  --num_pending_watches_;
}

void Bus::OnToggleWatch(DBusWatch* raw_watch) {
  AssertOnDBusThread();

  Watch* watch = static_cast<Watch*>(dbus_watch_get_data(raw_watch));
  if (watch->IsReadyToBeWatched()) {
    watch->StartWatching();
  } else {
    // It's safe to call this if StartWatching() wasn't called, per
    // message_pump_libevent.h.
    watch->StopWatching();
  }
}

dbus_bool_t Bus::OnAddTimeout(DBusTimeout* raw_timeout) {
  AssertOnDBusThread();

  // timeout will be deleted when raw_timeout is removed in
  // OnRemoveTimeoutThunk().
  Timeout* timeout = new Timeout(raw_timeout);
  if (timeout->IsReadyToBeMonitored()) {
    timeout->StartMonitoring(this);
  }
  ++num_pending_timeouts_;
  return true;
}

void Bus::OnRemoveTimeout(DBusTimeout* raw_timeout) {
  AssertOnDBusThread();

  Timeout* timeout = static_cast<Timeout*>(dbus_timeout_get_data(raw_timeout));
  timeout->Complete();
  --num_pending_timeouts_;
}

void Bus::OnToggleTimeout(DBusTimeout* raw_timeout) {
  AssertOnDBusThread();

  Timeout* timeout = static_cast<Timeout*>(dbus_timeout_get_data(raw_timeout));
  if (timeout->IsReadyToBeMonitored()) {
    timeout->StartMonitoring(this);
  } else {
    timeout->StopMonitoring();
  }
}

void Bus::OnDispatchStatusChanged(DBusConnection* connection,
                                  DBusDispatchStatus status) {
  DCHECK_EQ(connection, connection_);
  AssertOnDBusThread();

  if (!dbus_connection_get_is_connected(connection))
    return;

  // We cannot call ProcessAllIncomingDataIfAny() here, as calling
  // dbus_connection_dispatch() inside DBusDispatchStatusFunction is
  // prohibited by the D-Bus library. Hence, we post a task here instead.
  // See comments for dbus_connection_set_dispatch_status_function().
  PostTaskToDBusThread(FROM_HERE,
                       base::Bind(&Bus::ProcessAllIncomingDataIfAny,
                                  this));
}

dbus_bool_t Bus::OnAddWatchThunk(DBusWatch* raw_watch, void* data) {
  Bus* self = static_cast<Bus*>(data);
  return self->OnAddWatch(raw_watch);
}

void Bus::OnRemoveWatchThunk(DBusWatch* raw_watch, void* data) {
  Bus* self = static_cast<Bus*>(data);
  self->OnRemoveWatch(raw_watch);
}

void Bus::OnToggleWatchThunk(DBusWatch* raw_watch, void* data) {
  Bus* self = static_cast<Bus*>(data);
  self->OnToggleWatch(raw_watch);
}

dbus_bool_t Bus::OnAddTimeoutThunk(DBusTimeout* raw_timeout, void* data) {
  Bus* self = static_cast<Bus*>(data);
  return self->OnAddTimeout(raw_timeout);
}

void Bus::OnRemoveTimeoutThunk(DBusTimeout* raw_timeout, void* data) {
  Bus* self = static_cast<Bus*>(data);
  self->OnRemoveTimeout(raw_timeout);
}

void Bus::OnToggleTimeoutThunk(DBusTimeout* raw_timeout, void* data) {
  Bus* self = static_cast<Bus*>(data);
  self->OnToggleTimeout(raw_timeout);
}

void Bus::OnDispatchStatusChangedThunk(DBusConnection* connection,
                                       DBusDispatchStatus status,
                                       void* data) {
  Bus* self = static_cast<Bus*>(data);
  self->OnDispatchStatusChanged(connection, status);
}

}  // namespace dbus
