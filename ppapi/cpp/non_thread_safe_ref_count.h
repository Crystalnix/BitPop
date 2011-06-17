// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_CPP_NON_THREAD_SAFE_REF_COUNT_H_
#define PPAPI_CPP_NON_THREAD_SAFE_REF_COUNT_H_

#include "ppapi/cpp/core.h"
#include "ppapi/cpp/logging.h"
#include "ppapi/cpp/module.h"

namespace pp {

// Simple ref-count that isn't thread safe. Note: in Debug mode, it checks that
// it is either called on the main thread, or always called on another thread.
class NonThreadSafeRefCount {
 public:
  NonThreadSafeRefCount()
      : ref_(0) {
#ifndef NDEBUG
    is_main_thread_ = Module::Get()->core()->IsMainThread();
#endif
  }

  ~NonThreadSafeRefCount() {
    PP_DCHECK(is_main_thread_ == Module::Get()->core()->IsMainThread());
  }

  int32_t AddRef() {
    PP_DCHECK(is_main_thread_ == Module::Get()->core()->IsMainThread());
    return ++ref_;
  }

  int32_t Release() {
    PP_DCHECK(is_main_thread_ == Module::Get()->core()->IsMainThread());
    return --ref_;
  }

 private:
  int32_t ref_;
#ifndef NDEBUG
  bool is_main_thread_;
#endif
};

}  // namespace pp

#endif  // PPAPI_CPP_NON_THREAD_SAFE_REF_COUNT_H_
