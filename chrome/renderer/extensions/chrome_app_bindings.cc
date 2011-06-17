// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/chrome_app_bindings.h"

#include "base/command_line.h"
#include "base/json/json_writer.h"
#include "base/string16.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension_set.h"
#include "chrome/renderer/extensions/bindings_utils.h"
#include "chrome/renderer/extensions/extension_dispatcher.h"
#include "chrome/renderer/extensions/extension_helper.h"
#include "content/renderer/render_view.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "v8/include/v8.h"

using WebKit::WebFrame;

namespace {

bool IsCheckoutURL(const std::string& url_spec) {
  std::string checkout_url_prefix =
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kAppsCheckoutURL);
  if (checkout_url_prefix.empty())
    checkout_url_prefix = "https://checkout.google.com/";

  return StartsWithASCII(url_spec, checkout_url_prefix, false);
}

bool CheckAccessToAppDetails() {
  WebFrame* frame = WebFrame::frameForCurrentContext();
  if (!frame) {
    LOG(ERROR) << "Could not get frame for current context.";
    return false;
  }

  if (!IsCheckoutURL(frame->url().spec())) {
    std::string error("Access denied for URL: ");
    error += frame->url().spec();
    v8::ThrowException(v8::String::New(error.c_str()));
    return false;
  }

  return true;
}

}

namespace extensions_v8 {

static const char* const kAppExtensionName = "v8/ChromeApp";

class ChromeAppExtensionWrapper : public v8::Extension {
 public:
  explicit ChromeAppExtensionWrapper(ExtensionDispatcher* extension_dispatcher) :
    v8::Extension(kAppExtensionName,
      "var chrome;"
      "if (!chrome)"
      "  chrome = {};"
      "if (!chrome.app) {"
      "  chrome.app = new function() {"
      "    native function GetIsInstalled();"
      "    native function Install();"
      "    native function GetDetails();"
      "    native function GetDetailsForFrame();"
      "    this.__defineGetter__('isInstalled', GetIsInstalled);"
      "    this.install = Install;"
      "    this.getDetails = function() {"
      "      var json = GetDetails();"
      "      return json == null ? null : JSON.parse(json);"
      "    };"
      "    this.getDetailsForFrame = function(frame) {"
      "      var json = GetDetailsForFrame(frame);"
      "      return json == null ? null : JSON.parse(json);"
      "    };"
      "  };"
      "}") {
    extension_dispatcher_ = extension_dispatcher;
  }

  ~ChromeAppExtensionWrapper() {
    extension_dispatcher_ = NULL;
  }

  virtual v8::Handle<v8::FunctionTemplate> GetNativeFunction(
      v8::Handle<v8::String> name) {
    if (name->Equals(v8::String::New("GetIsInstalled"))) {
      return v8::FunctionTemplate::New(GetIsInstalled);
    } else if (name->Equals(v8::String::New("Install"))) {
      return v8::FunctionTemplate::New(Install);
    } else if (name->Equals(v8::String::New("GetDetails"))) {
      return v8::FunctionTemplate::New(GetDetails);
    } else if (name->Equals(v8::String::New("GetDetailsForFrame"))) {
      return v8::FunctionTemplate::New(GetDetailsForFrame);
    } else {
      return v8::Handle<v8::FunctionTemplate>();
    }
  }

  static v8::Handle<v8::Value> GetIsInstalled(const v8::Arguments& args) {
    WebFrame* frame = WebFrame::frameForCurrentContext();
    if (!frame)
      return v8::Boolean::New(false);

    GURL url(frame->url());
    if (url.is_empty() ||
        !url.is_valid() ||
        !(url.SchemeIs("http") || url.SchemeIs("https")))
      return v8::Boolean::New(false);

    bool has_web_extent =
        extension_dispatcher_->extensions()->GetByURL(url) != NULL;
    return v8::Boolean::New(has_web_extent);
  }

  static v8::Handle<v8::Value> Install(const v8::Arguments& args) {
    WebFrame* frame = WebFrame::frameForCurrentContext();
    RenderView* render_view = bindings_utils::GetRenderViewForCurrentContext();
    if (frame && render_view) {
      string16 error;

      ExtensionHelper* helper = ExtensionHelper::Get(render_view);
      if (!helper->InstallWebApplicationUsingDefinitionFile(frame, &error))
        v8::ThrowException(v8::String::New(UTF16ToUTF8(error).c_str()));
    }

    return v8::Undefined();
  }

  static v8::Handle<v8::Value> GetDetails(const v8::Arguments& args) {
    return GetDetailsForFrameImpl(WebFrame::frameForCurrentContext());
  }

  static v8::Handle<v8::Value> GetDetailsForFrame(
      const v8::Arguments& args) {
    if (!CheckAccessToAppDetails())
      return v8::Undefined();

    if (args.Length() < 0)
      return v8::ThrowException(v8::String::New("Not enough arguments."));

    if (!args[0]->IsObject()) {
      return v8::ThrowException(
          v8::String::New("Argument 0 must be an object."));
    }

    v8::Local<v8::Context> context =
        v8::Local<v8::Object>::Cast(args[0])->CreationContext();
    CHECK(!context.IsEmpty());

    WebFrame* target_frame = WebFrame::frameForContext(context);
    if (!target_frame) {
      return v8::ThrowException(
          v8::String::New("Could not find frame for specified object."));
    }

    return GetDetailsForFrameImpl(target_frame);
  }

  static v8::Handle<v8::Value> GetDetailsForFrameImpl(const WebFrame* frame) {
    const ::Extension* extension =
        extension_dispatcher_->extensions()->GetByURL(frame->url());
    if (!extension)
      return v8::Null();

    std::string manifest_json;
    const bool kPrettyPrint = false;
    scoped_ptr<DictionaryValue> manifest_copy(
        extension->manifest_value()->DeepCopy());
    manifest_copy->SetString("id", extension->id());
    base::JSONWriter::Write(manifest_copy.get(), kPrettyPrint, &manifest_json);

    return v8::String::New(manifest_json.c_str(), manifest_json.size());
  }

  static ExtensionDispatcher* extension_dispatcher_;
};

ExtensionDispatcher* ChromeAppExtensionWrapper::extension_dispatcher_;

v8::Extension* ChromeAppExtension::Get(
    ExtensionDispatcher* extension_dispatcher) {
  return new ChromeAppExtensionWrapper(extension_dispatcher);
}

}  // namespace extensions_v8
