// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/page_capture_custom_bindings.h"

#include "base/utf_string_conversions.h"
#include "chrome/common/extensions/extension_messages.h"
#include "content/public/renderer/render_view.h"
#include "grit/renderer_resources.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebBlob.h"
#include "v8/include/v8.h"

namespace extensions {

PageCaptureCustomBindings::PageCaptureCustomBindings(
    int dependency_count,
    const char** dependencies)
    : ChromeV8Extension(
          "extensions/page_capture_custom_bindings.js",
          IDR_PAGE_CAPTURE_CUSTOM_BINDINGS_JS,
          dependency_count,
          dependencies,
          NULL) {}

// Creates a Blob with the content of the specified file.
static v8::Handle<v8::Value> CreateBlob(const v8::Arguments& args) {
  CHECK(args.Length() == 2);
  CHECK(args[0]->IsString());
  CHECK(args[1]->IsInt32());
  WebKit::WebString path(UTF8ToUTF16(*v8::String::Utf8Value(args[0])));
  WebKit::WebBlob blob =
      WebKit::WebBlob::createFromFile(path, args[1]->Int32Value());
  return blob.toV8Value();
}

// static
v8::Handle<v8::Value> PageCaptureCustomBindings::SendResponseAck(
    const v8::Arguments& args) {
  CHECK(args.Length() == 1);
  CHECK(args[0]->IsInt32());

  content::RenderView* render_view = GetCurrentRenderView();
  if (render_view) {
    render_view->Send(new ExtensionHostMsg_ResponseAck(
        render_view->GetRoutingId(), args[0]->Int32Value()));
  }
  return v8::Undefined();
}

v8::Handle<v8::FunctionTemplate> PageCaptureCustomBindings::GetNativeFunction(
    v8::Handle<v8::String> name) {
  if (name->Equals(v8::String::New("CreateBlob"))) {
    return v8::FunctionTemplate::New(CreateBlob, v8::External::New(this));
  } else if (name->Equals(v8::String::New("SendResponseAck"))) {
    return v8::FunctionTemplate::New(SendResponseAck, v8::External::New(this));
  }

  return ChromeV8Extension::GetNativeFunction(name);
}

}  // namespace extensions
