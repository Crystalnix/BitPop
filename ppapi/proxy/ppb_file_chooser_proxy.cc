// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/ppb_file_chooser_proxy.h"

#include <queue>

#include "ppapi/c/dev/ppb_file_chooser_dev.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/c/private/ppb_proxy_private.h"
#include "ppapi/proxy/enter_proxy.h"
#include "ppapi/proxy/host_dispatcher.h"
#include "ppapi/proxy/plugin_dispatcher.h"
#include "ppapi/proxy/plugin_resource.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/ppb_file_ref_proxy.h"
#include "ppapi/proxy/serialized_var.h"
#include "ppapi/thunk/thunk.h"

using ::ppapi::thunk::PPB_FileChooser_API;

namespace pp {
namespace proxy {

class FileChooser : public PluginResource,
                    public PPB_FileChooser_API {
 public:
  FileChooser(const HostResource& resource);
  virtual ~FileChooser();

  // ResourceObjectBase overrides.
  virtual PPB_FileChooser_API* AsPPB_FileChooser_API() OVERRIDE;

  // PPB_FileChooser_API implementation.
  virtual int32_t Show(PP_CompletionCallback callback) OVERRIDE;
  virtual PP_Resource GetNextChosenFile() OVERRIDE;

  // Handles the choose complete notification from the host.
  void ChooseComplete(
    int32_t result_code,
    const std::vector<PPBFileRef_CreateInfo>& chosen_files);

 private:
  PP_CompletionCallback current_show_callback_;

  // All files returned by the current show callback that haven't yet been
  // given to the plugin. The plugin will repeatedly call us to get the next
  // file, and we'll vend those out of this queue, removing them when ownership
  // has transferred to the plugin.
  std::queue<PP_Resource> file_queue_;

  DISALLOW_COPY_AND_ASSIGN(FileChooser);
};

FileChooser::FileChooser(const HostResource& resource)
    : PluginResource(resource),
      current_show_callback_(PP_MakeCompletionCallback(NULL, NULL)) {
}

FileChooser::~FileChooser() {
  // Always need to fire completion callbacks to prevent a leak in the plugin.
  if (current_show_callback_.func) {
    // TODO(brettw) the callbacks at this level should be refactored with a
    // more automatic tracking system like we have in the renderer.
    MessageLoop::current()->PostTask(FROM_HERE, NewRunnableFunction(
        current_show_callback_.func, current_show_callback_.user_data,
        static_cast<int32_t>(PP_ERROR_ABORTED)));
  }

  // Any existing files we haven't transferred ownership to the plugin need
  // to be freed.
  PluginResourceTracker* tracker = PluginResourceTracker::GetInstance();
  while (!file_queue_.empty()) {
    tracker->ReleaseResource(file_queue_.front());
    file_queue_.pop();
  }
}

PPB_FileChooser_API* FileChooser::AsPPB_FileChooser_API() {
  return this;
}

int32_t FileChooser::Show(PP_CompletionCallback callback) {
  if (current_show_callback_.func)
    return PP_ERROR_INPROGRESS;  // Can't show more than once.

  current_show_callback_ = callback;
  GetDispatcher()->Send(new PpapiHostMsg_PPBFileChooser_Show(
      INTERFACE_ID_PPB_FILE_CHOOSER, host_resource()));
  return PP_OK_COMPLETIONPENDING;
}

PP_Resource FileChooser::GetNextChosenFile() {
  if (file_queue_.empty())
    return 0;

  // Return the next resource in the queue. These resource have already been
  // addrefed (they're currently owned by the FileChooser) and returning them
  // transfers ownership of that reference to the plugin.
  PP_Resource next = file_queue_.front();
  file_queue_.pop();
  return next;
}

void FileChooser::ChooseComplete(
    int32_t result_code,
    const std::vector<PPBFileRef_CreateInfo>& chosen_files) {
  // Convert each of the passed in file infos to resources. These will be owned
  // by the FileChooser object until they're passed to the plugin.
  DCHECK(file_queue_.empty());
  for (size_t i = 0; i < chosen_files.size(); i++)
    file_queue_.push(PPB_FileRef_Proxy::DeserializeFileRef(chosen_files[i]));

  // Notify the plugin of the new data.
  PP_RunAndClearCompletionCallback(&current_show_callback_, result_code);
  // DANGER: May delete |this|!
}

namespace {

InterfaceProxy* CreateFileChooserProxy(Dispatcher* dispatcher,
                                       const void* target_interface) {
  return new PPB_FileChooser_Proxy(dispatcher, target_interface);
}

}  // namespace

PPB_FileChooser_Proxy::PPB_FileChooser_Proxy(Dispatcher* dispatcher,
                                             const void* target_interface)
    : InterfaceProxy(dispatcher, target_interface),
      callback_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
}

PPB_FileChooser_Proxy::~PPB_FileChooser_Proxy() {
}

const InterfaceProxy::Info* PPB_FileChooser_Proxy::GetInfo() {
  static const Info info = {
    ::ppapi::thunk::GetPPB_FileChooser_Thunk(),
    PPB_FILECHOOSER_DEV_INTERFACE,
    INTERFACE_ID_PPB_FILE_CHOOSER,
    false,
    &CreateFileChooserProxy,
  };
  return &info;
}

// static
PP_Resource PPB_FileChooser_Proxy::CreateProxyResource(
    PP_Instance instance,
    const PP_FileChooserOptions_Dev* options) {
  Dispatcher* dispatcher = PluginDispatcher::GetForInstance(instance);
  if (!dispatcher)
    return 0;

  HostResource result;
  dispatcher->Send(new PpapiHostMsg_PPBFileChooser_Create(
      INTERFACE_ID_PPB_FILE_CHOOSER, instance,
      options->mode,
      options->accept_mime_types ? options->accept_mime_types : std::string(),
      &result));

  if (result.is_null())
    return 0;
  linked_ptr<FileChooser> object(new FileChooser(result));
  return PluginResourceTracker::GetInstance()->AddResource(object);
}

bool PPB_FileChooser_Proxy::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PPB_FileChooser_Proxy, msg)
    // Plugin -> host messages.
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBFileChooser_Create, OnMsgCreate)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBFileChooser_Show, OnMsgShow)

    // Host -> plugin messages.
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBFileChooser_ChooseComplete,
                        OnMsgChooseComplete)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PPB_FileChooser_Proxy::OnMsgCreate(PP_Instance instance,
                                        int mode,
                                        const std::string& accept_mime_types,
                                        HostResource* result) {
  PP_FileChooserOptions_Dev options;
  options.mode = static_cast<PP_FileChooserMode_Dev>(mode);
  options.accept_mime_types = accept_mime_types.c_str();
  result->SetHostResource(
      instance, ppb_file_chooser_target()->Create(instance, &options));
}

