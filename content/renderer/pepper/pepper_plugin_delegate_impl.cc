// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/pepper/pepper_plugin_delegate_impl.h"

#include <cmath>
#include <cstddef>
#include <map>
#include <queue>

#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util_proxy.h"
#include "base/logging.h"
#include "base/string_split.h"
#include "base/sync_socket.h"
#include "base/time.h"
#include "content/common/child_process.h"
#include "content/common/child_process_messages.h"
#include "content/common/child_thread.h"
#include "content/common/fileapi/file_system_dispatcher.h"
#include "content/common/fileapi/file_system_messages.h"
#include "content/common/gpu/client/webgraphicscontext3d_command_buffer_impl.h"
#include "content/common/pepper_plugin_registry.h"
#include "content/common/pepper_messages.h"
#include "content/common/quota_dispatcher.h"
#include "content/common/view_messages.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/context_menu_params.h"
#include "content/public/common/media_stream_request.h"
#include "content/public/common/referrer.h"
#include "content/public/renderer/content_renderer_client.h"
#include "content/public/renderer/renderer_restrict_dispatch_group.h"
#include "content/renderer/browser_plugin/old/browser_plugin_constants.h"
#include "content/renderer/browser_plugin/old/browser_plugin_registry.h"
#include "content/renderer/gamepad_shared_memory_reader.h"
#include "content/renderer/media/audio_hardware.h"
#include "content/renderer/media/media_stream_dispatcher.h"
#include "content/renderer/media/pepper_platform_video_decoder_impl.h"
#include "content/renderer/p2p/p2p_transport_impl.h"
#include "content/renderer/p2p/socket_dispatcher.h"
#include "content/renderer/pepper/content_renderer_pepper_host_factory.h"
#include "content/renderer/pepper/pepper_broker_impl.h"
#include "content/renderer/pepper/pepper_device_enumeration_event_handler.h"
#include "content/renderer/pepper/pepper_hung_plugin_filter.h"
#include "content/renderer/pepper/pepper_in_process_resource_creation.h"
#include "content/renderer/pepper/pepper_instance_state_accessor.h"
#include "content/renderer/pepper/pepper_platform_audio_input_impl.h"
#include "content/renderer/pepper/pepper_platform_audio_output_impl.h"
#include "content/renderer/pepper/pepper_platform_context_3d_impl.h"
#include "content/renderer/pepper/pepper_platform_image_2d_impl.h"
#include "content/renderer/pepper/pepper_platform_video_capture_impl.h"
#include "content/renderer/pepper/pepper_proxy_channel_delegate_impl.h"
#include "content/renderer/render_thread_impl.h"
#include "content/renderer/render_view_impl.h"
#include "content/renderer/render_widget_fullscreen_pepper.h"
#include "content/renderer/renderer_clipboard_client.h"
#include "content/renderer/webplugin_delegate_proxy.h"
#include "googleurl/src/gurl.h"
#include "ipc/ipc_channel_handle.h"
#include "media/video/capture/video_capture_proxy.h"
#include "ppapi/c/dev/pp_video_dev.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/c/private/ppb_flash.h"
#include "ppapi/host/ppapi_host.h"
#include "ppapi/proxy/host_dispatcher.h"
#include "ppapi/proxy/pepper_file_messages.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/shared_impl/file_path.h"
#include "ppapi/shared_impl/platform_file.h"
#include "ppapi/shared_impl/ppapi_permissions.h"
#include "ppapi/shared_impl/ppapi_preferences.h"
#include "ppapi/shared_impl/ppb_device_ref_shared.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/ppb_tcp_server_socket_private_api.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebCursorInfo.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebInputEvent.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPluginContainer.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebScreenInfo.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "ui/gfx/size.h"
#include "webkit/fileapi/file_system_callback_dispatcher.h"
#include "webkit/plugins/npapi/webplugin.h"
#include "webkit/plugins/ppapi/ppb_file_io_impl.h"
#include "webkit/plugins/ppapi/plugin_module.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"
#include "webkit/plugins/ppapi/ppb_flash_impl.h"
#include "webkit/plugins/ppapi/ppb_tcp_server_socket_private_impl.h"
#include "webkit/plugins/ppapi/ppb_tcp_socket_private_impl.h"
#include "webkit/plugins/ppapi/ppb_udp_socket_private_impl.h"
#include "webkit/plugins/ppapi/resource_helper.h"
#include "webkit/plugins/webplugininfo.h"

using WebKit::WebView;
using WebKit::WebFrame;

namespace content {

namespace {

class HostDispatcherWrapper
    : public webkit::ppapi::PluginDelegate::OutOfProcessProxy {
 public:
  HostDispatcherWrapper(RenderViewImpl* rv,
                        webkit::ppapi::PluginModule* module,
                        const ppapi::PpapiPermissions& perms)
      : module_(module),
        instance_state_(module),
        host_factory_(rv, perms, &instance_state_),
        render_view_(rv) {
  }
  virtual ~HostDispatcherWrapper() {}

  bool Init(const IPC::ChannelHandle& channel_handle,
            PP_GetInterface_Func local_get_interface,
            const ppapi::Preferences& preferences,
            const ppapi::PpapiPermissions& permissions,
            PepperHungPluginFilter* filter) {
    if (channel_handle.name.empty())
      return false;

#if defined(OS_POSIX)
    DCHECK_NE(-1, channel_handle.socket.fd);
    if (channel_handle.socket.fd == -1)
      return false;
#endif

    dispatcher_delegate_.reset(new PepperProxyChannelDelegateImpl);
    dispatcher_.reset(new ppapi::proxy::HostDispatcher(
        module_->pp_module(), local_get_interface, filter));

    host_.reset(new ppapi::host::PpapiHost(dispatcher_.get(), &host_factory_,
                                           permissions));
    dispatcher_->AddFilter(host_.get());

    if (!dispatcher_->InitHostWithChannel(dispatcher_delegate_.get(),
                                          channel_handle,
                                          true,  // Client.
                                          preferences)) {
      dispatcher_.reset();
      dispatcher_delegate_.reset();
      return false;
    }
    dispatcher_->channel()->SetRestrictDispatchChannelGroup(
        content::kRendererRestrictDispatchGroup_Pepper);
    render_view_->PpapiPluginCreated(host_.get());
    return true;
  }

  // OutOfProcessProxy implementation.
  virtual const void* GetProxiedInterface(const char* name) {
    return dispatcher_->GetProxiedInterface(name);
  }
  virtual void AddInstance(PP_Instance instance) {
    ppapi::proxy::HostDispatcher::SetForInstance(instance, dispatcher_.get());
  }
  virtual void RemoveInstance(PP_Instance instance) {
    ppapi::proxy::HostDispatcher::RemoveForInstance(instance);
  }

 private:
  webkit::ppapi::PluginModule* module_;
  PepperInstanceStateAccessorImpl instance_state_;
  ContentRendererPepperHostFactory host_factory_;

  RenderViewImpl* render_view_;
  scoped_ptr<ppapi::host::PpapiHost> host_;

  scoped_ptr<ppapi::proxy::HostDispatcher> dispatcher_;
  scoped_ptr<ppapi::proxy::ProxyChannel::Delegate> dispatcher_delegate_;
};

class QuotaCallbackTranslator : public QuotaDispatcher::Callback {
 public:
  typedef webkit::ppapi::PluginDelegate::AvailableSpaceCallback PluginCallback;
  explicit QuotaCallbackTranslator(const PluginCallback& cb) : callback_(cb) {}
  virtual void DidQueryStorageUsageAndQuota(int64 usage, int64 quota) OVERRIDE {
    callback_.Run(std::max(static_cast<int64>(0), quota - usage));
  }
  virtual void DidGrantStorageQuota(int64 granted_quota) OVERRIDE {
    NOTREACHED();
  }
  virtual void DidFail(quota::QuotaStatusCode error) OVERRIDE {
    callback_.Run(0);
  }
 private:
  PluginCallback callback_;
};

class PluginInstanceLockTarget : public MouseLockDispatcher::LockTarget {
 public:
  PluginInstanceLockTarget(webkit::ppapi::PluginInstance* plugin)
      : plugin_(plugin) {}

  virtual void OnLockMouseACK(bool succeeded) OVERRIDE {
    plugin_->OnLockMouseACK(succeeded);
  }

  virtual void OnMouseLockLost() OVERRIDE {
    plugin_->OnMouseLockLost();
  }

  virtual bool HandleMouseLockedInputEvent(
      const WebKit::WebMouseEvent &event) OVERRIDE {
    plugin_->HandleMouseLockedInputEvent(event);
    return true;
  }

