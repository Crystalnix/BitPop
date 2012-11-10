// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/extension_custom_bindings.h"

#include <string>

#include "base/string_util.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_action.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/url_constants.h"
#include "chrome/common/view_type.h"
#include "chrome/renderer/extensions/extension_dispatcher.h"
#include "chrome/renderer/extensions/extension_helper.h"
#include "content/public/renderer/render_view.h"
#include "grit/renderer_resources.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "webkit/glue/webkit_glue.h"
#include "v8/include/v8.h"

namespace extensions {

namespace {

}  // namespace

ExtensionCustomBindings::ExtensionCustomBindings(
    ExtensionDispatcher* extension_dispatcher)
    : ChromeV8Extension(extension_dispatcher) {
  RouteStaticFunction("GetExtensionViews", &GetExtensionViews);
  RouteStaticFunction("OpenChannelToExtension", &OpenChannelToExtension);
}

// static
v8::Handle<v8::Value> ExtensionCustomBindings::GetExtensionViews(
    const v8::Arguments& args) {
  if (args.Length() != 2)
    return v8::Undefined();

  if (!args[0]->IsInt32() || !args[1]->IsString())
    return v8::Undefined();

  // |browser_window_id| == extension_misc::kUnknownWindowId means getting
  // views attached to any browser window.
  int browser_window_id = args[0]->Int32Value();

  std::string view_type_string = *v8::String::Utf8Value(args[1]->ToString());
  StringToUpperASCII(&view_type_string);
  // |view_type| == chrome::VIEW_TYPE_INVALID means getting any type of
  // views.
  chrome::ViewType view_type = chrome::VIEW_TYPE_INVALID;
  if (view_type_string == chrome::kViewTypeBackgroundPage) {
    view_type = chrome::VIEW_TYPE_EXTENSION_BACKGROUND_PAGE;
  } else if (view_type_string == chrome::kViewTypeInfobar) {
    view_type = chrome::VIEW_TYPE_EXTENSION_INFOBAR;
  } else if (view_type_string == chrome::kViewTypeNotification) {
    view_type = chrome::VIEW_TYPE_NOTIFICATION;
  } else if (view_type_string == chrome::kViewTypeTabContents) {
    view_type = chrome::VIEW_TYPE_TAB_CONTENTS;
  } else if (view_type_string == chrome::kViewTypePopup) {
    view_type = chrome::VIEW_TYPE_EXTENSION_POPUP;
  } else if (view_type_string == chrome::kViewTypeExtensionDialog) {
    view_type = chrome::VIEW_TYPE_EXTENSION_DIALOG;
  } else if (view_type_string == chrome::kViewTypeAppShell) {
    view_type = chrome::VIEW_TYPE_APP_SHELL;
  } else if (view_type_string == chrome::kViewTypePanel) {
    view_type = chrome::VIEW_TYPE_PANEL;
  } else if (view_type_string != chrome::kViewTypeAll) {
    return v8::Undefined();
  }

  ExtensionCustomBindings* v8_extension =
      GetFromArguments<ExtensionCustomBindings>(args);
  const Extension* extension =
      v8_extension->GetExtensionForCurrentRenderView();
  if (!extension)
    return v8::Undefined();

  std::vector<content::RenderView*> views = ExtensionHelper::GetExtensionViews(
      extension->id(), browser_window_id, view_type);
  v8::Local<v8::Array> v8_views = v8::Array::New();
  int v8_index = 0;
  for (size_t i = 0; i < views.size(); ++i) {
    v8::Local<v8::Context> context =
        views[i]->GetWebView()->mainFrame()->mainWorldScriptContext();
    if (!context.IsEmpty()) {
      v8::Local<v8::Value> window = context->Global();
      DCHECK(!window.IsEmpty());
      v8_views->Set(v8::Integer::New(v8_index++), window);
    }
  }

  return v8_views;
}

// static
v8::Handle<v8::Value> ExtensionCustomBindings::OpenChannelToExtension(
    const v8::Arguments& args) {
  // Get the current RenderView so that we can send a routed IPC message from
  // the correct source.
  content::RenderView* renderview = GetCurrentRenderView();
  if (!renderview)
    return v8::Undefined();

  // The Javascript code should validate/fill the arguments.
  CHECK(args.Length() >= 3 &&
        args[0]->IsString() &&
        args[1]->IsString() &&
        args[2]->IsString());

  std::string source_id = *v8::String::Utf8Value(args[0]->ToString());
  std::string target_id = *v8::String::Utf8Value(args[1]->ToString());
  std::string channel_name = *v8::String::Utf8Value(args[2]->ToString());
  int port_id = -1;
  renderview->Send(new ExtensionHostMsg_OpenChannelToExtension(
      renderview->GetRoutingID(),
      source_id,
      target_id,
      channel_name,
      &port_id));
  return v8::Integer::New(port_id);
}

}  // namespace extensions