void PPB_FileChooser_Proxy::OnMsgShow(const HostResource& chooser) {
  CompletionCallback callback = callback_factory_.NewCallback(
      &PPB_FileChooser_Proxy::OnShowCallback, chooser);

  int32_t result = ppb_file_chooser_target()->Show(
      chooser.host_resource(), callback.pp_completion_callback());
  if (result != PP_OK_COMPLETIONPENDING)
    callback.Run(result);
}

void PPB_FileChooser_Proxy::OnMsgChooseComplete(
    const HostResource& chooser,
    int32_t result_code,
    const std::vector<PPBFileRef_CreateInfo>& chosen_files) {
  EnterPluginFromHostResource<PPB_FileChooser_API> enter(chooser);
  if (enter.succeeded()) {
    static_cast<FileChooser*>(enter.object())->ChooseComplete(
        result_code, chosen_files);
  }
}

void PPB_FileChooser_Proxy::OnShowCallback(int32_t result,
                                           const HostResource& chooser) {
  std::vector<PPBFileRef_CreateInfo> files;
  if (result == PP_OK) {
    // Jump through some hoops to get the FileRef proxy. Since we know we're
    // in the host at this point, we can ask the host dispatcher for it.
    DCHECK(!dispatcher()->IsPlugin());
    HostDispatcher* host_disp = static_cast<HostDispatcher*>(dispatcher());
    PPB_FileRef_Proxy* file_ref_proxy = static_cast<PPB_FileRef_Proxy*>(
        host_disp->GetOrCreatePPBInterfaceProxy(INTERFACE_ID_PPB_FILE_REF));

    // Convert the returned files to the serialized info.
    while (PP_Resource cur_file_resource =
               ppb_file_chooser_target()->GetNextChosenFile(
                   chooser.host_resource())) {
      PPBFileRef_CreateInfo cur_create_info;
      file_ref_proxy->SerializeFileRef(cur_file_resource, &cur_create_info);
      files.push_back(cur_create_info);
    }
  }

  dispatcher()->Send(new PpapiMsg_PPBFileChooser_ChooseComplete(
      INTERFACE_ID_PPB_FILE_CHOOSER, chooser, result, files));
}

}  // namespace proxy
}  // namespace pp