 private:
  webkit::ppapi::PluginInstance* plugin_;
};

class AsyncOpenFileSystemURLCallbackTranslator
    : public fileapi::FileSystemCallbackDispatcher {
 public:
  AsyncOpenFileSystemURLCallbackTranslator(
      const webkit::ppapi::PluginDelegate::AsyncOpenFileSystemURLCallback&
          callback,
      const webkit::ppapi::PluginDelegate::NotifyCloseFileCallback&
          close_file_callback)
    : callback_(callback),
      close_file_callback_(close_file_callback) {
  }

  virtual ~AsyncOpenFileSystemURLCallbackTranslator() {}

  virtual void DidSucceed() {
    NOTREACHED();
  }
  virtual void DidReadMetadata(
      const base::PlatformFileInfo& file_info,
      const FilePath& platform_path) {
    NOTREACHED();
  }
  virtual void DidReadDirectory(
      const std::vector<base::FileUtilProxy::Entry>& entries,
      bool has_more) {
    NOTREACHED();
  }
  virtual void DidOpenFileSystem(const std::string& name,
                                 const GURL& root) {
    NOTREACHED();
  }

  virtual void DidFail(base::PlatformFileError error_code) {
    base::PlatformFile invalid_file = base::kInvalidPlatformFileValue;
    callback_.Run(error_code,
                  base::PassPlatformFile(&invalid_file),
                  webkit::ppapi::PluginDelegate::NotifyCloseFileCallback());
  }

  virtual void DidWrite(int64 bytes, bool complete) {
    NOTREACHED();
  }

  virtual void DidOpenFile(base::PlatformFile file) {
    callback_.Run(base::PLATFORM_FILE_OK,
                  base::PassPlatformFile(&file),
                  close_file_callback_);
    // Make sure we won't leak file handle if the requester has died.
    if (file != base::kInvalidPlatformFileValue) {
      base::FileUtilProxy::Close(
          RenderThreadImpl::current()->GetFileThreadMessageLoopProxy(), file,
          close_file_callback_);
    }
  }

 private:
  webkit::ppapi::PluginDelegate::AsyncOpenFileSystemURLCallback callback_;
  webkit::ppapi::PluginDelegate::NotifyCloseFileCallback close_file_callback_;
};

void DoNotifyCloseFile(const GURL& path, base::PlatformFileError /* unused */) {
  ChildThread::current()->file_system_dispatcher()->NotifyCloseFile(path);
}

}  // namespace

PepperPluginDelegateImpl::PepperPluginDelegateImpl(RenderViewImpl* render_view)
    : RenderViewObserver(render_view),
      render_view_(render_view),
      has_saved_context_menu_action_(false),
      saved_context_menu_action_(0),
      focused_plugin_(NULL),
      last_mouse_event_target_(NULL),
      device_enumeration_event_handler_(
          new PepperDeviceEnumerationEventHandler()) {
}

PepperPluginDelegateImpl::~PepperPluginDelegateImpl() {
  DCHECK(mouse_lock_instances_.empty());
}

scoped_refptr<webkit::ppapi::PluginModule>
PepperPluginDelegateImpl::CreatePepperPluginModule(
    const webkit::WebPluginInfo& webplugin_info,
    bool* pepper_plugin_was_registered) {
  *pepper_plugin_was_registered = true;

  // See if a module has already been loaded for this plugin.
  FilePath path(webplugin_info.path);
  scoped_refptr<webkit::ppapi::PluginModule> module =
      PepperPluginRegistry::GetInstance()->GetLiveModule(path);
  if (module)
    return module;

  // In-process plugins will have always been created up-front to avoid the
  // sandbox restrictions. So getting here implies it doesn't exist or should
  // be out of process.
  const PepperPluginInfo* info =
      PepperPluginRegistry::GetInstance()->GetInfoForPlugin(webplugin_info);
  if (!info) {
    *pepper_plugin_was_registered = false;
    return scoped_refptr<webkit::ppapi::PluginModule>();
  } else if (!info->is_out_of_process) {
    // In-process plugin not preloaded, it probably couldn't be initialized.
    return scoped_refptr<webkit::ppapi::PluginModule>();
  }
  ppapi::PpapiPermissions permissions(info->permissions);

  // Out of process: have the browser start the plugin process for us.
  IPC::ChannelHandle channel_handle;
  int plugin_child_id = 0;
  render_view_->Send(new ViewHostMsg_OpenChannelToPepperPlugin(
      path, &channel_handle, &plugin_child_id));
  if (channel_handle.name.empty()) {
    // Couldn't be initialized.
    return scoped_refptr<webkit::ppapi::PluginModule>();
  }

  scoped_refptr<PepperHungPluginFilter> hung_filter(
      new PepperHungPluginFilter(path, render_view_->routing_id(),
                                 plugin_child_id));

  // Create a new HostDispatcher for the proxying, and hook it to a new
  // PluginModule. Note that AddLiveModule must be called before any early
  // returns since the module's destructor will remove itself.
  module = new webkit::ppapi::PluginModule(
      info->name, path,
      PepperPluginRegistry::GetInstance(),
      permissions);
  PepperPluginRegistry::GetInstance()->AddLiveModule(path, module);
  scoped_ptr<HostDispatcherWrapper> dispatcher(
      new HostDispatcherWrapper(render_view_, module, permissions));
  if (!dispatcher->Init(
          channel_handle,
          webkit::ppapi::PluginModule::GetLocalGetInterfaceFunc(),
          GetPreferences(),
          permissions,
          hung_filter.get()))
    return scoped_refptr<webkit::ppapi::PluginModule>();
  module->InitAsProxied(dispatcher.release());
  return module;
}

scoped_refptr<webkit::ppapi::PluginModule>
    PepperPluginDelegateImpl::CreateBrowserPluginModule(
        const IPC::ChannelHandle& channel_handle,
        int guest_process_id) {
  BrowserPluginRegistry* registry =
      RenderThreadImpl::current()->browser_plugin_registry();
  scoped_refptr<webkit::ppapi::PluginModule> module =
      registry->GetModule(guest_process_id);
  if (module)
    return module;

  ppapi::PpapiPermissions permissions;

  FilePath path(kBrowserPluginPath);
  scoped_refptr<PepperHungPluginFilter> hung_filter(
      new PepperHungPluginFilter(path,
                                 render_view_->routing_id(),
                                 guest_process_id));
  // Create a new HostDispatcher for the proxying, and hook it to a new
  // PluginModule.
  module = new webkit::ppapi::PluginModule(kBrowserPluginName,
                                           path,
                                           registry,
                                           permissions);
  RenderThreadImpl::current()->browser_plugin_registry()->AddModule(
      guest_process_id, module);
  scoped_ptr<HostDispatcherWrapper> dispatcher(
      new HostDispatcherWrapper(render_view_, module, permissions));
  if (!dispatcher->Init(
          channel_handle,
          webkit::ppapi::PluginModule::GetLocalGetInterfaceFunc(),
          GetPreferences(),
          permissions,
          hung_filter.get()))
    return scoped_refptr<webkit::ppapi::PluginModule>();
  module->InitAsProxied(dispatcher.release());
  return module;
}

scoped_refptr<PepperBrokerImpl> PepperPluginDelegateImpl::CreateBroker(
    webkit::ppapi::PluginModule* plugin_module) {
  DCHECK(plugin_module);
  DCHECK(!plugin_module->GetBroker());

  // The broker path is the same as the plugin.
  const FilePath& broker_path = plugin_module->path();

  scoped_refptr<PepperBrokerImpl> broker =
      new PepperBrokerImpl(plugin_module, this);

  int request_id =
      pending_connect_broker_.Add(new scoped_refptr<PepperBrokerImpl>(broker));

  // Have the browser start the broker process for us.
  IPC::Message* msg =
      new ViewHostMsg_OpenChannelToPpapiBroker(render_view_->routing_id(),
                                               request_id,
                                               broker_path);
  if (!render_view_->Send(msg)) {
    pending_connect_broker_.Remove(request_id);
    return scoped_refptr<PepperBrokerImpl>();
  }

  return broker;
}

void PepperPluginDelegateImpl::OnPpapiBrokerChannelCreated(
    int request_id,
    const IPC::ChannelHandle& handle) {
  scoped_refptr<PepperBrokerImpl>* broker_ptr =
      pending_connect_broker_.Lookup(request_id);
  if (broker_ptr) {
    scoped_refptr<PepperBrokerImpl> broker = *broker_ptr;
    pending_connect_broker_.Remove(request_id);
    broker->OnBrokerChannelConnected(handle);
  } else {
    // There is no broker waiting for this channel. Close it so the broker can
    // clean up and possibly exit.
    // The easiest way to clean it up is to just put it in an object
    // and then close them. This failure case is not performance critical.
    PepperBrokerDispatcherWrapper temp_dispatcher;
    temp_dispatcher.Init(handle);
  }
}

// Iterates through pending_connect_broker_ to find the broker.
// Cannot use Lookup() directly because pending_connect_broker_ does not store
// the raw pointer to the broker. Assumes maximum of one copy of broker exists.
bool PepperPluginDelegateImpl::StopWaitingForBrokerConnection(
    PepperBrokerImpl* broker) {
  for (BrokerMap::iterator i(&pending_connect_broker_);
       !i.IsAtEnd(); i.Advance()) {
    if (i.GetCurrentValue()->get() == broker) {
      pending_connect_broker_.Remove(i.GetCurrentKey());
      return true;
    }
  }
  return false;
}

void PepperPluginDelegateImpl::ViewWillInitiatePaint() {
  // Notify all of our instances that we started painting. This is used for
  // internal bookkeeping only, so we know that the set can not change under
  // us.
  for (std::set<webkit::ppapi::PluginInstance*>::iterator i =
           active_instances_.begin();
       i != active_instances_.end(); ++i)
    (*i)->ViewWillInitiatePaint();
}

void PepperPluginDelegateImpl::ViewInitiatedPaint() {
  // Notify all instances that we painted.  The same caveats apply as for
  // ViewFlushedPaint regarding instances closing themselves, so we take
  // similar precautions.
  std::set<webkit::ppapi::PluginInstance*> plugins = active_instances_;
  for (std::set<webkit::ppapi::PluginInstance*>::iterator i = plugins.begin();
       i != plugins.end(); ++i) {
    if (active_instances_.find(*i) != active_instances_.end())
      (*i)->ViewInitiatedPaint();
  }
}

void PepperPluginDelegateImpl::ViewFlushedPaint() {
  // Notify all instances that we flushed. This will call into the plugin, and
  // we it may ask to close itself as a result. This will, in turn, modify our
  // set, possibly invalidating the iterator. So we iterate on a copy that
  // won't change out from under us.
  std::set<webkit::ppapi::PluginInstance*> plugins = active_instances_;
  for (std::set<webkit::ppapi::PluginInstance*>::iterator i = plugins.begin();
       i != plugins.end(); ++i) {
    // The copy above makes sure our iterator is never invalid if some plugins
    // are destroyed. But some plugin may decide to close all of its views in
    // response to a paint in one of them, so we need to make sure each one is
    // still "current" before using it.
    //
    // It's possible that a plugin was destroyed, but another one was created
    // with the same address. In this case, we'll call ViewFlushedPaint on that
    // new plugin. But that's OK for this particular case since we're just
    // notifying all of our instances that the view flushed, and the new one is
    // one of our instances.
    //
    // What about the case where a new one is created in a callback at a new
    // address and we don't issue the callback? We're still OK since this
    // callback is used for flush callbacks and we could not have possibly
    // started a new paint (ViewWillInitiatePaint) for the new plugin while
    // processing a previous paint for an existing one.
    if (active_instances_.find(*i) != active_instances_.end())
      (*i)->ViewFlushedPaint();
  }
}

webkit::ppapi::PluginInstance*
PepperPluginDelegateImpl::GetBitmapForOptimizedPluginPaint(
    const gfx::Rect& paint_bounds,
    TransportDIB** dib,
    gfx::Rect* location,
    gfx::Rect* clip) {
  for (std::set<webkit::ppapi::PluginInstance*>::iterator i =
           active_instances_.begin();
       i != active_instances_.end(); ++i) {
    webkit::ppapi::PluginInstance* instance = *i;
    if (instance->GetBitmapForOptimizedPluginPaint(
            paint_bounds, dib, location, clip))
      return *i;
  }
  return NULL;
}

void PepperPluginDelegateImpl::PluginFocusChanged(
    webkit::ppapi::PluginInstance* instance,
    bool focused) {
  if (focused)
    focused_plugin_ = instance;
  else if (focused_plugin_ == instance)
    focused_plugin_ = NULL;
  if (render_view_)
    render_view_->PpapiPluginFocusChanged();
}

void PepperPluginDelegateImpl::PluginTextInputTypeChanged(
    webkit::ppapi::PluginInstance* instance) {
  if (focused_plugin_ == instance && render_view_)
    render_view_->PpapiPluginTextInputTypeChanged();
}

void PepperPluginDelegateImpl::PluginCaretPositionChanged(
    webkit::ppapi::PluginInstance* instance) {
  if (focused_plugin_ == instance && render_view_)
    render_view_->PpapiPluginCaretPositionChanged();
}

void PepperPluginDelegateImpl::PluginRequestedCancelComposition(
    webkit::ppapi::PluginInstance* instance) {
  if (focused_plugin_ == instance && render_view_)
    render_view_->PpapiPluginCancelComposition();
}

void PepperPluginDelegateImpl::PluginSelectionChanged(
    webkit::ppapi::PluginInstance* instance) {
  if (focused_plugin_ == instance && render_view_)
    render_view_->PpapiPluginSelectionChanged();
}

void PepperPluginDelegateImpl::SimulateImeSetComposition(
    const string16& text,
    const std::vector<WebKit::WebCompositionUnderline>& underlines,
    int selection_start,
    int selection_end) {
  if (render_view_) {
    render_view_->SimulateImeSetComposition(
        text, underlines, selection_start, selection_end);
  }
}

void PepperPluginDelegateImpl::SimulateImeConfirmComposition(
    const string16& text) {
  if (render_view_)
    render_view_->SimulateImeConfirmComposition(text, ui::Range());
}

void PepperPluginDelegateImpl::OnImeSetComposition(
    const string16& text,
    const std::vector<WebKit::WebCompositionUnderline>& underlines,
    int selection_start,
    int selection_end) {
  if (!IsPluginAcceptingCompositionEvents()) {
    composition_text_ = text;
  } else {
    // TODO(kinaba) currently all composition events are sent directly to
    // plugins. Use DOM event mechanism after WebKit is made aware about
    // plugins that support composition.
    // The code below mimics the behavior of WebCore::Editor::setComposition.

    // Empty -> nonempty: composition started.
    if (composition_text_.empty() && !text.empty())
      focused_plugin_->HandleCompositionStart(string16());
    // Nonempty -> empty: composition canceled.
    if (!composition_text_.empty() && text.empty())
       focused_plugin_->HandleCompositionEnd(string16());
    composition_text_ = text;
    // Nonempty: composition is ongoing.
    if (!composition_text_.empty()) {
      focused_plugin_->HandleCompositionUpdate(composition_text_, underlines,
                                               selection_start, selection_end);
    }
  }
}

void PepperPluginDelegateImpl::OnImeConfirmComposition(const string16& text) {
  // Here, text.empty() has a special meaning. It means to commit the last
  // update of composition text (see RenderWidgetHost::ImeConfirmComposition()).
  const string16& last_text = text.empty() ? composition_text_ : text;

  // last_text is empty only when both text and composition_text_ is. Ignore it.
  if (last_text.empty())
    return;

  if (!IsPluginAcceptingCompositionEvents()) {
    for (size_t i = 0; i < text.size(); ++i) {
      WebKit::WebKeyboardEvent char_event;
      char_event.type = WebKit::WebInputEvent::Char;
      char_event.timeStampSeconds = base::Time::Now().ToDoubleT();
      char_event.modifiers = 0;
      char_event.windowsKeyCode = last_text[i];
      char_event.nativeKeyCode = last_text[i];
      char_event.text[0] = last_text[i];
      char_event.unmodifiedText[0] = last_text[i];
      if (render_view_->webwidget())
        render_view_->webwidget()->handleInputEvent(char_event);
    }
  } else {
    // Mimics the order of events sent by WebKit.
    // See WebCore::Editor::setComposition() for the corresponding code.
    focused_plugin_->HandleCompositionEnd(last_text);
    focused_plugin_->HandleTextInput(last_text);
  }
  composition_text_.clear();
}

gfx::Rect PepperPluginDelegateImpl::GetCaretBounds() const {
  if (!focused_plugin_)
    return gfx::Rect(0, 0, 0, 0);
  return focused_plugin_->GetCaretBounds();
}

ui::TextInputType PepperPluginDelegateImpl::GetTextInputType() const {
  if (!focused_plugin_)
    return ui::TEXT_INPUT_TYPE_NONE;
  return focused_plugin_->text_input_type();
}

void PepperPluginDelegateImpl::GetSurroundingText(string16* text,
                                                  ui::Range* range) const {
  if (!focused_plugin_)
    return;
  return focused_plugin_->GetSurroundingText(text, range);
}

bool PepperPluginDelegateImpl::IsPluginAcceptingCompositionEvents() const {
  if (!focused_plugin_)
    return false;
  return focused_plugin_->IsPluginAcceptingCompositionEvents();
}

bool PepperPluginDelegateImpl::CanComposeInline() const {
  return IsPluginAcceptingCompositionEvents();
}

void PepperPluginDelegateImpl::PluginCrashed(
    webkit::ppapi::PluginInstance* instance) {
  render_view_->PluginCrashed(instance->module()->path());
  UnSetAndDeleteLockTargetAdapter(instance);
}

void PepperPluginDelegateImpl::InstanceCreated(
    webkit::ppapi::PluginInstance* instance) {
  active_instances_.insert(instance);

  // Set the initial focus.
  instance->SetContentAreaFocus(render_view_->has_focus());
}

void PepperPluginDelegateImpl::InstanceDeleted(
    webkit::ppapi::PluginInstance* instance) {
  active_instances_.erase(instance);
  UnSetAndDeleteLockTargetAdapter(instance);

  if (last_mouse_event_target_ == instance)
    last_mouse_event_target_ = NULL;
  if (focused_plugin_ == instance)
    PluginFocusChanged(instance, false);
}

scoped_ptr< ::ppapi::thunk::ResourceCreationAPI>
PepperPluginDelegateImpl::CreateResourceCreationAPI(
    webkit::ppapi::PluginInstance* instance) {
  return scoped_ptr< ::ppapi::thunk::ResourceCreationAPI>(
      new PepperInProcessResourceCreation(render_view_, instance,
                                          instance->module()->permissions()));
}

SkBitmap* PepperPluginDelegateImpl::GetSadPluginBitmap() {
  return GetContentClient()->renderer()->GetSadPluginBitmap();
}

WebKit::WebPlugin* PepperPluginDelegateImpl::CreatePluginReplacement(
    const FilePath& file_path) {
  return GetContentClient()->renderer()->CreatePluginReplacement(
      render_view_, file_path);
}

webkit::ppapi::PluginDelegate::PlatformImage2D*
PepperPluginDelegateImpl::CreateImage2D(int width, int height) {
  return PepperPlatformImage2DImpl::Create(width, height);
}

webkit::ppapi::PluginDelegate::PlatformContext3D*
    PepperPluginDelegateImpl::CreateContext3D() {
#ifdef ENABLE_GPU
  // If accelerated compositing of plugins is disabled, fail to create a 3D
  // context, because it won't be visible. This allows graceful fallback in the
  // modules.
  if (!render_view_->webkit_preferences().accelerated_plugins_enabled)
    return NULL;
  return new PlatformContext3DImpl(this);
#else
  return NULL;
#endif
}

webkit::ppapi::PluginDelegate::PlatformVideoCapture*
PepperPluginDelegateImpl::CreateVideoCapture(
    const std::string& device_id,
    PlatformVideoCaptureEventHandler* handler) {
  return new PepperPlatformVideoCaptureImpl(AsWeakPtr(), device_id, handler);
}

webkit::ppapi::PluginDelegate::PlatformVideoDecoder*
PepperPluginDelegateImpl::CreateVideoDecoder(
    media::VideoDecodeAccelerator::Client* client,
    int32 command_buffer_route_id) {
  return new PlatformVideoDecoderImpl(client, command_buffer_route_id);
}

void PepperPluginDelegateImpl::NumberOfFindResultsChanged(int identifier,
                                                          int total,
                                                          bool final_result) {
  render_view_->reportFindInPageMatchCount(identifier, total, final_result);
}

void PepperPluginDelegateImpl::SelectedFindResultChanged(int identifier,
                                                         int index) {
  render_view_->reportFindInPageSelection(
      identifier, index + 1, WebKit::WebRect());
}

uint32_t PepperPluginDelegateImpl::GetAudioHardwareOutputSampleRate() {
  return static_cast<uint32_t>(audio_hardware::GetOutputSampleRate());
}

uint32_t PepperPluginDelegateImpl::GetAudioHardwareOutputBufferSize() {
  return static_cast<uint32_t>(audio_hardware::GetOutputBufferSize());
}

webkit::ppapi::PluginDelegate::PlatformAudioOutput*
PepperPluginDelegateImpl::CreateAudioOutput(
    uint32_t sample_rate,
    uint32_t sample_count,
    webkit::ppapi::PluginDelegate::PlatformAudioOutputClient* client) {
  return PepperPlatformAudioOutputImpl::Create(
      static_cast<int>(sample_rate), static_cast<int>(sample_count), client);
}

webkit::ppapi::PluginDelegate::PlatformAudioInput*
PepperPluginDelegateImpl::CreateAudioInput(
    const std::string& device_id,
    uint32_t sample_rate,
    uint32_t sample_count,
    webkit::ppapi::PluginDelegate::PlatformAudioInputClient* client) {
  return PepperPlatformAudioInputImpl::Create(
      AsWeakPtr(), device_id, static_cast<int>(sample_rate),
      static_cast<int>(sample_count), client);
}

// If a broker has not already been created for this plugin, creates one.
webkit::ppapi::PluginDelegate::Broker*
PepperPluginDelegateImpl::ConnectToBroker(
    webkit::ppapi::PPB_Broker_Impl* client) {
  DCHECK(client);

  // If a broker needs to be created, this will ensure it does not get deleted
  // before Connect() adds a reference.
  scoped_refptr<PepperBrokerImpl> broker_impl;

  webkit::ppapi::PluginModule* plugin_module =
      webkit::ppapi::ResourceHelper::GetPluginModule(client);
  if (!plugin_module)
    return NULL;

  webkit::ppapi::PluginDelegate::Broker* broker = plugin_module->GetBroker();
  if (!broker) {
    broker_impl = CreateBroker(plugin_module);
    if (!broker_impl.get())
      return NULL;
    broker = broker_impl;
  }

  // Adds a reference, ensuring not deleted when broker_impl goes out of scope.
  broker->Connect(client);
  return broker;
}

bool PepperPluginDelegateImpl::AsyncOpenFile(
    const FilePath& path,
    int flags,
    const AsyncOpenFileCallback& callback) {
  int message_id = pending_async_open_files_.Add(
      new AsyncOpenFileCallback(callback));
  IPC::Message* msg = new ViewHostMsg_AsyncOpenFile(
      render_view_->routing_id(), path, flags, message_id);
  return render_view_->Send(msg);
}

void PepperPluginDelegateImpl::OnAsyncFileOpened(
    base::PlatformFileError error_code,
    base::PlatformFile file,
    int message_id) {
  AsyncOpenFileCallback* callback =
      pending_async_open_files_.Lookup(message_id);
  DCHECK(callback);
  pending_async_open_files_.Remove(message_id);
  callback->Run(error_code, base::PassPlatformFile(&file));
  // Make sure we won't leak file handle if the requester has died.
  if (file != base::kInvalidPlatformFileValue)
    base::FileUtilProxy::Close(GetFileThreadMessageLoopProxy(), file,
                               base::FileUtilProxy::StatusCallback());
  delete callback;
}

void PepperPluginDelegateImpl::OnSetFocus(bool has_focus) {
  for (std::set<webkit::ppapi::PluginInstance*>::iterator i =
           active_instances_.begin();
       i != active_instances_.end(); ++i)
    (*i)->SetContentAreaFocus(has_focus);
}

void PepperPluginDelegateImpl::PageVisibilityChanged(bool is_visible) {
  for (std::set<webkit::ppapi::PluginInstance*>::iterator i =
           active_instances_.begin();
       i != active_instances_.end(); ++i)
    (*i)->PageVisibilityChanged(is_visible);
}

bool PepperPluginDelegateImpl::IsPluginFocused() const {
  return focused_plugin_ != NULL;
}

void PepperPluginDelegateImpl::WillHandleMouseEvent() {
  // This method is called for every mouse event that the render view receives.
  // And then the mouse event is forwarded to WebKit, which dispatches it to the
  // event target. Potentially a Pepper plugin will receive the event.
  // In order to tell whether a plugin gets the last mouse event and which it
  // is, we set |last_mouse_event_target_| to NULL here. If a plugin gets the
  // event, it will notify us via DidReceiveMouseEvent() and set itself as
  // |last_mouse_event_target_|.
  last_mouse_event_target_ = NULL;
}

bool PepperPluginDelegateImpl::OpenFileSystem(
    const GURL& origin_url,
    fileapi::FileSystemType type,
    long long size,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->OpenFileSystem(
      origin_url, type, size, true /* create */, dispatcher);
}

bool PepperPluginDelegateImpl::MakeDirectory(
    const GURL& path,
    bool recursive,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->Create(
      path, false, true, recursive, dispatcher);
}

bool PepperPluginDelegateImpl::Query(
    const GURL& path,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->ReadMetadata(path, dispatcher);
}

bool PepperPluginDelegateImpl::Touch(
    const GURL& path,
    const base::Time& last_access_time,
    const base::Time& last_modified_time,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->TouchFile(path, last_access_time,
                                           last_modified_time, dispatcher);
}

bool PepperPluginDelegateImpl::Delete(
    const GURL& path,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->Remove(path, false /* recursive */,
                                        dispatcher);
}

bool PepperPluginDelegateImpl::Rename(
    const GURL& file_path,
    const GURL& new_file_path,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->Move(file_path, new_file_path, dispatcher);
}

bool PepperPluginDelegateImpl::ReadDirectory(
    const GURL& directory_path,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->ReadDirectory(directory_path, dispatcher);
}

void PepperPluginDelegateImpl::QueryAvailableSpace(
    const GURL& origin, quota::StorageType type,
    const AvailableSpaceCallback& callback) {
  ChildThread::current()->quota_dispatcher()->QueryStorageUsageAndQuota(
      origin, type, new QuotaCallbackTranslator(callback));
}

void PepperPluginDelegateImpl::WillUpdateFile(const GURL& path) {
  ChildThread::current()->Send(new FileSystemHostMsg_WillUpdate(path));
}

void PepperPluginDelegateImpl::DidUpdateFile(const GURL& path, int64_t delta) {
  ChildThread::current()->Send(new FileSystemHostMsg_DidUpdate(path, delta));
}

bool PepperPluginDelegateImpl::AsyncOpenFileSystemURL(
    const GURL& path,
    int flags,
    const AsyncOpenFileSystemURLCallback& callback) {

  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->OpenFile(path, flags,
      new AsyncOpenFileSystemURLCallbackTranslator(
          callback,
          base::Bind(&DoNotifyCloseFile, path)));
}

base::PlatformFileError PepperPluginDelegateImpl::OpenFile(
    const ppapi::PepperFilePath& path,
    int flags,
    base::PlatformFile* file) {
  IPC::PlatformFileForTransit transit_file;
  base::PlatformFileError error;
  IPC::Message* msg = new PepperFileMsg_OpenFile(
      path, flags, &error, &transit_file);
  if (!render_view_->Send(msg)) {
    *file = base::kInvalidPlatformFileValue;
    return base::PLATFORM_FILE_ERROR_FAILED;
  }
  *file = IPC::PlatformFileForTransitToPlatformFile(transit_file);
  return error;
}

base::PlatformFileError PepperPluginDelegateImpl::RenameFile(
    const ppapi::PepperFilePath& from_path,
    const ppapi::PepperFilePath& to_path) {
  base::PlatformFileError error;
  IPC::Message* msg = new PepperFileMsg_RenameFile(from_path, to_path, &error);
  if (!render_view_->Send(msg))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return error;
}

base::PlatformFileError PepperPluginDelegateImpl::DeleteFileOrDir(
    const ppapi::PepperFilePath& path,
    bool recursive) {
  base::PlatformFileError error;
  IPC::Message* msg = new PepperFileMsg_DeleteFileOrDir(
      path, recursive, &error);
  if (!render_view_->Send(msg))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return error;
}

base::PlatformFileError PepperPluginDelegateImpl::CreateDir(
    const ppapi::PepperFilePath& path) {
  base::PlatformFileError error;
  IPC::Message* msg = new PepperFileMsg_CreateDir(path, &error);
  if (!render_view_->Send(msg))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return error;
}

base::PlatformFileError PepperPluginDelegateImpl::QueryFile(
    const ppapi::PepperFilePath& path,
    base::PlatformFileInfo* info) {
  base::PlatformFileError error;
  IPC::Message* msg = new PepperFileMsg_QueryFile(path, info, &error);
  if (!render_view_->Send(msg))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return error;
}

base::PlatformFileError PepperPluginDelegateImpl::GetDirContents(
    const ppapi::PepperFilePath& path,
    ppapi::DirContents* contents) {
  base::PlatformFileError error;
  IPC::Message* msg = new PepperFileMsg_GetDirContents(path, contents, &error);
  if (!render_view_->Send(msg))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return error;
}

base::PlatformFileError PepperPluginDelegateImpl::CreateTemporaryFile(
    base::PlatformFile* file) {
  IPC::PlatformFileForTransit transit_file;
  base::PlatformFileError error;
  IPC::Message* msg = new PepperFileMsg_CreateTemporaryFile(&error,
                                                            &transit_file);
  if (!render_view_->Send(msg)) {
    *file = base::kInvalidPlatformFileValue;
    return base::PLATFORM_FILE_ERROR_FAILED;
  }
  *file = IPC::PlatformFileForTransitToPlatformFile(transit_file);
  return error;
}

void PepperPluginDelegateImpl::SyncGetFileSystemPlatformPath(
    const GURL& url, FilePath* platform_path) {
  RenderThreadImpl::current()->Send(new FileSystemHostMsg_SyncGetPlatformPath(
      url, platform_path));
}

scoped_refptr<base::MessageLoopProxy>
PepperPluginDelegateImpl::GetFileThreadMessageLoopProxy() {
  return RenderThreadImpl::current()->GetFileThreadMessageLoopProxy();
}

uint32 PepperPluginDelegateImpl::TCPSocketCreate() {
  uint32 socket_id = 0;
  render_view_->Send(new PpapiHostMsg_PPBTCPSocket_Create(
      render_view_->routing_id(), 0, &socket_id));
  return socket_id;
}

void PepperPluginDelegateImpl::TCPSocketConnect(
    webkit::ppapi::PPB_TCPSocket_Private_Impl* socket,
    uint32 socket_id,
    const std::string& host,
    uint16_t port) {
  RegisterTCPSocket(socket, socket_id);
  render_view_->Send(
      new PpapiHostMsg_PPBTCPSocket_Connect(
          render_view_->routing_id(), socket_id, host, port));
}

void PepperPluginDelegateImpl::TCPSocketConnectWithNetAddress(
      webkit::ppapi::PPB_TCPSocket_Private_Impl* socket,
      uint32 socket_id,
      const PP_NetAddress_Private& addr) {
  RegisterTCPSocket(socket, socket_id);
  render_view_->Send(
      new PpapiHostMsg_PPBTCPSocket_ConnectWithNetAddress(
          render_view_->routing_id(), socket_id, addr));
}

void PepperPluginDelegateImpl::TCPSocketSSLHandshake(
    uint32 socket_id,
    const std::string& server_name,
    uint16_t server_port,
    const std::vector<std::vector<char> >& trusted_certs,
    const std::vector<std::vector<char> >& untrusted_certs) {
  DCHECK(tcp_sockets_.Lookup(socket_id));
  render_view_->Send(new PpapiHostMsg_PPBTCPSocket_SSLHandshake(
      socket_id, server_name, server_port, trusted_certs, untrusted_certs));
}

void PepperPluginDelegateImpl::TCPSocketRead(uint32 socket_id,
                                             int32_t bytes_to_read) {
  DCHECK(tcp_sockets_.Lookup(socket_id));
  render_view_->Send(
      new PpapiHostMsg_PPBTCPSocket_Read(socket_id, bytes_to_read));
}

void PepperPluginDelegateImpl::TCPSocketWrite(uint32 socket_id,
                                              const std::string& buffer) {
  DCHECK(tcp_sockets_.Lookup(socket_id));
  render_view_->Send(new PpapiHostMsg_PPBTCPSocket_Write(socket_id, buffer));
}

void PepperPluginDelegateImpl::TCPSocketDisconnect(uint32 socket_id) {
  // There are no DCHECK(tcp_sockets_.Lookup(socket_id)) because it
  // can be called before
  // TCPSocketConnect/TCPSocketConnectWithNetAddress is called.
  render_view_->Send(new PpapiHostMsg_PPBTCPSocket_Disconnect(socket_id));
  if (tcp_sockets_.Lookup(socket_id))
    tcp_sockets_.Remove(socket_id);
}

void PepperPluginDelegateImpl::RegisterTCPSocket(
    webkit::ppapi::PPB_TCPSocket_Private_Impl* socket,
    uint32 socket_id) {
  tcp_sockets_.AddWithID(socket, socket_id);
}

uint32 PepperPluginDelegateImpl::UDPSocketCreate() {
  uint32 socket_id = 0;
  render_view_->Send(new PpapiHostMsg_PPBUDPSocket_Create(
      render_view_->routing_id(), 0, &socket_id));
  return socket_id;
}

void PepperPluginDelegateImpl::UDPSocketBind(
    webkit::ppapi::PPB_UDPSocket_Private_Impl* socket,
    uint32 socket_id,
    const PP_NetAddress_Private& addr) {
  udp_sockets_.AddWithID(socket, socket_id);
  render_view_->Send(new PpapiHostMsg_PPBUDPSocket_Bind(
      render_view_->routing_id(), socket_id, addr));
}

void PepperPluginDelegateImpl::UDPSocketRecvFrom(uint32 socket_id,
                                                 int32_t num_bytes) {
  DCHECK(udp_sockets_.Lookup(socket_id));
  render_view_->Send(
      new PpapiHostMsg_PPBUDPSocket_RecvFrom(socket_id, num_bytes));
}

void PepperPluginDelegateImpl::UDPSocketSendTo(
    uint32 socket_id,
    const std::string& buffer,
    const PP_NetAddress_Private& net_addr) {
  DCHECK(udp_sockets_.Lookup(socket_id));
  render_view_->Send(
      new PpapiHostMsg_PPBUDPSocket_SendTo(socket_id, buffer, net_addr));
}

void PepperPluginDelegateImpl::UDPSocketClose(uint32 socket_id) {
  // There are no DCHECK(udp_sockets_.Lookup(socket_id)) because it
  // can be called before UDPSocketBind is called.
  render_view_->Send(new PpapiHostMsg_PPBUDPSocket_Close(socket_id));
  if (udp_sockets_.Lookup(socket_id))
    udp_sockets_.Remove(socket_id);
}

void PepperPluginDelegateImpl::TCPServerSocketListen(
    PP_Resource socket_resource,
    const PP_NetAddress_Private& addr,
    int32_t backlog) {
  render_view_->Send(
      new PpapiHostMsg_PPBTCPServerSocket_Listen(
          render_view_->routing_id(), 0, socket_resource, addr, backlog));
}

void PepperPluginDelegateImpl::TCPServerSocketAccept(uint32 server_socket_id) {
  DCHECK(tcp_server_sockets_.Lookup(server_socket_id));
  render_view_->Send(new PpapiHostMsg_PPBTCPServerSocket_Accept(
      render_view_->routing_id(), server_socket_id));
}

void PepperPluginDelegateImpl::TCPServerSocketStopListening(
    PP_Resource socket_resource,
    uint32 socket_id) {
  if (socket_id != 0) {
    render_view_->Send(new PpapiHostMsg_PPBTCPServerSocket_Destroy(socket_id));
    tcp_server_sockets_.Remove(socket_id);
  }
}

void PepperPluginDelegateImpl::RegisterHostResolver(
    ppapi::PPB_HostResolver_Shared* host_resolver,
    uint32 host_resolver_id) {
  host_resolvers_.AddWithID(host_resolver, host_resolver_id);
}

void PepperPluginDelegateImpl::HostResolverResolve(
    uint32 host_resolver_id,
    const ::ppapi::HostPortPair& host_port,
    const PP_HostResolver_Private_Hint* hint) {
  DCHECK(host_resolvers_.Lookup(host_resolver_id));
  if (!hint) {
    PP_HostResolver_Private_Hint empty_hint;
    empty_hint.family = PP_NETADDRESSFAMILY_UNSPECIFIED;
    empty_hint.flags = static_cast<PP_HostResolver_Private_Flags>(0);
    render_view_->Send(
        new PpapiHostMsg_PPBHostResolver_Resolve(
            GetRoutingID(),
            0,
            host_resolver_id,
            host_port,
            empty_hint));
  } else {
    render_view_->Send(
        new PpapiHostMsg_PPBHostResolver_Resolve(
            GetRoutingID(),
            0,
            host_resolver_id,
            host_port,
            *hint));
  }
}

void PepperPluginDelegateImpl::UnregisterHostResolver(uint32 host_resolver_id) {
  host_resolvers_.Remove(host_resolver_id);
}

bool PepperPluginDelegateImpl::AddNetworkListObserver(
    webkit_glue::NetworkListObserver* observer) {
#if defined(ENABLE_WEBRTC)
  P2PSocketDispatcher* socket_dispatcher =
      render_view_->p2p_socket_dispatcher();
  if (!socket_dispatcher) {
    return false;
  }
  socket_dispatcher->AddNetworkListObserver(observer);
  return true;
#else
  return false;
#endif
}

void PepperPluginDelegateImpl::RemoveNetworkListObserver(
    webkit_glue::NetworkListObserver* observer) {
#if defined(ENABLE_WEBRTC)
  P2PSocketDispatcher* socket_dispatcher =
      render_view_->p2p_socket_dispatcher();
  if (socket_dispatcher)
    socket_dispatcher->RemoveNetworkListObserver(observer);
#endif
}

bool PepperPluginDelegateImpl::X509CertificateParseDER(
    const std::vector<char>& der,
    ppapi::PPB_X509Certificate_Fields* fields) {
  bool succeeded = false;
  render_view_->Send(
      new PpapiHostMsg_PPBX509Certificate_ParseDER(der, &succeeded, fields));
  return succeeded;
}

int32_t PepperPluginDelegateImpl::ShowContextMenu(
    webkit::ppapi::PluginInstance* instance,
    webkit::ppapi::PPB_Flash_Menu_Impl* menu,
    const gfx::Point& position) {
  int32 render_widget_id = render_view_->routing_id();
  if (instance->flash_fullscreen()) {
    webkit::ppapi::FullscreenContainer* container =
        instance->fullscreen_container();
    DCHECK(container);
    render_widget_id =
        static_cast<RenderWidgetFullscreenPepper*>(container)->routing_id();
  }

  int request_id = pending_context_menus_.Add(
      new scoped_refptr<webkit::ppapi::PPB_Flash_Menu_Impl>(menu));

  ContextMenuParams params;
  params.x = position.x();
  params.y = position.y();
  params.custom_context.is_pepper_menu = true;
  params.custom_context.request_id = request_id;
  params.custom_context.render_widget_id = render_widget_id;
  params.custom_items = menu->menu_data();

  // Transform the position to be in render view's coordinates.
  if (instance->view_data().is_fullscreen || instance->flash_fullscreen()) {
    WebKit::WebRect window_rect = render_view_->windowRect();
    WebKit::WebRect screen_rect = render_view_->screenInfo().rect;
    params.x = params.x - window_rect.x + screen_rect.x;
    params.y = params.y - window_rect.y + screen_rect.y;
  } else {
    params.x += instance->view_data().rect.point.x;
    params.y += instance->view_data().rect.point.y;
  }

  IPC::Message* msg = new ViewHostMsg_ContextMenu(render_view_->routing_id(),
                                                  params);
  if (!render_view_->Send(msg)) {
    pending_context_menus_.Remove(request_id);
    return PP_ERROR_FAILED;
  }

  return PP_OK_COMPLETIONPENDING;
}

void PepperPluginDelegateImpl::OnContextMenuClosed(
    const CustomContextMenuContext& custom_context) {
  int request_id = custom_context.request_id;
  scoped_refptr<webkit::ppapi::PPB_Flash_Menu_Impl>* menu_ptr =
      pending_context_menus_.Lookup(request_id);
  if (!menu_ptr) {
    NOTREACHED() << "CompleteShowContextMenu() called twice for the same menu.";
    return;
  }
  scoped_refptr<webkit::ppapi::PPB_Flash_Menu_Impl> menu = *menu_ptr;
  DCHECK(menu.get());
  pending_context_menus_.Remove(request_id);

  if (has_saved_context_menu_action_) {
    menu->CompleteShow(PP_OK, saved_context_menu_action_);
    has_saved_context_menu_action_ = false;
    saved_context_menu_action_ = 0;
  } else {
    menu->CompleteShow(PP_ERROR_USERCANCEL, 0);
  }
}

void PepperPluginDelegateImpl::OnCustomContextMenuAction(
    const CustomContextMenuContext& custom_context,
    unsigned action) {
  // Just save the action.
  DCHECK(!has_saved_context_menu_action_);
  has_saved_context_menu_action_ = true;
  saved_context_menu_action_ = action;
}

webkit::ppapi::FullscreenContainer*
PepperPluginDelegateImpl::CreateFullscreenContainer(
    webkit::ppapi::PluginInstance* instance) {
  return render_view_->CreatePepperFullscreenContainer(instance);
}

gfx::Size PepperPluginDelegateImpl::GetScreenSize() {
  WebKit::WebScreenInfo info = render_view_->screenInfo();
  return gfx::Size(info.rect.width, info.rect.height);
}

std::string PepperPluginDelegateImpl::GetDefaultEncoding() {
  return render_view_->webkit_preferences().default_encoding;
}

void PepperPluginDelegateImpl::ZoomLimitsChanged(double minimum_factor,
                                                 double maximum_factor) {
  double minimum_level = WebView::zoomFactorToZoomLevel(minimum_factor);
  double maximum_level = WebView::zoomFactorToZoomLevel(maximum_factor);
  render_view_->webview()->zoomLimitsChanged(minimum_level, maximum_level);
}

std::string PepperPluginDelegateImpl::ResolveProxy(const GURL& url) {
  bool result;
  std::string proxy_result;
  RenderThreadImpl::current()->Send(
      new ViewHostMsg_ResolveProxy(url, &result, &proxy_result));
  return proxy_result;
}

void PepperPluginDelegateImpl::DidStartLoading() {
  render_view_->DidStartLoadingForPlugin();
}

void PepperPluginDelegateImpl::DidStopLoading() {
  render_view_->DidStopLoadingForPlugin();
}

void PepperPluginDelegateImpl::SetContentRestriction(int restrictions) {
  render_view_->Send(new ViewHostMsg_UpdateContentRestrictions(
      render_view_->routing_id(), restrictions));
}

void PepperPluginDelegateImpl::SaveURLAs(const GURL& url) {
  WebFrame* frame = render_view_->webview()->mainFrame();
  content::Referrer referrer(frame->document().url(),
                             frame->document().referrerPolicy());
  render_view_->Send(new ViewHostMsg_SaveURLAs(
      render_view_->routing_id(), url, referrer));
}

webkit_glue::P2PTransport* PepperPluginDelegateImpl::CreateP2PTransport() {
#if defined(ENABLE_P2P_APIS)
  return new P2PTransportImpl(render_view_->p2p_socket_dispatcher());
#else
  return NULL;
#endif
}

double PepperPluginDelegateImpl::GetLocalTimeZoneOffset(base::Time t) {
  double result = 0.0;
  render_view_->Send(new PepperMsg_GetLocalTimeZoneOffset(
      t, &result));
  return result;
}

std::string PepperPluginDelegateImpl::GetDeviceID() {
  std::string result;
  render_view_->Send(new PepperMsg_GetDeviceID(&result));
  return result;
}

PP_FlashLSORestrictions PepperPluginDelegateImpl::GetLocalDataRestrictions(
    const GURL& document_url,
    const GURL& plugin_url) {
  PP_FlashLSORestrictions restrictions = PP_FLASHLSORESTRICTIONS_NONE;
  render_view_->Send(
      new PepperMsg_GetLocalDataRestrictions(document_url, plugin_url,
                                             &restrictions));
  return restrictions;
}

base::SharedMemory* PepperPluginDelegateImpl::CreateAnonymousSharedMemory(
    uint32_t size) {
  if (size == 0)
    return NULL;
  base::SharedMemoryHandle handle;
  if (!render_view_->Send(
          new ChildProcessHostMsg_SyncAllocateSharedMemory(size, &handle))) {
    DLOG(WARNING) << "Browser allocation request message failed";
    return NULL;
  }
  if (!base::SharedMemory::IsHandleValid(handle)) {
    DLOG(WARNING) << "Browser failed to allocate shared memory";
    return NULL;
  }
  return new base::SharedMemory(handle, false);
}

ppapi::Preferences PepperPluginDelegateImpl::GetPreferences() {
  return ppapi::Preferences(render_view_->webkit_preferences());
}

bool PepperPluginDelegateImpl::LockMouse(
    webkit::ppapi::PluginInstance* instance) {
  return GetMouseLockDispatcher(instance)->LockMouse(
      GetOrCreateLockTargetAdapter(instance));
}

void PepperPluginDelegateImpl::UnlockMouse(
    webkit::ppapi::PluginInstance* instance) {
  GetMouseLockDispatcher(instance)->UnlockMouse(
      GetOrCreateLockTargetAdapter(instance));
}

bool PepperPluginDelegateImpl::IsMouseLocked(
    webkit::ppapi::PluginInstance* instance) {
  return GetMouseLockDispatcher(instance)->IsMouseLockedTo(
      GetOrCreateLockTargetAdapter(instance));
}

void PepperPluginDelegateImpl::DidChangeCursor(
    webkit::ppapi::PluginInstance* instance,
    const WebKit::WebCursorInfo& cursor) {
  // Update the cursor appearance immediately if the requesting plugin is the
  // one which receives the last mouse event. Otherwise, the new cursor won't be
  // picked up until the plugin gets the next input event. That is bad if, e.g.,
  // the plugin would like to set an invisible cursor when there isn't any user
  // input for a while.
  if (instance == last_mouse_event_target_)
    render_view_->didChangeCursor(cursor);
}

void PepperPluginDelegateImpl::DidReceiveMouseEvent(
    webkit::ppapi::PluginInstance* instance) {
  last_mouse_event_target_ = instance;
}

bool PepperPluginDelegateImpl::IsInFullscreenMode() {
  return render_view_->is_fullscreen();
}

void PepperPluginDelegateImpl::SampleGamepads(WebKit::WebGamepads* data) {
  if (!gamepad_shared_memory_reader_.get())
    gamepad_shared_memory_reader_.reset(new GamepadSharedMemoryReader);
  gamepad_shared_memory_reader_->SampleGamepads(*data);
}

bool PepperPluginDelegateImpl::IsPageVisible() const {
  return !render_view_->is_hidden();
}

int PepperPluginDelegateImpl::EnumerateDevices(
    PP_DeviceType_Dev type,
    const EnumerateDevicesCallback& callback) {
  int request_id =
      device_enumeration_event_handler_->RegisterEnumerateDevicesCallback(
          callback);

#if defined(ENABLE_WEBRTC)
  render_view_->media_stream_dispatcher()->EnumerateDevices(
      request_id, device_enumeration_event_handler_.get()->AsWeakPtr(),
      PepperDeviceEnumerationEventHandler::FromPepperDeviceType(type),
      GURL());
#else
  MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(
          &PepperDeviceEnumerationEventHandler::OnDevicesEnumerationFailed,
          device_enumeration_event_handler_->AsWeakPtr(), request_id));
#endif

