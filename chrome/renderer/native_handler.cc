// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/native_handler.h"

#include "base/memory/linked_ptr.h"
#include "base/logging.h"
#include "chrome/renderer/module_system.h"
#include "v8/include/v8.h"

NativeHandler::NativeHandler()
    : object_template_(
        v8::Persistent<v8::ObjectTemplate>::New(v8::ObjectTemplate::New())) {
}

NativeHandler::~NativeHandler() {
  object_template_.Dispose();
}

v8::Handle<v8::Object> NativeHandler::NewInstance() {
  return object_template_->NewInstance();
}

// static
v8::Handle<v8::Value> NativeHandler::Router(const v8::Arguments& args) {
  // It is possible for JS code to execute after ModuleSystem has been deleted
  // in which case the native handlers will also have been deleted, making
  // HandlerFunction below point to freed memory.
  if (!ModuleSystem::IsPresentInCurrentContext()) {
    return v8::ThrowException(v8::Exception::Error(
        v8::String::New("ModuleSystem has been deleted")));
  }
  HandlerFunction* handler_function = static_cast<HandlerFunction*>(
      args.Data().As<v8::External>()->Value());
  return handler_function->Run(args);
}

void NativeHandler::RouteFunction(const std::string& name,
    const HandlerFunction& handler_function) {
  linked_ptr<HandlerFunction> function(new HandlerFunction(handler_function));
  // TODO(koz): Investigate using v8's MakeWeak() function instead of holding
  // on to these pointers here.
  handler_functions_.push_back(function);
  v8::Handle<v8::FunctionTemplate> function_template =
      v8::FunctionTemplate::New(Router,
          v8::External::New(function.get()));
  object_template_->Set(name.c_str(), function_template);
}

void NativeHandler::RouteStaticFunction(const std::string& name,
                                        const HandlerFunc handler_func) {
  v8::Handle<v8::FunctionTemplate> function_template =
      v8::FunctionTemplate::New(handler_func, v8::External::New(this));
  object_template_->Set(name.c_str(), function_template);
}
