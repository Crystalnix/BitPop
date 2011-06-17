// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/bindings_utils.h"

#include "base/lazy_instance.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "content/renderer/render_view.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"

using WebKit::WebFrame;
using WebKit::WebView;

namespace bindings_utils {

const char* kChromeHidden = "chromeHidden";
const char* kValidateCallbacks = "validateCallbacks";

struct SingletonData {
  ContextList contexts;
  PendingRequestMap pending_requests;
};
static base::LazyInstance<SingletonData> g_singleton_data(
    base::LINKER_INITIALIZED);

typedef std::map<int, std::string> StringMap;
static base::LazyInstance<StringMap> g_string_map(base::LINKER_INITIALIZED);

const char* GetStringResource(int resource_id) {
  StringMap* strings = g_string_map.Pointer();
  StringMap::iterator it = strings->find(resource_id);
  if (it == strings->end()) {
    it = strings->insert(std::make_pair(
        resource_id,
        ResourceBundle::GetSharedInstance().GetRawDataResource(
            resource_id).as_string())).first;
  }
  return it->second.c_str();
}

// ExtensionBase

v8::Handle<v8::FunctionTemplate>
    ExtensionBase::GetNativeFunction(v8::Handle<v8::String> name) {
  if (name->Equals(v8::String::New("GetChromeHidden"))) {
    return v8::FunctionTemplate::New(GetChromeHidden);
  }

  return v8::Handle<v8::FunctionTemplate>();
}

v8::Handle<v8::Value> ExtensionBase::GetChromeHidden(
    const v8::Arguments& args) {
  v8::Local<v8::Context> context = v8::Context::GetCurrent();
  v8::Local<v8::Object> global = context->Global();
  v8::Local<v8::Value> hidden = global->GetHiddenValue(
      v8::String::New(kChromeHidden));

  if (hidden.IsEmpty() || hidden->IsUndefined()) {
    hidden = v8::Object::New();
    global->SetHiddenValue(v8::String::New(kChromeHidden), hidden);

#ifndef NDEBUG
    // Tell extension_process_bindings.js to validate callbacks and events
    // against their schema definitions in api/extension_api.json.
    v8::Local<v8::Object>::Cast(hidden)
        ->Set(v8::String::New(kValidateCallbacks), v8::True());
#endif
  }

  DCHECK(hidden->IsObject());
  return hidden;
}

ContextInfo::ContextInfo(v8::Persistent<v8::Context> context,
                         const std::string& extension_id,
                         WebKit::WebFrame* parent_frame,
                         RenderView* render_view)
    : context(context),
      extension_id(extension_id),
      parent_frame(parent_frame),
      render_view(render_view),
      num_connected_events(0) {
}

ContextInfo::~ContextInfo() {}

ContextList& GetContexts() {
  return g_singleton_data.Get().contexts;
}

ContextList GetContextsForExtension(const std::string& extension_id) {
  ContextList& all_contexts = GetContexts();
  ContextList contexts;

  for (ContextList::iterator it = all_contexts.begin();
       it != all_contexts.end(); ++it) {
     if ((*it)->extension_id == extension_id)
       contexts.push_back(*it);
  }

  return contexts;
}

ContextInfo* GetInfoForCurrentContext() {
  // This can happen in testing scenarios and v8::Context::GetCurrent() crashes
  // if there is no JavaScript currently running.
  if (!v8::Context::InContext())
    return NULL;

  v8::Local<v8::Context> context = v8::Context::GetCurrent();
  ContextList::iterator context_iter = FindContext(context);
  if (context_iter == GetContexts().end())
    return NULL;
  else
    return context_iter->get();
}

PendingRequest::PendingRequest(v8::Persistent<v8::Context> context,
                               const std::string& name)
    : context(context), name(name) {
}

PendingRequest::~PendingRequest() {}

ContextList::iterator FindContext(v8::Handle<v8::Context> context) {
  ContextList& all_contexts = GetContexts();

  ContextList::iterator it = all_contexts.begin();
  for (; it != all_contexts.end(); ++it) {
    if ((*it)->context == context)
      break;
  }

  return it;
}

PendingRequestMap& GetPendingRequestMap() {
  return g_singleton_data.Get().pending_requests;
}

RenderView* GetRenderViewForCurrentContext() {
  WebFrame* webframe = WebFrame::frameForCurrentContext();
  DCHECK(webframe) << "RetrieveCurrentFrame called when not in a V8 context.";
  if (!webframe)
    return NULL;

  WebView* webview = webframe->view();
  if (!webview)
    return NULL;  // can happen during closing

  RenderView* renderview = RenderView::FromWebView(webview);
  DCHECK(renderview) << "Encountered a WebView without a WebViewDelegate";
  return renderview;
}

v8::Handle<v8::Value> CallFunctionInContext(v8::Handle<v8::Context> context,
    const std::string& function_name, int argc,
    v8::Handle<v8::Value>* argv) {
  v8::Context::Scope context_scope(context);

  // Look up the function name, which may be a sub-property like
  // "Port.dispatchOnMessage" in the hidden global variable.
  v8::Local<v8::Value> value =
      context->Global()->GetHiddenValue(v8::String::New(kChromeHidden));
  std::vector<std::string> components;
  base::SplitStringDontTrim(function_name, '.', &components);
  for (size_t i = 0; i < components.size(); ++i) {
    if (!value.IsEmpty() && value->IsObject())
      value = value->ToObject()->Get(v8::String::New(components[i].c_str()));
  }
  if (value.IsEmpty() || !value->IsFunction()) {
    NOTREACHED();
    return v8::Undefined();
  }

  v8::Local<v8::Function> function = v8::Local<v8::Function>::Cast(value);
  if (!function.IsEmpty())
    return function->Call(v8::Object::New(), argc, argv);

  return v8::Undefined();
}

}  // namespace bindings_utils