  return request_id;
}

bool PepperPluginDelegateImpl::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PepperPluginDelegateImpl, message)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBTCPSocket_ConnectACK,
                        OnTCPSocketConnectACK)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBTCPSocket_SSLHandshakeACK,
                        OnTCPSocketSSLHandshakeACK)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBTCPSocket_ReadACK, OnTCPSocketReadACK)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBTCPSocket_WriteACK, OnTCPSocketWriteACK)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBUDPSocket_BindACK, OnUDPSocketBindACK)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBUDPSocket_RecvFromACK,
                        OnUDPSocketRecvFromACK)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBUDPSocket_SendToACK, OnUDPSocketSendToACK)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBTCPServerSocket_ListenACK,
                        OnTCPServerSocketListenACK)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBTCPServerSocket_AcceptACK,
                        OnTCPServerSocketAcceptACK)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBHostResolver_ResolveACK,
                        OnHostResolverResolveACK)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PepperPluginDelegateImpl::OnDestruct() {
  // Nothing to do here. Default implementation in RenderViewObserver does
  // 'delete this' but it's not suitable for PepperPluginDelegateImpl because
  // it's non-pointer member in RenderViewImpl.
}

void PepperPluginDelegateImpl::OnTCPSocketConnectACK(
    uint32 plugin_dispatcher_id,
    uint32 socket_id,
    bool succeeded,
    const PP_NetAddress_Private& local_addr,
    const PP_NetAddress_Private& remote_addr) {
  webkit::ppapi::PPB_TCPSocket_Private_Impl* socket =
      tcp_sockets_.Lookup(socket_id);
  if (socket)
    socket->OnConnectCompleted(succeeded, local_addr, remote_addr);
  if (!succeeded)
    tcp_sockets_.Remove(socket_id);
}

