// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/resource_creation_proxy.h"

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_size.h"
#include "ppapi/c/trusted/ppb_image_data_trusted.h"
#include "ppapi/proxy/plugin_dispatcher.h"
#include "ppapi/proxy/plugin_resource_tracker.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/ppb_audio_input_proxy.h"
#include "ppapi/proxy/ppb_audio_proxy.h"
#include "ppapi/proxy/ppb_buffer_proxy.h"
#include "ppapi/proxy/ppb_broker_proxy.h"
#include "ppapi/proxy/ppb_file_chooser_proxy.h"
#include "ppapi/proxy/ppb_file_io_proxy.h"
#include "ppapi/proxy/ppb_file_ref_proxy.h"
#include "ppapi/proxy/ppb_file_system_proxy.h"
#include "ppapi/proxy/ppb_flash_menu_proxy.h"
#include "ppapi/proxy/ppb_flash_net_connector_proxy.h"
#include "ppapi/proxy/ppb_font_proxy.h"
#include "ppapi/proxy/ppb_graphics_2d_proxy.h"
#include "ppapi/proxy/ppb_graphics_3d_proxy.h"
#include "ppapi/proxy/ppb_image_data_proxy.h"
#include "ppapi/proxy/ppb_tcp_socket_private_proxy.h"
#include "ppapi/proxy/ppb_udp_socket_private_proxy.h"
#include "ppapi/proxy/ppb_url_loader_proxy.h"
#include "ppapi/proxy/ppb_video_capture_proxy.h"
#include "ppapi/proxy/ppb_video_decoder_proxy.h"
#include "ppapi/shared_impl/api_id.h"
#include "ppapi/shared_impl/function_group_base.h"
#include "ppapi/shared_impl/host_resource.h"
#include "ppapi/shared_impl/ppb_audio_config_shared.h"
#include "ppapi/shared_impl/ppb_input_event_shared.h"
#include "ppapi/shared_impl/ppb_resource_array_shared.h"
#include "ppapi/shared_impl/ppb_url_request_info_shared.h"
#include "ppapi/shared_impl/private/ppb_font_shared.h"
#include "ppapi/shared_impl/var.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/ppb_image_data_api.h"

using ppapi::thunk::ResourceCreationAPI;

