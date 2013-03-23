// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/dispatcher.h"

#include "base/callback.h"
#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_piece.h"
#include "chrome/common/child_process_logging.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/extensions/api/extension_api.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/extensions/permissions/permission_set.h"
#include "chrome/common/url_constants.h"
#include "chrome/common/view_type.h"
#include "chrome/renderer/chrome_render_process_observer.h"
#include "chrome/renderer/extensions/api_definitions_natives.h"
#include "chrome/renderer/extensions/app_bindings.h"
#include "chrome/renderer/extensions/app_runtime_custom_bindings.h"
#include "chrome/renderer/extensions/app_window_custom_bindings.h"
#include "chrome/renderer/extensions/chrome_v8_context.h"
#include "chrome/renderer/extensions/chrome_v8_extension.h"
#include "chrome/renderer/extensions/context_menus_custom_bindings.h"
#include "chrome/renderer/extensions/event_bindings.h"
#include "chrome/renderer/extensions/extension_custom_bindings.h"
#include "chrome/renderer/extensions/extension_groups.h"
#include "chrome/renderer/extensions/extension_helper.h"
#include "chrome/renderer/extensions/file_browser_handler_custom_bindings.h"
#include "chrome/renderer/extensions/file_browser_private_custom_bindings.h"
#include "chrome/renderer/extensions/file_system_natives.h"
#include "chrome/renderer/extensions/i18n_custom_bindings.h"
#include "chrome/renderer/extensions/media_galleries_custom_bindings.h"
#include "chrome/renderer/extensions/miscellaneous_bindings.h"
#include "chrome/renderer/extensions/module_system.h"
#include "chrome/renderer/extensions/native_handler.h"
#include "chrome/renderer/extensions/page_actions_custom_bindings.h"
#include "chrome/renderer/extensions/page_capture_custom_bindings.h"
#include "chrome/renderer/extensions/request_sender.h"
#include "chrome/renderer/extensions/runtime_custom_bindings.h"
#include "chrome/renderer/extensions/send_request_natives.h"
#include "chrome/renderer/extensions/set_icon_natives.h"
#include "chrome/renderer/extensions/sync_file_system_custom_bindings.h"
#include "chrome/renderer/extensions/tab_finder.h"
#include "chrome/renderer/extensions/tabs_custom_bindings.h"
#include "chrome/renderer/extensions/tts_custom_bindings.h"
#include "chrome/renderer/extensions/user_script_slave.h"
#include "chrome/renderer/extensions/web_request_custom_bindings.h"
#include "chrome/renderer/extensions/webstore_bindings.h"
#include "chrome/renderer/resource_bundle_source_map.h"
#include "content/public/renderer/render_thread.h"
#include "content/public/renderer/render_view.h"
#include "grit/renderer_resources.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDataSource.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebScopedUserGesture.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebSecurityPolicy.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebString.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebURLRequest.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"
#include "v8/include/v8.h"

using WebKit::WebDataSource;
using WebKit::WebDocument;
using WebKit::WebFrame;
using WebKit::WebScopedUserGesture;
using WebKit::WebSecurityPolicy;
using WebKit::WebString;
using WebKit::WebVector;
using WebKit::WebView;
using content::RenderThread;
using content::RenderView;

namespace extensions {

namespace {

static const int64 kInitialExtensionIdleHandlerDelayMs = 5*1000;
static const int64 kMaxExtensionIdleHandlerDelayMs = 5*60*1000;
static const char kEventDispatchFunction[] = "Event.dispatchEvent";
static const char kOnUnloadEvent[] = "runtime.onSuspend";
static const char kOnSuspendCanceledEvent[] = "runtime.onSuspendCanceled";

class ChromeHiddenNativeHandler : public NativeHandler {
 public:
  ChromeHiddenNativeHandler() {
    RouteFunction("GetChromeHidden",
        base::Bind(&ChromeHiddenNativeHandler::GetChromeHidden,
                   base::Unretained(this)));
  }

  v8::Handle<v8::Value> GetChromeHidden(const v8::Arguments& args) {
    return ChromeV8Context::GetOrCreateChromeHidden(v8::Context::GetCurrent());
  }
};

class PrintNativeHandler : public NativeHandler {
 public:
  PrintNativeHandler() {
    RouteFunction("Print",
        base::Bind(&PrintNativeHandler::Print,
                   base::Unretained(this)));
  }

  v8::Handle<v8::Value> Print(const v8::Arguments& args) {
    if (args.Length() < 1)
      return v8::Undefined();

    std::vector<std::string> components;
    for (int i = 0; i < args.Length(); ++i)
      components.push_back(*v8::String::Utf8Value(args[i]->ToString()));

    LOG(ERROR) << JoinString(components, ',');
    return v8::Undefined();
  }
};

class LazyBackgroundPageNativeHandler : public ChromeV8Extension {
 public:
  explicit LazyBackgroundPageNativeHandler(Dispatcher* dispatcher)
      : ChromeV8Extension(dispatcher) {
    RouteFunction("IncrementKeepaliveCount",
        base::Bind(&LazyBackgroundPageNativeHandler::IncrementKeepaliveCount,
                   base::Unretained(this)));
    RouteFunction("DecrementKeepaliveCount",
        base::Bind(&LazyBackgroundPageNativeHandler::DecrementKeepaliveCount,
                   base::Unretained(this)));
  }

  v8::Handle<v8::Value> IncrementKeepaliveCount(const v8::Arguments& args) {
    ChromeV8Context* context = dispatcher()->v8_context_set().GetCurrent();
    if (!context)
      return v8::Undefined();
    RenderView* render_view = context->GetRenderView();
    if (IsContextLazyBackgroundPage(render_view, context->extension())) {
      render_view->Send(new ExtensionHostMsg_IncrementLazyKeepaliveCount(
          render_view->GetRoutingID()));
    }
    return v8::Undefined();
  }