void PepperPluginDelegateImpl::OnTCPSocketSSLHandshakeACK(
    uint32 plugin_dispatcher_id,
    uint32 socket_id,
    bool succeeded,
    const ppapi::PPB_X509Certificate_Fields& certificate_fields) {
  webkit::ppapi::PPB_TCPSocket_Private_Impl* socket =
      tcp_sockets_.Lookup(socket_id);
  if (socket)
    socket->OnSSLHandshakeCompleted(succeeded, certificate_fields);
}

void PepperPluginDelegateImpl::OnTCPSocketReadACK(uint32 plugin_dispatcher_id,
                                                  uint32 socket_id,
                                                  bool succeeded,
                                                  const std::string& data) {
  webkit::ppapi::PPB_TCPSocket_Private_Impl* socket =
      tcp_sockets_.Lookup(socket_id);
  if (socket)
    socket->OnReadCompleted(succeeded, data);
}

void PepperPluginDelegateImpl::OnTCPSocketWriteACK(uint32 plugin_dispatcher_id,
                                                   uint32 socket_id,
                                                   bool succeeded,
                                                   int32_t bytes_written) {
  webkit::ppapi::PPB_TCPSocket_Private_Impl* socket =
      tcp_sockets_.Lookup(socket_id);
  if (socket)
    socket->OnWriteCompleted(succeeded, bytes_written);
}

