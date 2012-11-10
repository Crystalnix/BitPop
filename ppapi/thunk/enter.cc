// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/thunk/enter.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/stringprintf.h"
#include "base/synchronization/lock.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/shared_impl/ppapi_globals.h"
#include "ppapi/shared_impl/tracked_callback.h"
#include "ppapi/thunk/ppb_instance_api.h"
#include "ppapi/thunk/resource_creation_api.h"

namespace ppapi {
namespace {

bool IsMainThread() {
  return
      PpapiGlobals::Get()->GetMainThreadMessageLoop()->BelongsToCurrentThread();
}

}  // namespace

namespace thunk {

namespace subtle {

void AssertLockHeld() {
  base::Lock* proxy_lock = PpapiGlobals::Get()->GetProxyLock();
  // The lock is only valid in the plugin side of the proxy, so it only makes
  // sense to assert there. Otherwise, silently succeed.
  if (proxy_lock)
    proxy_lock->AssertAcquired();
}

EnterBase::EnterBase()
    : resource_(NULL),
      retval_(PP_OK) {
  // TODO(dmichael) validate that threads have an associated message loop.
}

EnterBase::EnterBase(PP_Resource resource)
    : resource_(GetResource(resource)),
      retval_(PP_OK) {
  // TODO(dmichael) validate that threads have an associated message loop.
}

EnterBase::EnterBase(PP_Resource resource,
                     const PP_CompletionCallback& callback)
    : resource_(GetResource(resource)),
      retval_(PP_OK) {
  callback_ = new TrackedCallback(resource_, callback);

  // TODO(dmichael) validate that threads have an associated message loop.
}

EnterBase::~EnterBase() {
  // callback_ is cleared any time it is run, scheduled to be run, or once we
  // know it will be completed asynchronously. So by this point it should be
  // NULL.
  DCHECK(!callback_);
}

int32_t EnterBase::SetResult(int32_t result) {
  if (!callback_) {
    // It doesn't make sense to call SetResult if there is no callback.
    NOTREACHED();
    retval_ = result;
    return result;
  }
  if (result == PP_OK_COMPLETIONPENDING) {
    retval_ = result;
    if (callback_->is_blocking()) {
      DCHECK(!IsMainThread());  // We should have returned an error before this.
      retval_ = callback_->BlockUntilComplete();
    } else {
      // The callback is not blocking and the operation will complete
      // asynchronously, so there's nothing to do.
      retval_ = result;
    }
  } else {
    // The function completed synchronously.
    if (callback_->is_required()) {
      // This is a required callback, so we must issue it asynchronously.
      // TODO(dmichael) make this work so that a call from a background thread
      // goes back to that thread.
      callback_->PostRun(result);
      retval_ = PP_OK_COMPLETIONPENDING;
    } else {
      // The callback is blocking or optional, so all we need to do is mark
      // the callback as completed so that it won't be issued later.
      callback_->MarkAsCompleted();
      retval_ = result;
    }
  }
  callback_ = NULL;
  return retval_;
}

// static
Resource* EnterBase::GetResource(PP_Resource resource) {
  return PpapiGlobals::Get()->GetResourceTracker()->GetResource(resource);
}

void EnterBase::SetStateForCallbackError(bool report_error) {
  if (!CallbackIsValid()) {
    callback_->MarkAsCompleted();
    callback_ = NULL;
    retval_ = PP_ERROR_BLOCKS_MAIN_THREAD;
    if (report_error) {
      std::string message(
          "Blocking callbacks are not allowed on the main thread.");
      PpapiGlobals::Get()->BroadcastLogWithSource(0, PP_LOGLEVEL_ERROR,
                                                  std::string(), message);
    }
  }
}

bool EnterBase::CallbackIsValid() const {
  // A callback is only considered invalid if it is blocking and we're on the
  // main thread.
  return !callback_ || !callback_->is_blocking() || !IsMainThread();
}

void EnterBase::ClearCallback() {
  callback_ = NULL;
}

void EnterBase::SetStateForResourceError(PP_Resource pp_resource,
                                         Resource* resource_base,
                                         void* object,
                                         bool report_error) {
  // Check for callback errors. If we get any, SetStateForCallbackError will
  // emit a log message. But we also want to check for resource errors. If there
  // are both kinds of errors, we'll emit two log messages and return
  // PP_ERROR_BADRESOURCE.
  SetStateForCallbackError(report_error);

  if (object)
    return;  // Everything worked.

  if (callback_ && callback_->is_required()) {
    // TODO(dmichael) make this work so that a call from a background thread
    // goes back to that thread.
    callback_->PostRun(static_cast<int32_t>(PP_ERROR_BADRESOURCE));
    callback_ = NULL;
    retval_ = PP_OK_COMPLETIONPENDING;
  } else {
    if (callback_)
      callback_->MarkAsCompleted();
    callback_ = NULL;
    retval_ = PP_ERROR_BADRESOURCE;
  }

  // We choose to silently ignore the error when the pp_resource is null
  // because this is a pretty common case and we don't want to have lots
  // of errors in the log. This should be an obvious case to debug.
  if (report_error && pp_resource) {
    std::string message;
    if (resource_base) {
      message = base::StringPrintf(
          "0x%X is not the correct type for this function.",
          pp_resource);
    } else {
      message = base::StringPrintf(
          "0x%X is not a valid resource ID.",
          pp_resource);
    }
    PpapiGlobals::Get()->BroadcastLogWithSource(0, PP_LOGLEVEL_ERROR,
                                                std::string(), message);
  }
}

void EnterBase::SetStateForFunctionError(PP_Instance pp_instance,
                                         void* object,
                                         bool report_error) {
  // Check for callback errors. If we get any, SetStateForCallbackError will
  // emit a log message. But we also want to check for instance errors. If there
  // are both kinds of errors, we'll emit two log messages and return
  // PP_ERROR_BADARGUMENT.
  SetStateForCallbackError(report_error);

  if (object)
    return;  // Everything worked.

  if (callback_ && callback_->is_required()) {
    callback_->PostRun(static_cast<int32_t>(PP_ERROR_BADARGUMENT));
    callback_ = NULL;
    retval_ = PP_OK_COMPLETIONPENDING;
  } else {
    if (callback_)
      callback_->MarkAsCompleted();
    callback_ = NULL;
    retval_ = PP_ERROR_BADARGUMENT;
  }

  // We choose to silently ignore the error when the pp_instance is null as
  // for PP_Resources above.
  if (report_error && pp_instance) {
    std::string message;
    message = base::StringPrintf(
        "0x%X is not a valid instance ID.",
        pp_instance);
    PpapiGlobals::Get()->BroadcastLogWithSource(0, PP_LOGLEVEL_ERROR,
                                                std::string(), message);
  }
}

}  // namespace subtle

EnterInstance::EnterInstance(PP_Instance instance)
    : EnterBase(),
      functions_(PpapiGlobals::Get()->GetInstanceAPI(instance)) {
  SetStateForFunctionError(instance, functions_, true);
}

EnterInstance::EnterInstance(PP_Instance instance,
                             const PP_CompletionCallback& callback)
    : EnterBase(0 /* resource */, callback),
      // TODO(dmichael): This means that the callback_ we get is not associated
      //                 even with the instance, but we should handle that for
      //                 MouseLock (maybe others?).
      functions_(PpapiGlobals::Get()->GetInstanceAPI(instance)) {
  SetStateForFunctionError(instance, functions_, true);
}

EnterInstance::~EnterInstance() {
}

EnterInstanceNoLock::EnterInstanceNoLock(PP_Instance instance)
    : EnterBase(),
      functions_(PpapiGlobals::Get()->GetInstanceAPI(instance)) {
  SetStateForFunctionError(instance, functions_, true);
}

EnterInstanceNoLock::EnterInstanceNoLock(
    PP_Instance instance,
    const PP_CompletionCallback& callback)
    : EnterBase(0 /* resource */, callback),
      // TODO(dmichael): This means that the callback_ we get is not associated
      //                 even with the instance, but we should handle that for
      //                 MouseLock (maybe others?).
      functions_(PpapiGlobals::Get()->GetInstanceAPI(instance)) {
  SetStateForFunctionError(instance, functions_, true);
}

EnterInstanceNoLock::~EnterInstanceNoLock() {
}

EnterResourceCreation::EnterResourceCreation(PP_Instance instance)
    : EnterBase(),
      functions_(PpapiGlobals::Get()->GetResourceCreationAPI(instance)) {
  SetStateForFunctionError(instance, functions_, true);
}

EnterResourceCreation::~EnterResourceCreation() {
}

EnterResourceCreationNoLock::EnterResourceCreationNoLock(PP_Instance instance)
    : EnterBase(),
      functions_(PpapiGlobals::Get()->GetResourceCreationAPI(instance)) {
  SetStateForFunctionError(instance, functions_, true);
}

EnterResourceCreationNoLock::~EnterResourceCreationNoLock() {
}

}  // namespace thunk
}  // namespace ppapi