  v8::Handle<v8::Value> DecrementKeepaliveCount(const v8::Arguments& args) {
    ChromeV8Context* context = dispatcher()->v8_context_set().GetCurrent();
    if (!context)
      return v8::Undefined();
    RenderView* render_view = context->GetRenderView();
    if (IsContextLazyBackgroundPage(render_view, context->extension())) {
      render_view->Send(new ExtensionHostMsg_DecrementLazyKeepaliveCount(
          render_view->GetRoutingID()));
    }
    return v8::Undefined();
  }

 private:
  bool IsContextLazyBackgroundPage(RenderView* render_view,
                                   const Extension* extension) {
    if (!render_view)
      return false;

    ExtensionHelper* helper = ExtensionHelper::Get(render_view);
    return (extension && extension->has_lazy_background_page() &&
            helper->view_type() == chrome::VIEW_TYPE_EXTENSION_BACKGROUND_PAGE);
  }
};

class ProcessInfoNativeHandler : public ChromeV8Extension {
 public:
  explicit ProcessInfoNativeHandler(
      Dispatcher* dispatcher,
      const std::string& extension_id,
      const std::string& context_type,
      bool is_incognito_context,
      int manifest_version,
      bool send_request_disabled)
      : ChromeV8Extension(dispatcher),
        extension_id_(extension_id),
        context_type_(context_type),
        is_incognito_context_(is_incognito_context),
        manifest_version_(manifest_version),
        send_request_disabled_(send_request_disabled) {
    RouteFunction("GetExtensionId",
        base::Bind(&ProcessInfoNativeHandler::GetExtensionId,
                   base::Unretained(this)));
    RouteFunction("GetContextType",
        base::Bind(&ProcessInfoNativeHandler::GetContextType,
                   base::Unretained(this)));
    RouteFunction("InIncognitoContext",
        base::Bind(&ProcessInfoNativeHandler::InIncognitoContext,
                   base::Unretained(this)));
    RouteFunction("GetManifestVersion",
        base::Bind(&ProcessInfoNativeHandler::GetManifestVersion,
                   base::Unretained(this)));
    RouteFunction("IsSendRequestDisabled",
        base::Bind(&ProcessInfoNativeHandler::IsSendRequestDisabled,
                   base::Unretained(this)));
  }

  v8::Handle<v8::Value> GetExtensionId(const v8::Arguments& args) {
    return v8::String::New(extension_id_.c_str());
  }

  v8::Handle<v8::Value> GetContextType(const v8::Arguments& args) {
    return v8::String::New(context_type_.c_str());
  }

  v8::Handle<v8::Value> InIncognitoContext(const v8::Arguments& args) {
    return v8::Boolean::New(is_incognito_context_);
  }

  v8::Handle<v8::Value> GetManifestVersion(const v8::Arguments& args) {
    return v8::Integer::New(manifest_version_);
  }

  v8::Handle<v8::Value> IsSendRequestDisabled(const v8::Arguments& args) {
    if (send_request_disabled_) {
      return v8::String::New(
          "sendRequest and onRequest are obsolete."
          " Please use sendMessage and onMessage instead.");
    }
    return v8::Undefined();
  }

 private:
  std::string extension_id_;
  std::string context_type_;
  bool is_incognito_context_;
  int manifest_version_;
  bool send_request_disabled_;
};

class LoggingNativeHandler : public NativeHandler {
 public:
  LoggingNativeHandler() {
    RouteFunction("DCHECK",
        base::Bind(&LoggingNativeHandler::Dcheck,
                   base::Unretained(this)));
  }

  v8::Handle<v8::Value> Dcheck(const v8::Arguments& args) {
    CHECK_LE(args.Length(), 2);
    bool check_value = args[0]->BooleanValue();
    std::string error_message;
    if (args.Length() == 2)
      error_message = "Error: " + std::string(*v8::String::AsciiValue(args[1]));

    v8::Handle<v8::StackTrace> stack_trace =
        v8::StackTrace::CurrentStackTrace(10);
    if (stack_trace.IsEmpty() || stack_trace->GetFrameCount() <= 0) {
      error_message += "\n    <no stack trace>";
    } else {
      for (size_t i = 0; i < (size_t) stack_trace->GetFrameCount(); ++i) {
        v8::Handle<v8::StackFrame> frame = stack_trace->GetFrame(i);
        CHECK(!frame.IsEmpty());
        error_message += base::StringPrintf("\n    at %s (%s:%d:%d)",
            ToStringOrDefault(frame->GetFunctionName(), "<anonymous>").c_str(),
            ToStringOrDefault(frame->GetScriptName(), "<anonymous>").c_str(),
            frame->GetLineNumber(),
            frame->GetColumn());
      }
    }
    DCHECK(check_value) << error_message;
    LOG(WARNING) << error_message;
    return v8::Undefined();
  }