void PepperPluginDelegateImpl::OnUDPSocketBindACK(
    uint32 plugin_dispatcher_id,
    uint32 socket_id,
    bool succeeded,
    const PP_NetAddress_Private& addr) {
  webkit::ppapi::PPB_UDPSocket_Private_Impl* socket =
      udp_sockets_.Lookup(socket_id);
  if (socket)
    socket->OnBindCompleted(succeeded, addr);
  if (!succeeded)
    udp_sockets_.Remove(socket_id);
}

void PepperPluginDelegateImpl::OnUDPSocketRecvFromACK(
    uint32 plugin_dispatcher_id,
    uint32 socket_id,
    bool succeeded,
    const std::string& data,
    const PP_NetAddress_Private& remote_addr) {
  webkit::ppapi::PPB_UDPSocket_Private_Impl* socket =
      udp_sockets_.Lookup(socket_id);
  if (socket)
    socket->OnRecvFromCompleted(succeeded, data, remote_addr);
}

void PepperPluginDelegateImpl::OnUDPSocketSendToACK(uint32 plugin_dispatcher_id,
                                                    uint32 socket_id,
                                                    bool succeeded,
                                                    int32_t bytes_written) {
  webkit::ppapi::PPB_UDPSocket_Private_Impl* socket =
      udp_sockets_.Lookup(socket_id);
  if (socket)
    socket->OnSendToCompleted(succeeded, bytes_written);
}