namespace ppapi {
namespace proxy {

ResourceCreationProxy::ResourceCreationProxy(Dispatcher* dispatcher)
    : InterfaceProxy(dispatcher) {
}

ResourceCreationProxy::~ResourceCreationProxy() {
}

// static
InterfaceProxy* ResourceCreationProxy::Create(Dispatcher* dispatcher) {
  return new ResourceCreationProxy(dispatcher);
}

ResourceCreationAPI* ResourceCreationProxy::AsResourceCreationAPI() {
  return this;
}

PP_Resource ResourceCreationProxy::CreateAudio(
    PP_Instance instance,
    PP_Resource config_id,
    PPB_Audio_Callback audio_callback,
    void* user_data) {
  return PPB_Audio_Proxy::CreateProxyResource(instance, config_id,
                                              audio_callback, user_data);
}

PP_Resource ResourceCreationProxy::CreateAudioConfig(
    PP_Instance instance,
    PP_AudioSampleRate sample_rate,
    uint32_t sample_frame_count) {
  return PPB_AudioConfig_Shared::CreateAsProxy(
      instance, sample_rate, sample_frame_count);
}

PP_Resource ResourceCreationProxy::CreateAudioTrusted(PP_Instance instance) {
  // Proxied plugins can't created trusted audio devices.
  return 0;
}

PP_Resource ResourceCreationProxy::CreateAudioInput(
    PP_Instance instance,
    PP_Resource config_id,
    PPB_AudioInput_Callback audio_input_callback,
    void* user_data) {
  return PPB_AudioInput_Proxy::CreateProxyResource(instance, config_id,
                                                   audio_input_callback,
                                                   user_data);
}

PP_Resource ResourceCreationProxy::CreateAudioInputTrusted(
    PP_Instance instance) {
  // Proxied plugins can't created trusted audio input devices.
  return 0;
}

PP_Resource ResourceCreationProxy::CreateBroker(PP_Instance instance) {
  return PPB_Broker_Proxy::CreateProxyResource(instance);
}

PP_Resource ResourceCreationProxy::CreateBuffer(PP_Instance instance,
                                                uint32_t size) {
  return PPB_Buffer_Proxy::CreateProxyResource(instance, size);
}

PP_Resource ResourceCreationProxy::CreateDirectoryReader(
    PP_Resource directory_ref) {
  NOTIMPLEMENTED();  // Not proxied yet.
  return 0;
}

PP_Resource ResourceCreationProxy::CreateFileChooser(
    PP_Instance instance,
    PP_FileChooserMode_Dev mode,
    const char* accept_mime_types) {
  return PPB_FileChooser_Proxy::CreateProxyResource(instance, mode,
                                                    accept_mime_types);
}

PP_Resource ResourceCreationProxy::CreateFileIO(PP_Instance instance) {
  return PPB_FileIO_Proxy::CreateProxyResource(instance);
}

PP_Resource ResourceCreationProxy::CreateFileRef(PP_Resource file_system,
                                                 const char* path) {
  return PPB_FileRef_Proxy::CreateProxyResource(file_system, path);
}

PP_Resource ResourceCreationProxy::CreateFileSystem(
    PP_Instance instance,
    PP_FileSystemType type) {
  return PPB_FileSystem_Proxy::CreateProxyResource(instance, type);
}

PP_Resource ResourceCreationProxy::CreateFlashMenu(
    PP_Instance instance,
    const PP_Flash_Menu* menu_data) {
  return PPB_Flash_Menu_Proxy::CreateProxyResource(instance, menu_data);
}

PP_Resource ResourceCreationProxy::CreateFlashNetConnector(
    PP_Instance instance) {
  return PPB_Flash_NetConnector_Proxy::CreateProxyResource(instance);
}

PP_Resource ResourceCreationProxy::CreateFontObject(
    PP_Instance instance,
    const PP_FontDescription_Dev* description) {
  PluginDispatcher* dispatcher =
      PluginDispatcher::GetForInstance(instance);
  if (!dispatcher)
    return 0;
  return PPB_Font_Shared::CreateAsProxy(instance, *description,
                                        dispatcher->preferences());
}

PP_Resource ResourceCreationProxy::CreateGraphics2D(PP_Instance instance,
                                                    const PP_Size& size,
                                                    PP_Bool is_always_opaque) {
  return PPB_Graphics2D_Proxy::CreateProxyResource(instance, size,
                                                   is_always_opaque);
}

PP_Resource ResourceCreationProxy::CreateImageData(PP_Instance instance,
                                                   PP_ImageDataFormat format,
                                                   const PP_Size& size,
                                                   PP_Bool init_to_zero) {
  return PPB_ImageData_Proxy::CreateProxyResource(instance, format, size,
                                                  init_to_zero);
}

PP_Resource ResourceCreationProxy::CreateKeyboardInputEvent(
    PP_Instance instance,
    PP_InputEvent_Type type,
    PP_TimeTicks time_stamp,
    uint32_t modifiers,
    uint32_t key_code,
    struct PP_Var character_text) {
  if (type != PP_INPUTEVENT_TYPE_RAWKEYDOWN &&
      type != PP_INPUTEVENT_TYPE_KEYDOWN &&
      type != PP_INPUTEVENT_TYPE_KEYUP &&
      type != PP_INPUTEVENT_TYPE_CHAR)
    return 0;
  InputEventData data;
  data.event_type = type;
  data.event_time_stamp = time_stamp;
  data.event_modifiers = modifiers;
  data.key_code = key_code;
  if (character_text.type == PP_VARTYPE_STRING) {
    StringVar* text_str = StringVar::FromPPVar(character_text);
    if (!text_str)
      return 0;
    data.character_text = text_str->value();
  }