 private:
  std::string ToStringOrDefault(const v8::Handle<v8::String>& v8_string,
                                  const std::string& dflt) {
    if (v8_string.IsEmpty())
      return dflt;
    std::string ascii_value = *v8::String::AsciiValue(v8_string);
    return ascii_value.empty() ? dflt : ascii_value;
  }
};

void InstallAppBindings(ModuleSystem* module_system,
                        v8::Handle<v8::Object> chrome,
                        v8::Handle<v8::Object> chrome_hidden) {
  module_system->SetLazyField(chrome, "app", "app", "chromeApp");
  module_system->SetLazyField(chrome, "appNotifications", "app",
                              "chromeAppNotifications");
  module_system->SetLazyField(chrome_hidden, "app", "app",
                              "chromeHiddenApp");
}

void InstallWebstoreBindings(ModuleSystem* module_system,
                             v8::Handle<v8::Object> chrome,
                             v8::Handle<v8::Object> chrome_hidden) {
  module_system->SetLazyField(chrome, "webstore", "webstore", "chromeWebstore");
  module_system->SetLazyField(chrome_hidden, "webstore", "webstore",
                              "chromeHiddenWebstore");
}

static v8::Handle<v8::Object> GetOrCreateChrome(
    v8::Handle<v8::Context> context) {
  v8::Handle<v8::String> chrome_string(v8::String::New("chrome"));
  v8::Handle<v8::Object> global(context->Global());
  v8::Handle<v8::Value> chrome(global->Get(chrome_string));
  if (chrome.IsEmpty() || chrome->IsUndefined()) {
    v8::Handle<v8::Object> chrome_object(v8::Object::New());
    global->Set(chrome_string, chrome_object);
    return chrome_object;
  }
  CHECK(chrome->IsObject());
  return chrome->ToObject();
}

}  // namespace

Dispatcher::Dispatcher()
    : is_webkit_initialized_(false),
      webrequest_adblock_(false),
      webrequest_adblock_plus_(false),
      webrequest_other_(false),
      source_map_(&ResourceBundle::GetSharedInstance()) {
  const CommandLine& command_line = *(CommandLine::ForCurrentProcess());
  is_extension_process_ =
      command_line.HasSwitch(switches::kExtensionProcess) ||
      command_line.HasSwitch(switches::kSingleProcess);

  if (is_extension_process_) {
    RenderThread::Get()->SetIdleNotificationDelayInMs(
        kInitialExtensionIdleHandlerDelayMs);
  }

  user_script_slave_.reset(new UserScriptSlave(&extensions_));
  request_sender_.reset(new RequestSender(this, &v8_context_set_));
  PopulateSourceMap();
  PopulateLazyBindingsMap();
}

Dispatcher::~Dispatcher() {
}

bool Dispatcher::OnControlMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(Dispatcher, message)
    IPC_MESSAGE_HANDLER(ExtensionMsg_SetChannel, OnSetChannel)
    IPC_MESSAGE_HANDLER(ExtensionMsg_MessageInvoke, OnMessageInvoke)
    IPC_MESSAGE_HANDLER(ExtensionMsg_DispatchOnConnect, OnDispatchOnConnect)
    IPC_MESSAGE_HANDLER(ExtensionMsg_DeliverMessage, OnDeliverMessage)
    IPC_MESSAGE_HANDLER(ExtensionMsg_DispatchOnDisconnect,
                        OnDispatchOnDisconnect)
    IPC_MESSAGE_HANDLER(ExtensionMsg_SetFunctionNames, OnSetFunctionNames)
    IPC_MESSAGE_HANDLER(ExtensionMsg_Loaded, OnLoaded)
    IPC_MESSAGE_HANDLER(ExtensionMsg_Unloaded, OnUnloaded)
    IPC_MESSAGE_HANDLER(ExtensionMsg_SetScriptingWhitelist,
                        OnSetScriptingWhitelist)
    IPC_MESSAGE_HANDLER(ExtensionMsg_ActivateExtension, OnActivateExtension)
    IPC_MESSAGE_HANDLER(ExtensionMsg_UpdatePermissions, OnUpdatePermissions)
    IPC_MESSAGE_HANDLER(ExtensionMsg_UpdateTabSpecificPermissions,
                        OnUpdateTabSpecificPermissions)
    IPC_MESSAGE_HANDLER(ExtensionMsg_ClearTabSpecificPermissions,
                        OnClearTabSpecificPermissions)
    IPC_MESSAGE_HANDLER(ExtensionMsg_UpdateUserScripts, OnUpdateUserScripts)
    IPC_MESSAGE_HANDLER(ExtensionMsg_UsingWebRequestAPI, OnUsingWebRequestAPI)
    IPC_MESSAGE_HANDLER(ExtensionMsg_ShouldUnload, OnShouldUnload)
    IPC_MESSAGE_HANDLER(ExtensionMsg_Unload, OnUnload)
    IPC_MESSAGE_HANDLER(ExtensionMsg_CancelUnload, OnCancelUnload)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  return handled;
}

void Dispatcher::WebKitInitialized() {
  // For extensions, we want to ensure we call the IdleHandler every so often,
  // even if the extension keeps up activity.
  if (is_extension_process_) {
    forced_idle_timer_.Start(FROM_HERE,
        base::TimeDelta::FromMilliseconds(kMaxExtensionIdleHandlerDelayMs),
        RenderThread::Get(), &RenderThread::IdleHandler);
  }

  // Initialize host permissions for any extensions that were activated before
  // WebKit was initialized.
  for (std::set<std::string>::iterator iter = active_extension_ids_.begin();
       iter != active_extension_ids_.end(); ++iter) {
    const Extension* extension = extensions_.GetByID(*iter);
    CHECK(extension);
    InitOriginPermissions(extension);
  }

  is_webkit_initialized_ = true;
}

void Dispatcher::IdleNotification() {
  if (is_extension_process_) {
    // Dampen the forced delay as well if the extension stays idle for long
    // periods of time.
    int64 forced_delay_ms = std::max(
        RenderThread::Get()->GetIdleNotificationDelayInMs(),
        kMaxExtensionIdleHandlerDelayMs);
    forced_idle_timer_.Stop();
    forced_idle_timer_.Start(FROM_HERE,
        base::TimeDelta::FromMilliseconds(forced_delay_ms),
        RenderThread::Get(), &RenderThread::IdleHandler);
  }
}

void Dispatcher::OnSetFunctionNames(
    const std::vector<std::string>& names) {
  function_names_.clear();
  for (size_t i = 0; i < names.size(); ++i)
    function_names_.insert(names[i]);
}

void Dispatcher::OnSetChannel(int channel) {
  extensions::Feature::SetCurrentChannel(
      static_cast<chrome::VersionInfo::Channel>(channel));
}