void PepperPluginDelegateImpl::OnTCPServerSocketListenACK(
    uint32 plugin_dispatcher_id,
    PP_Resource socket_resource,
    uint32 socket_id,
    int32_t status) {
  ppapi::thunk::EnterResource<ppapi::thunk::PPB_TCPServerSocket_Private_API>
      enter(socket_resource, true);
  if (enter.succeeded()) {
    ppapi::PPB_TCPServerSocket_Shared* socket =
        static_cast<ppapi::PPB_TCPServerSocket_Shared*>(enter.object());
    if (status == PP_OK)
      tcp_server_sockets_.AddWithID(socket, socket_id);
    socket->OnListenCompleted(socket_id, status);
  } else if (socket_id != 0 && status == PP_OK) {
    // StopListening was called before completion of Listen.
    render_view_->Send(new PpapiHostMsg_PPBTCPServerSocket_Destroy(socket_id));
  }
}

void PepperPluginDelegateImpl::OnTCPServerSocketAcceptACK(
    uint32 plugin_dispatcher_id,
    uint32 server_socket_id,
    uint32 accepted_socket_id,
    const PP_NetAddress_Private& local_addr,
    const PP_NetAddress_Private& remote_addr) {
  ppapi::PPB_TCPServerSocket_Shared* socket =
      tcp_server_sockets_.Lookup(server_socket_id);
  if (socket) {
    bool succeeded = (accepted_socket_id != 0);
    socket->OnAcceptCompleted(succeeded,
                              accepted_socket_id,
                              local_addr,
                              remote_addr);
  } else if (accepted_socket_id != 0) {
    render_view_->Send(
        new PpapiHostMsg_PPBTCPSocket_Disconnect(accepted_socket_id));
  }
}