  return (new PPB_InputEvent_Shared(PPB_InputEvent_Shared::InitAsProxy(),
                                    instance, data))->GetReference();
}

PP_Resource ResourceCreationProxy::CreateMouseInputEvent(
    PP_Instance instance,
    PP_InputEvent_Type type,
    PP_TimeTicks time_stamp,
    uint32_t modifiers,
    PP_InputEvent_MouseButton mouse_button,
    const PP_Point* mouse_position,
    int32_t click_count,
    const PP_Point* mouse_movement) {
  if (type != PP_INPUTEVENT_TYPE_MOUSEDOWN &&
      type != PP_INPUTEVENT_TYPE_MOUSEUP &&
      type != PP_INPUTEVENT_TYPE_MOUSEMOVE &&
      type != PP_INPUTEVENT_TYPE_MOUSEENTER &&
      type != PP_INPUTEVENT_TYPE_MOUSELEAVE)
    return 0;

  InputEventData data;
  data.event_type = type;
  data.event_time_stamp = time_stamp;
  data.event_modifiers = modifiers;
  data.mouse_button = mouse_button;
  data.mouse_position = *mouse_position;
  data.mouse_click_count = click_count;
  data.mouse_movement = *mouse_movement;

  return (new PPB_InputEvent_Shared(PPB_InputEvent_Shared::InitAsProxy(),
                                    instance, data))->GetReference();
}

PP_Resource ResourceCreationProxy::CreateGraphics3D(
    PP_Instance instance,
    PP_Resource share_context,
    const int32_t* attrib_list) {
  return PPB_Graphics3D_Proxy::CreateProxyResource(
      instance, share_context, attrib_list);
}

PP_Resource ResourceCreationProxy::CreateGraphics3DRaw(
    PP_Instance instance,
    PP_Resource share_context,
    const int32_t* attrib_list) {
  // Not proxied. The raw creation function is used only in the implementation
  // of the proxy on the host side.
  return 0;
}

PP_Resource ResourceCreationProxy::CreateResourceArray(
    PP_Instance instance,
    const PP_Resource elements[],
    uint32_t size) {
  PPB_ResourceArray_Shared* object = new PPB_ResourceArray_Shared(
      PPB_ResourceArray_Shared::InitAsProxy(), instance, elements, size);
  return object->GetReference();
}

PP_Resource ResourceCreationProxy::CreateScrollbar(PP_Instance instance,
                                                   PP_Bool vertical) {
  NOTIMPLEMENTED();  // Not proxied yet.
  return 0;
}

PP_Resource ResourceCreationProxy::CreateTCPSocketPrivate(
    PP_Instance instance) {
  return PPB_TCPSocket_Private_Proxy::CreateProxyResource(instance);
}

PP_Resource ResourceCreationProxy::CreateTransport(PP_Instance instance,
                                                   const char* name,
                                                   PP_TransportType type) {
  NOTIMPLEMENTED();  // Not proxied yet.
  return 0;
}

PP_Resource ResourceCreationProxy::CreateUDPSocketPrivate(
    PP_Instance instance) {
  return PPB_UDPSocket_Private_Proxy::CreateProxyResource(instance);
}

PP_Resource ResourceCreationProxy::CreateURLLoader(PP_Instance instance) {
  return PPB_URLLoader_Proxy::CreateProxyResource(instance);
}

PP_Resource ResourceCreationProxy::CreateURLRequestInfo(
    PP_Instance instance,
    const PPB_URLRequestInfo_Data& data) {
  return (new PPB_URLRequestInfo_Shared(
      HostResource::MakeInstanceOnly(instance), data))->GetReference();
}

PP_Resource ResourceCreationProxy::CreateVideoCapture(PP_Instance instance) {
  return PPB_VideoCapture_Proxy::CreateProxyResource(instance);
}

PP_Resource ResourceCreationProxy::CreateVideoDecoder(
    PP_Instance instance,
    PP_Resource context3d_id,
    PP_VideoDecoder_Profile profile) {
  return PPB_VideoDecoder_Proxy::CreateProxyResource(
      instance, context3d_id, profile);
}

PP_Resource ResourceCreationProxy::CreateVideoLayer(
    PP_Instance instance,
    PP_VideoLayerMode_Dev mode) {
  NOTIMPLEMENTED();
  return 0;
}

PP_Resource ResourceCreationProxy::CreateWebSocket(PP_Instance instance) {
  NOTIMPLEMENTED();
  return 0;
}

PP_Resource ResourceCreationProxy::CreateWheelInputEvent(
    PP_Instance instance,
    PP_TimeTicks time_stamp,
    uint32_t modifiers,
    const PP_FloatPoint* wheel_delta,
    const PP_FloatPoint* wheel_ticks,
    PP_Bool scroll_by_page) {
  InputEventData data;
  data.event_type = PP_INPUTEVENT_TYPE_WHEEL;
  data.event_time_stamp = time_stamp;
  data.event_modifiers = modifiers;
  data.wheel_delta = *wheel_delta;
  data.wheel_ticks = *wheel_ticks;
  data.wheel_scroll_by_page = PP_ToBool(scroll_by_page);

  return (new PPB_InputEvent_Shared(PPB_InputEvent_Shared::InitAsProxy(),
                                    instance, data))->GetReference();
}

bool ResourceCreationProxy::Send(IPC::Message* msg) {
  return dispatcher()->Send(msg);
}

bool ResourceCreationProxy::OnMessageReceived(const IPC::Message& msg) {
  return false;
}

}  // namespace proxy
}  // namespace ppapi