void Dispatcher::OnMessageInvoke(const std::string& extension_id,
                                 const std::string& function_name,
                                 const ListValue& args,
                                 const GURL& event_url,
                                 bool user_gesture) {
  scoped_ptr<WebScopedUserGesture> web_user_gesture;
  if (user_gesture) {
    web_user_gesture.reset(new WebScopedUserGesture);
  }

  v8_context_set_.DispatchChromeHiddenMethod(
      extension_id, function_name, args, NULL, event_url);

  // Reset the idle handler each time there's any activity like event or message
  // dispatch, for which Invoke is the chokepoint.
  if (is_extension_process_) {
    RenderThread::Get()->ScheduleIdleHandler(
        kInitialExtensionIdleHandlerDelayMs);
  }

  // Tell the browser process when an event has been dispatched with a lazy
  // background page active.
  const Extension* extension = extensions_.GetByID(extension_id);
  if (extension && extension->has_lazy_background_page() &&
      function_name == kEventDispatchFunction) {
    RenderView* background_view =
        ExtensionHelper::GetBackgroundPage(extension_id);
    if (background_view) {
      background_view->Send(new ExtensionHostMsg_EventAck(
          background_view->GetRoutingID()));
    }
  }
}

void Dispatcher::OnDispatchOnConnect(int target_port_id,
                                     const std::string& channel_name,
                                     const std::string& tab_json,
                                     const std::string& source_extension_id,
                                     const std::string& target_extension_id) {
  MiscellaneousBindings::DispatchOnConnect(
      v8_context_set_.GetAll(),
      target_port_id, channel_name, tab_json,
      source_extension_id, target_extension_id,
      NULL);  // All render views.
}

void Dispatcher::OnDeliverMessage(int target_port_id,
                                  const std::string& message) {
  MiscellaneousBindings::DeliverMessage(
      v8_context_set_.GetAll(),
      target_port_id,
      message,
      NULL);  // All render views.
}

void Dispatcher::OnDispatchOnDisconnect(int port_id, bool connection_error) {
  MiscellaneousBindings::DispatchOnDisconnect(
      v8_context_set_.GetAll(),
      port_id, connection_error,
      NULL);  // All render views.
}

void Dispatcher::OnLoaded(
    const std::vector<ExtensionMsg_Loaded_Params>& loaded_extensions) {
  std::vector<ExtensionMsg_Loaded_Params>::const_iterator i;
  for (i = loaded_extensions.begin(); i != loaded_extensions.end(); ++i) {
    scoped_refptr<const Extension> extension(i->ConvertToExtension());
    if (!extension) {
      // This can happen if extension parsing fails for any reason. One reason
      // this can legitimately happen is if the
      // --enable-experimental-extension-apis changes at runtime, which happens
      // during browser tests. Existing renderers won't know about the change.
      continue;
    }

    extensions_.Insert(extension);
  }
}

void Dispatcher::OnUnloaded(const std::string& id) {
  extensions_.Remove(id);
  active_extension_ids_.erase(id);

  // If the extension is later reloaded with a different set of permissions,
  // we'd like it to get a new isolated world ID, so that it can pick up the
  // changed origin whitelist.
  user_script_slave_->RemoveIsolatedWorld(id);

  // We don't do anything with existing platform-app stylesheets. They will
  // stay resident, but the URL pattern corresponding to the unloaded
  // extension's URL just won't match anything anymore.
}

void Dispatcher::OnSetScriptingWhitelist(
    const Extension::ScriptingWhitelist& extension_ids) {
  Extension::SetScriptingWhitelist(extension_ids);
}

bool Dispatcher::IsExtensionActive(
    const std::string& extension_id) const {
  bool is_active =
      active_extension_ids_.find(extension_id) != active_extension_ids_.end();
  if (is_active)
    CHECK(extensions_.Contains(extension_id));
  return is_active;
}

bool Dispatcher::AllowScriptExtension(
    WebFrame* frame,
    const std::string& v8_extension_name,
    int extension_group) {
  return AllowScriptExtension(frame, v8_extension_name, extension_group, 0);
}

namespace {

// This is what the extension_group variable will be when DidCreateScriptContext
// is called. We know because it's the same as what AllowScriptExtension gets
// passed, and the two functions are called sequentially from WebKit.
//
// TODO(koz): Plumb extension_group through to AllowScriptExtension() from
// WebKit.
static int g_hack_extension_group = 0;

}  // namespace

bool Dispatcher::AllowScriptExtension(WebFrame* frame,
                                      const std::string& v8_extension_name,
                                      int extension_group,
                                      int world_id) {
  g_hack_extension_group = extension_group;
  return true;
}

