// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/chrome_v8_context.h"

#include "base/logging.h"
#include "base/string_split.h"
#include "base/values.h"
#include "chrome/common/extensions/extension_set.h"
#include "chrome/renderer/extensions/chrome_v8_extension.h"
#include "content/public/renderer/render_view.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "v8/include/v8.h"

namespace {

const char kChromeHidden[] = "chromeHidden";

#ifndef NDEBUG
const char kValidateCallbacks[] = "validateCallbacks";
#endif

}  // namespace


ChromeV8Context::ChromeV8Context(v8::Handle<v8::Context> v8_context,
                                 WebKit::WebFrame* web_frame,
                                 const std::string& extension_id)
    : v8_context_(v8::Persistent<v8::Context>::New(v8_context)),
      web_frame_(web_frame),
      extension_id_(extension_id) {
  VLOG(1) << "Created context for extension\n"
          << "  id:    " << extension_id << "\n"
          << "  frame: " << web_frame_;
}

ChromeV8Context::~ChromeV8Context() {
  VLOG(1) << "Destroyed context for extension\n"
          << "  id:    " << extension_id_;
  v8_context_.Dispose();
}

// static
v8::Handle<v8::Value> ChromeV8Context::GetOrCreateChromeHidden(
    v8::Handle<v8::Context> context) {
  v8::Local<v8::Object> global = context->Global();
  v8::Local<v8::Value> hidden = global->GetHiddenValue(
      v8::String::New(kChromeHidden));

  if (hidden.IsEmpty() || hidden->IsUndefined()) {
    hidden = v8::Object::New();
    global->SetHiddenValue(v8::String::New(kChromeHidden), hidden);

#ifndef NDEBUG
    // Tell schema_generated_bindings.js to validate callbacks and events
    // against their schema definitions.
    v8::Local<v8::Object>::Cast(hidden)
        ->Set(v8::String::New(kValidateCallbacks), v8::True());
#endif
  }

  DCHECK(hidden->IsObject());
  return v8::Local<v8::Object>::Cast(hidden);
}

v8::Handle<v8::Value> ChromeV8Context::GetChromeHidden() const {
  v8::Local<v8::Object> global = v8_context_->Global();
  return global->GetHiddenValue(v8::String::New(kChromeHidden));
}

content::RenderView* ChromeV8Context::GetRenderView() const {
  if (web_frame_ && web_frame_->view())
    return content::RenderView::FromWebView(web_frame_->view());
  else
    return NULL;
}

bool ChromeV8Context::CallChromeHiddenMethod(
    const std::string& function_name,
    int argc,
    v8::Handle<v8::Value>* argv,
    v8::Handle<v8::Value>* result) const {
  v8::Context::Scope context_scope(v8_context_);

  // Look up the function name, which may be a sub-property like
  // "Port.dispatchOnMessage" in the hidden global variable.
  v8::Local<v8::Value> value = v8::Local<v8::Value>::New(GetChromeHidden());
  if (value.IsEmpty())
    return false;

  std::vector<std::string> components;
  base::SplitStringDontTrim(function_name, '.', &components);
  for (size_t i = 0; i < components.size(); ++i) {
    if (!value.IsEmpty() && value->IsObject()) {
      value = v8::Local<v8::Object>::Cast(value)->Get(
          v8::String::New(components[i].c_str()));
    }
  }

  if (value.IsEmpty() || !value->IsFunction()) {
    VLOG(1) << "Could not execute chrome hidden method: " << function_name;
    return false;
  }

  v8::Handle<v8::Value> result_temp =
      v8::Local<v8::Function>::Cast(value)->Call(v8::Object::New(), argc, argv);
  if (result)
    *result = result_temp;
  return true;
}

void ChromeV8Context::DispatchOnLoadEvent(bool is_extension_process,
                                          bool is_incognito_process,
                                          int manifest_version) const {
  v8::HandleScope handle_scope;
  v8::Handle<v8::Value> argv[4];
  argv[0] = v8::String::New(extension_id_.c_str());
  argv[1] = v8::Boolean::New(is_extension_process);
  argv[2] = v8::Boolean::New(is_incognito_process);
  argv[3] = v8::Integer::New(manifest_version);
  CallChromeHiddenMethod("dispatchOnLoad", arraysize(argv), argv, NULL);
}

void ChromeV8Context::DispatchOnUnloadEvent() const {
  v8::HandleScope handle_scope;
  CallChromeHiddenMethod("dispatchOnUnload", 0, NULL, NULL);
}