void PepperPluginDelegateImpl::OnHostResolverResolveACK(
    uint32 plugin_dispatcher_id,
    uint32 host_resolver_id,
    bool succeeded,
    const std::string& canonical_name,
    const ppapi::NetAddressList& net_address_list) {
  ppapi::PPB_HostResolver_Shared* host_resolver =
      host_resolvers_.Lookup(host_resolver_id);
  if (host_resolver) {
    host_resolver->OnResolveCompleted(succeeded,
                                      canonical_name,
                                      net_address_list);
  }
}

int PepperPluginDelegateImpl::GetRoutingID() const {
  return render_view_->routing_id();
}

int PepperPluginDelegateImpl::OpenDevice(PP_DeviceType_Dev type,
                                         const std::string& device_id,
                                         const OpenDeviceCallback& callback) {
  int request_id =
      device_enumeration_event_handler_->RegisterOpenDeviceCallback(callback);

#if defined(ENABLE_WEBRTC)
  render_view_->media_stream_dispatcher()->OpenDevice(
      request_id,
      device_enumeration_event_handler_.get()->AsWeakPtr(),
      device_id,
      PepperDeviceEnumerationEventHandler::FromPepperDeviceType(type),
      GURL());
#else
  MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&PepperDeviceEnumerationEventHandler::OnDeviceOpenFailed,
                 device_enumeration_event_handler_->AsWeakPtr(), request_id));