void Dispatcher::RegisterNativeHandlers(ModuleSystem* module_system,
                                        ChromeV8Context* context) {
  module_system->RegisterNativeHandler("event_bindings",
      scoped_ptr<NativeHandler>(EventBindings::Get(this)));
  module_system->RegisterNativeHandler("miscellaneous_bindings",
      scoped_ptr<NativeHandler>(MiscellaneousBindings::Get(this)));
  module_system->RegisterNativeHandler("apiDefinitions",
      scoped_ptr<NativeHandler>(new ApiDefinitionsNatives(this)));
  module_system->RegisterNativeHandler("sendRequest",
      scoped_ptr<NativeHandler>(
          new SendRequestNatives(this, request_sender_.get())));
  module_system->RegisterNativeHandler("setIcon",
      scoped_ptr<NativeHandler>(
          new SetIconNatives(this, request_sender_.get())));

  // Natives used by multiple APIs.
  module_system->RegisterNativeHandler("file_system_natives",
      scoped_ptr<NativeHandler>(new FileSystemNatives()));

  // Custom bindings.
  module_system->RegisterNativeHandler("app",
      scoped_ptr<NativeHandler>(new AppBindings(this, context)));
  module_system->RegisterNativeHandler("app_runtime",
      scoped_ptr<NativeHandler>(new AppRuntimeCustomBindings()));
  module_system->RegisterNativeHandler("app_window",
      scoped_ptr<NativeHandler>(new AppWindowCustomBindings(this)));
  module_system->RegisterNativeHandler("context_menus",
      scoped_ptr<NativeHandler>(new ContextMenusCustomBindings()));
  module_system->RegisterNativeHandler("extension",
      scoped_ptr<NativeHandler>(
          new ExtensionCustomBindings(this)));
  module_system->RegisterNativeHandler("sync_file_system",
      scoped_ptr<NativeHandler>(new SyncFileSystemCustomBindings()));
  module_system->RegisterNativeHandler("file_browser_handler",
      scoped_ptr<NativeHandler>(new FileBrowserHandlerCustomBindings()));
  module_system->RegisterNativeHandler("file_browser_private",
      scoped_ptr<NativeHandler>(new FileBrowserPrivateCustomBindings()));
  module_system->RegisterNativeHandler("i18n",
      scoped_ptr<NativeHandler>(new I18NCustomBindings()));
  module_system->RegisterNativeHandler("mediaGalleries",
      scoped_ptr<NativeHandler>(new MediaGalleriesCustomBindings()));
  module_system->RegisterNativeHandler("page_actions",
      scoped_ptr<NativeHandler>(
          new PageActionsCustomBindings(this)));
  module_system->RegisterNativeHandler("page_capture",
      scoped_ptr<NativeHandler>(new PageCaptureCustomBindings()));
  module_system->RegisterNativeHandler("runtime",
      scoped_ptr<NativeHandler>(new RuntimeCustomBindings(context)));
  module_system->RegisterNativeHandler("tabs",
      scoped_ptr<NativeHandler>(new TabsCustomBindings()));
  module_system->RegisterNativeHandler("tts",
      scoped_ptr<NativeHandler>(new TTSCustomBindings()));
  module_system->RegisterNativeHandler("web_request",
      scoped_ptr<NativeHandler>(new WebRequestCustomBindings()));
  module_system->RegisterNativeHandler("webstore",
      scoped_ptr<NativeHandler>(new WebstoreBindings(this, context)));
}

void Dispatcher::PopulateSourceMap() {
  source_map_.RegisterSource("event_bindings", IDR_EVENT_BINDINGS_JS);
  source_map_.RegisterSource("miscellaneous_bindings",
      IDR_MISCELLANEOUS_BINDINGS_JS);
  source_map_.RegisterSource("schema_generated_bindings",
      IDR_SCHEMA_GENERATED_BINDINGS_JS);
  source_map_.RegisterSource("json_schema", IDR_JSON_SCHEMA_JS);
  source_map_.RegisterSource("apitest", IDR_EXTENSION_APITEST_JS);

  // Libraries.
  source_map_.RegisterSource("lastError", IDR_LAST_ERROR_JS);
  source_map_.RegisterSource("schemaUtils", IDR_SCHEMA_UTILS_JS);
  source_map_.RegisterSource("sendRequest", IDR_SEND_REQUEST_JS);
  source_map_.RegisterSource("setIcon", IDR_SET_ICON_JS);
  source_map_.RegisterSource("utils", IDR_UTILS_JS);

  // Custom bindings.
  source_map_.RegisterSource("app", IDR_APP_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("app.runtime", IDR_APP_RUNTIME_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("app.window", IDR_APP_WINDOW_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("bluetooth", IDR_BLUETOOTH_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("browserAction",
                             IDR_BROWSER_ACTION_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("contentSettings",
                             IDR_CONTENT_SETTINGS_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("contextMenus",
                             IDR_CONTEXT_MENUS_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("declarativeWebRequest",
                             IDR_DECLARATIVE_WEBREQUEST_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource(
      "experimental.mediaGalleries",
      IDR_EXPERIMENTAL_MEDIA_GALLERIES_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("experimental.offscreen",
                             IDR_EXPERIMENTAL_OFFSCREENTABS_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("extension", IDR_EXTENSION_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("fileBrowserHandler",
                             IDR_FILE_BROWSER_HANDLER_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("fileBrowserPrivate",
                             IDR_FILE_BROWSER_PRIVATE_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("fileSystem",
                             IDR_FILE_SYSTEM_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("i18n", IDR_I18N_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("input.ime", IDR_INPUT_IME_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("mediaGalleries",
                             IDR_MEDIA_GALLERIES_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("omnibox", IDR_OMNIBOX_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("pageActions",
                             IDR_PAGE_ACTIONS_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("pageAction", IDR_PAGE_ACTION_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("pageCapture",
                             IDR_PAGE_CAPTURE_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("permissions", IDR_PERMISSIONS_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("runtime", IDR_RUNTIME_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("storage", IDR_STORAGE_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("syncFileSystem",
                             IDR_SYNC_FILE_SYSTEM_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("systemIndicator",
                             IDR_SYSTEM_INDICATOR_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("tabCapture", IDR_TAB_CAPTURE_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("tabs", IDR_TABS_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("tts", IDR_TTS_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("ttsEngine", IDR_TTS_ENGINE_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("types", IDR_TYPES_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("webRequest", IDR_WEB_REQUEST_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("webRequestInternal",
                             IDR_WEB_REQUEST_INTERNAL_CUSTOM_BINDINGS_JS);
  source_map_.RegisterSource("webstore", IDR_WEBSTORE_CUSTOM_BINDINGS_JS);

  // Platform app sources that are not API-specific..
  source_map_.RegisterSource("tagWatcher", IDR_TAG_WATCHER_JS);
  source_map_.RegisterSource("webview", IDR_WEB_VIEW_JS);
  source_map_.RegisterSource("denyWebview", IDR_WEB_VIEW_DENY_JS);
  source_map_.RegisterSource("platformApp", IDR_PLATFORM_APP_JS);
  source_map_.RegisterSource("injectAppTitlebar", IDR_INJECT_APP_TITLEBAR_JS);
}

void Dispatcher::PopulateLazyBindingsMap() {
  lazy_bindings_map_["app"] = InstallAppBindings;
  lazy_bindings_map_["webstore"] = InstallWebstoreBindings;
}

void Dispatcher::InstallBindings(ModuleSystem* module_system,
                                 v8::Handle<v8::Context> v8_context,
                                 const std::string& api) {
  std::map<std::string, BindingInstaller>::const_iterator lazy_binding =
      lazy_bindings_map_.find(api);
  if (lazy_binding != lazy_bindings_map_.end()) {
    v8::Handle<v8::Object> global(v8_context->Global());
    v8::Handle<v8::Object> chrome =
        global->Get(v8::String::New("chrome"))->ToObject();
    v8::Handle<v8::Object> chrome_hidden =
        ChromeV8Context::GetOrCreateChromeHidden(v8_context)->ToObject();
    (*lazy_binding->second)(module_system, chrome, chrome_hidden);
  } else {
    module_system->Require(api);
  }
}

void Dispatcher::DidCreateScriptContext(
    WebFrame* frame, v8::Handle<v8::Context> v8_context, int extension_group,
    int world_id) {
// Extensions are not supported on Android, so don't register any bindings.
#if defined(OS_ANDROID)
  return;
#endif

  // TODO(koz): If the caller didn't pass extension_group, use the last value.
  if (extension_group == -1)
    extension_group = g_hack_extension_group;

  std::string extension_id = GetExtensionID(frame, world_id);

  const Extension* extension = extensions_.GetByID(extension_id);
  if (!extension && !extension_id.empty()) {
    // There are conditions where despite a context being associated with an
    // extension, no extension actually gets found.  Ignore "invalid" because
    // CSP blocks extension page loading by switching the extension ID to
    // "invalid". This isn't interesting.
    if (extension_id != "invalid") {
      LOG(ERROR) << "Extension \"" << extension_id << "\" not found";
      RenderThread::Get()->RecordUserMetrics("ExtensionNotFound_ED");
    }

    extension_id = "";
  }

  ExtensionURLInfo url_info(frame->document().securityOrigin(),
      UserScriptSlave::GetDataSourceURLForFrame(frame));

  Feature::Context context_type =
      ClassifyJavaScriptContext(extension_id, extension_group, url_info);

  ChromeV8Context* context =
      new ChromeV8Context(v8_context, frame, extension, context_type);
  v8_context_set_.Add(context);

  scoped_ptr<ModuleSystem> module_system(new ModuleSystem(v8_context,
                                                          &source_map_));
  // Enable natives in startup.
  ModuleSystem::NativesEnabledScope natives_enabled_scope(module_system.get());

  RegisterNativeHandlers(module_system.get(), context);

  module_system->RegisterNativeHandler("chrome_hidden",
      scoped_ptr<NativeHandler>(new ChromeHiddenNativeHandler()));
  module_system->RegisterNativeHandler("print",
      scoped_ptr<NativeHandler>(new PrintNativeHandler()));
  module_system->RegisterNativeHandler("lazy_background_page",
      scoped_ptr<NativeHandler>(new LazyBackgroundPageNativeHandler(this)));
  module_system->RegisterNativeHandler("logging",
      scoped_ptr<NativeHandler>(new LoggingNativeHandler()));

  int manifest_version = extension ? extension->manifest_version() : 1;
  bool send_request_disabled =
      (extension && extension->location() == Extension::LOAD &&
       extension->has_lazy_background_page());
  module_system->RegisterNativeHandler("process",
      scoped_ptr<NativeHandler>(new ProcessInfoNativeHandler(
          this, context->GetExtensionID(),
          context->GetContextTypeDescription(),
          ChromeRenderProcessObserver::is_incognito_process(),
          manifest_version, send_request_disabled)));

  GetOrCreateChrome(v8_context);

  // Loading JavaScript is expensive, so only run the full API bindings
  // generation mechanisms in extension pages (NOT all web pages).
  switch (context_type) {
    case Feature::UNSPECIFIED_CONTEXT:
    case Feature::WEB_PAGE_CONTEXT:
      // TODO(kalman): see comment below about ExtensionAPI.
      InstallBindings(module_system.get(), v8_context, "app");
      InstallBindings(module_system.get(), v8_context, "webstore");
      break;

    case Feature::BLESSED_EXTENSION_CONTEXT:
    case Feature::UNBLESSED_EXTENSION_CONTEXT:
    case Feature::CONTENT_SCRIPT_CONTEXT: {
      CHECK(extension);
      if (!extension->is_platform_app())
        module_system->Require("miscellaneous_bindings");
      module_system->Require("schema_generated_bindings");
      module_system->Require("apitest");

      // TODO(kalman): move this code back out of the switch and execute it
      // regardless of |context_type|. ExtensionAPI knows how to return the
      // correct APIs, however, until it doesn't have a 2MB overhead we can't
      // load it in every process.
      const std::set<std::string>& apis = context->GetAvailableExtensionAPIs();
      for (std::set<std::string>::const_iterator i = apis.begin();
           i != apis.end(); ++i) {
        InstallBindings(module_system.get(), v8_context, *i);
      }

      break;
    }
  }

  // Inject custom JS into the platform app context.
  if (IsWithinPlatformApp(frame))
    module_system->Require("platformApp");

  if (context_type == Feature::BLESSED_EXTENSION_CONTEXT) {
    bool has_permission = extension->HasAPIPermission(APIPermission::kWebView);
    module_system->Require(has_permission ? "webview" : "denyWebview");
  }

  context->set_module_system(module_system.Pass());

  context->DispatchOnLoadEvent(
      ChromeRenderProcessObserver::is_incognito_process(),
      manifest_version);

  VLOG(1) << "Num tracked contexts: " << v8_context_set_.size();
}

std::string Dispatcher::GetExtensionID(const WebFrame* frame, int world_id) {
  if (world_id != 0) {
    // Isolated worlds (content script).
    return user_script_slave_->GetExtensionIdForIsolatedWorld(world_id);
  }

  // Extension pages (chrome-extension:// URLs).
  GURL frame_url = UserScriptSlave::GetDataSourceURLForFrame(frame);
  return extensions_.GetExtensionOrAppIDByURL(
      ExtensionURLInfo(frame->document().securityOrigin(), frame_url));
}

bool Dispatcher::IsWithinPlatformApp(const WebFrame* frame) {
  // We intentionally don't use the origin parameter for ExtensionURLInfo since
  // it would be empty (i.e. unique) for sandboxed resources and thus not match.
  ExtensionURLInfo url_info(
      UserScriptSlave::GetDataSourceURLForFrame(frame->top()));
  const Extension* extension = extensions_.GetExtensionOrAppByURL(url_info);

  return extension && extension->is_platform_app();
}

void Dispatcher::WillReleaseScriptContext(
    WebFrame* frame, v8::Handle<v8::Context> v8_context, int world_id) {
  ChromeV8Context* context = v8_context_set_.GetByV8Context(v8_context);
  if (!context)
    return;

  context->DispatchOnUnloadEvent();

  v8_context_set_.Remove(context);
  VLOG(1) << "Num tracked contexts: " << v8_context_set_.size();
}

void Dispatcher::DidCreateDocumentElement(WebKit::WebFrame* frame) {
  if (IsWithinPlatformApp(frame)) {
    // WebKit doesn't let us define an additional user agent stylesheet, so we
    // insert the default platform app stylesheet into all documents that are
    // loaded in each app.
    frame->document().insertUserStyleSheet(
        WebString::fromUTF8(ResourceBundle::GetSharedInstance().
            GetRawDataResource(IDR_PLATFORM_APP_CSS)),
        WebDocument::UserStyleUserLevel);
  }
}

void Dispatcher::OnActivateExtension(const std::string& extension_id) {
  active_extension_ids_.insert(extension_id);
  const Extension* extension = extensions_.GetByID(extension_id);
  CHECK(extension);

  // This is called when starting a new extension page, so start the idle
  // handler ticking.
  RenderThread::Get()->ScheduleIdleHandler(kInitialExtensionIdleHandlerDelayMs);

  UpdateActiveExtensions();

  if (is_webkit_initialized_)
    InitOriginPermissions(extension);
}

void Dispatcher::InitOriginPermissions(const Extension* extension) {
  // TODO(jstritar): We should try to remove this special case. Also, these
  // whitelist entries need to be updated when the kManagement permission
  // changes.
  if (extension->HasAPIPermission(APIPermission::kManagement)) {
    WebSecurityPolicy::addOriginAccessWhitelistEntry(
        extension->url(),
        WebString::fromUTF8(chrome::kChromeUIScheme),
        WebString::fromUTF8(chrome::kChromeUIExtensionIconHost),
        false);
  }

  AddOrRemoveOriginPermissions(
      UpdatedExtensionPermissionsInfo::ADDED,
      extension,
      extension->GetActivePermissions()->explicit_hosts());
}

void Dispatcher::AddOrRemoveOriginPermissions(
    UpdatedExtensionPermissionsInfo::Reason reason,
    const Extension* extension,
    const URLPatternSet& origins) {
  for (URLPatternSet::const_iterator i = origins.begin();
       i != origins.end(); ++i) {
    const char* schemes[] = {
      chrome::kHttpScheme,
      chrome::kHttpsScheme,
      chrome::kFileScheme,
      chrome::kChromeUIScheme,
    };
    for (size_t j = 0; j < arraysize(schemes); ++j) {
      if (i->MatchesScheme(schemes[j])) {
        ((reason == UpdatedExtensionPermissionsInfo::REMOVED) ?
         WebSecurityPolicy::removeOriginAccessWhitelistEntry :
         WebSecurityPolicy::addOriginAccessWhitelistEntry)(
              extension->url(),
              WebString::fromUTF8(schemes[j]),
              WebString::fromUTF8(i->host()),
              i->match_subdomains());
      }
    }
  }
}

void Dispatcher::OnUpdatePermissions(int reason_id,
                                     const std::string& extension_id,
                                     const APIPermissionSet& apis,
                                     const URLPatternSet& explicit_hosts,
                                     const URLPatternSet& scriptable_hosts) {
  const Extension* extension = extensions_.GetByID(extension_id);
  if (!extension)
    return;

  scoped_refptr<const PermissionSet> delta =
      new PermissionSet(apis, explicit_hosts, scriptable_hosts);
  scoped_refptr<const PermissionSet> old_active =
      extension->GetActivePermissions();
  UpdatedExtensionPermissionsInfo::Reason reason =
      static_cast<UpdatedExtensionPermissionsInfo::Reason>(reason_id);

  const PermissionSet* new_active = NULL;
  switch (reason) {
    case UpdatedExtensionPermissionsInfo::ADDED:
      new_active = PermissionSet::CreateUnion(old_active, delta);
      break;
    case UpdatedExtensionPermissionsInfo::REMOVED:
      new_active = PermissionSet::CreateDifference(old_active, delta);
      break;
  }

  extension->SetActivePermissions(new_active);
  AddOrRemoveOriginPermissions(reason, extension, explicit_hosts);
}

void Dispatcher::OnUpdateTabSpecificPermissions(
    int page_id,
    int tab_id,
    const std::string& extension_id,
    const URLPatternSet& origin_set) {
  RenderView* view = TabFinder::Find(tab_id);

  // For now, the message should only be sent to the render view that contains
  // the target tab. This may change. Either way, if this is the target tab it
  // gives us the chance to check against the page ID to avoid races.
  DCHECK(view);
  if (view && view->GetPageId() != page_id)
    return;

  const Extension* extension = extensions_.GetByID(extension_id);
  if (!extension)
    return;

  extension->UpdateTabSpecificPermissions(
      tab_id,
      new PermissionSet(APIPermissionSet(), origin_set, URLPatternSet()));
}

void Dispatcher::OnClearTabSpecificPermissions(
    int tab_id,
    const std::vector<std::string>& extension_ids) {
  for (std::vector<std::string>::const_iterator it = extension_ids.begin();
       it != extension_ids.end(); ++it) {
    const Extension* extension = extensions_.GetByID(*it);
    if (extension)
      extension->ClearTabSpecificPermissions(tab_id);
  }
}

void Dispatcher::OnUpdateUserScripts(
    base::SharedMemoryHandle scripts) {
  DCHECK(base::SharedMemory::IsHandleValid(scripts)) << "Bad scripts handle";
  user_script_slave_->UpdateScripts(scripts);
  UpdateActiveExtensions();
}

void Dispatcher::UpdateActiveExtensions() {
  // In single-process mode, the browser process reports the active extensions.
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kSingleProcess))
    return;

  std::set<std::string> active_extensions = active_extension_ids_;
  user_script_slave_->GetActiveExtensions(&active_extensions);
  child_process_logging::SetActiveExtensions(active_extensions);
}

void Dispatcher::OnUsingWebRequestAPI(
    bool adblock, bool adblock_plus, bool other) {
  webrequest_adblock_ = adblock;
  webrequest_adblock_plus_ = adblock_plus;
  webrequest_other_ = other;
}

void Dispatcher::OnShouldUnload(const std::string& extension_id,
                                         int sequence_id) {
  RenderThread::Get()->Send(
      new ExtensionHostMsg_ShouldUnloadAck(extension_id, sequence_id));
}

void Dispatcher::OnUnload(const std::string& extension_id) {
  // Dispatch the unload event. This doesn't go through the standard event
  // dispatch machinery because it requires special handling. We need to let
  // the browser know when we are starting and stopping the event dispatch, so
  // that it still considers the extension idle despite any activity the unload
  // event creates.
  ListValue args;
  args.Set(0, Value::CreateStringValue(kOnUnloadEvent));
  args.Set(1, new ListValue());
  v8_context_set_.DispatchChromeHiddenMethod(
      extension_id, kEventDispatchFunction, args, NULL, GURL());

  RenderThread::Get()->Send(new ExtensionHostMsg_UnloadAck(extension_id));
}

void Dispatcher::OnCancelUnload(const std::string& extension_id) {
  ListValue args;
  args.Set(0, Value::CreateStringValue(kOnSuspendCanceledEvent));
  args.Set(1, new ListValue());
  v8_context_set_.DispatchChromeHiddenMethod(
      extension_id, kEventDispatchFunction, args, NULL, GURL());
}

Feature::Context Dispatcher::ClassifyJavaScriptContext(
    const std::string& extension_id,
    int extension_group,
    const ExtensionURLInfo& url_info) {
  if (extension_group == EXTENSION_GROUP_CONTENT_SCRIPTS) {
    return extensions_.Contains(extension_id) ?
        Feature::CONTENT_SCRIPT_CONTEXT : Feature::UNSPECIFIED_CONTEXT;
  }

  // We have an explicit check for sandboxed pages before checking whether the
  // extension is active in this process because:
  // 1. Sandboxed pages run in the same process as regular extension pages, so
  //    the extension is considered active.
  // 2. ScriptContext creation (which triggers bindings injection) happens
  //    before the SecurityContext is updated with the sandbox flags (after
  //    reading the CSP header), so url_info.url().securityOrigin() is not
  //    unique yet.
  if (extensions_.IsSandboxedPage(url_info))
    return Feature::WEB_PAGE_CONTEXT;

  if (IsExtensionActive(extension_id))
    return Feature::BLESSED_EXTENSION_CONTEXT;

  if (extensions_.ExtensionBindingsAllowed(url_info)) {
    return extensions_.Contains(extension_id) ?
        Feature::UNBLESSED_EXTENSION_CONTEXT : Feature::UNSPECIFIED_CONTEXT;
  }

  if (url_info.url().is_valid())
    return Feature::WEB_PAGE_CONTEXT;

  return Feature::UNSPECIFIED_CONTEXT;
}

void Dispatcher::OnExtensionResponse(int request_id,
                                     bool success,
                                     const base::ListValue& response,
                                     const std::string& error) {
  request_sender_->HandleResponse(request_id, success, response, error);
}

bool Dispatcher::CheckCurrentContextAccessToExtensionAPI(
    const std::string& function_name) const {
  ChromeV8Context* context = v8_context_set().GetCurrent();
  if (!context) {
    DLOG(ERROR) << "Not in a v8::Context";
    return false;
  }

  if (!context->extension()) {
    v8::ThrowException(
        v8::Exception::Error(v8::String::New("Not in an extension.")));
    return false;
  }

  if (!context->extension()->HasAPIPermission(function_name)) {
    static const char kMessage[] =
        "You do not have permission to use '%s'. Be sure to declare"
        " in your manifest what permissions you need.";
    std::string error_msg = base::StringPrintf(kMessage, function_name.c_str());
    v8::ThrowException(
        v8::Exception::Error(v8::String::New(error_msg.c_str())));
    return false;
  }

  if (ExtensionAPI::GetSharedInstance()->IsPrivileged(function_name) &&
      context->context_type() != Feature::BLESSED_EXTENSION_CONTEXT) {
    static const char kMessage[] =
        "%s can only be used in an extension process.";
    std::string error_msg = base::StringPrintf(kMessage, function_name.c_str());
    v8::ThrowException(
        v8::Exception::Error(v8::String::New(error_msg.c_str())));
    return false;
  }

  // Theoretically we could end up with bindings being injected into sandboxed
  // frames, for example content scripts. Don't let them execute API functions.
  WebKit::WebFrame* frame = context->web_frame();
  ExtensionURLInfo url_info(frame->document().securityOrigin(),
                            UserScriptSlave::GetDataSourceURLForFrame(frame));
  if (extensions_.IsSandboxedPage(url_info)) {
    static const char kMessage[] =
        "%s cannot be used within a sandboxed frame.";
    std::string error_msg = base::StringPrintf(kMessage, function_name.c_str());
    v8::ThrowException(
        v8::Exception::Error(v8::String::New(error_msg.c_str())));
    return false;
  }

  return true;
}

}  // namespace extensions