#endif

  return request_id;
}

void PepperPluginDelegateImpl::CloseDevice(const std::string& label) {
#if defined(ENABLE_WEBRTC)
  render_view_->media_stream_dispatcher()->CloseDevice(label);
#endif
}

int PepperPluginDelegateImpl::GetSessionID(PP_DeviceType_Dev type,
                                           const std::string& label) {
#if defined(ENABLE_WEBRTC)
  switch (type) {
    case PP_DEVICETYPE_DEV_AUDIOCAPTURE:
      return render_view_->media_stream_dispatcher()->audio_session_id(label,
                                                                       0);
    case PP_DEVICETYPE_DEV_VIDEOCAPTURE:
      return render_view_->media_stream_dispatcher()->video_session_id(label,
                                                                       0);
    default:
      NOTREACHED();
      return 0;
  }
#else
  return 0;
#endif
}

WebGraphicsContext3DCommandBufferImpl*
PepperPluginDelegateImpl::GetParentContextForPlatformContext3D() {
  WebGraphicsContext3DCommandBufferImpl* context =
      static_cast<WebGraphicsContext3DCommandBufferImpl*>(
          render_view_->webview()->sharedGraphicsContext3D());
  if (!context)
    return NULL;
  if (!context->makeContextCurrent() || context->isContextLost())
    return NULL;

  return context;
}

MouseLockDispatcher::LockTarget*
    PepperPluginDelegateImpl::GetOrCreateLockTargetAdapter(
    webkit::ppapi::PluginInstance* instance) {
  MouseLockDispatcher::LockTarget* target = mouse_lock_instances_[instance];
  if (target)
    return target;

  return mouse_lock_instances_[instance] =
      new PluginInstanceLockTarget(instance);
}

void PepperPluginDelegateImpl::UnSetAndDeleteLockTargetAdapter(
    webkit::ppapi::PluginInstance* instance) {
  LockTargetMap::iterator it = mouse_lock_instances_.find(instance);
  if (it != mouse_lock_instances_.end()) {
    MouseLockDispatcher::LockTarget* target = it->second;
    GetMouseLockDispatcher(instance)->OnLockTargetDestroyed(target);
    delete target;
    mouse_lock_instances_.erase(it);
  }
}

MouseLockDispatcher* PepperPluginDelegateImpl::GetMouseLockDispatcher(
    webkit::ppapi::PluginInstance* instance) {
  if (instance->flash_fullscreen())
    return instance->fullscreen_container()->GetMouseLockDispatcher();
  else
    return render_view_->mouse_lock_dispatcher();
}

webkit_glue::ClipboardClient*
    PepperPluginDelegateImpl::CreateClipboardClient() const {
  return new RendererClipboardClient;
}

}  // namespace content
