// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/ppapi/ppb_audio_impl.h"

#include "base/logging.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/ppb_audio.h"
#include "ppapi/c/ppb_audio_config.h"
#include "ppapi/c/trusted/ppb_audio_trusted.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/ppb_audio_config_api.h"
#include "ppapi/thunk/thunk.h"
#include "webkit/plugins/ppapi/common.h"

namespace webkit {
namespace ppapi {

// PPB_AudioConfig -------------------------------------------------------------

PPB_AudioConfig_Impl::PPB_AudioConfig_Impl(PluginInstance* instance)
    : Resource(instance) {
}

PPB_AudioConfig_Impl::~PPB_AudioConfig_Impl() {
}

::ppapi::thunk::PPB_AudioConfig_API*
PPB_AudioConfig_Impl::AsPPB_AudioConfig_API() {
  return this;
}

// PPB_Audio_Impl --------------------------------------------------------------

PPB_Audio_Impl::PPB_Audio_Impl(PluginInstance* instance)
    : Resource(instance),
      config_id_(0),
      audio_(NULL),
      create_callback_pending_(false) {
  create_callback_ = PP_MakeCompletionCallback(NULL, NULL);
}

PPB_Audio_Impl::~PPB_Audio_Impl() {
  if (config_id_)
    ResourceTracker::Get()->UnrefResource(config_id_);

  // Calling ShutDown() makes sure StreamCreated cannot be called anymore and
  // releases the audio data associated with the pointer. Note however, that
  // until ShutDown returns, StreamCreated may still be called. This will be
  // OK since we'll just immediately clean up the data it stored later in this
  // destructor.
  if (audio_) {
    audio_->ShutDown();
    audio_ = NULL;
  }

  // If the completion callback hasn't fired yet, do so here
  // with an error condition.
  if (create_callback_pending_) {
    PP_RunCompletionCallback(&create_callback_, PP_ERROR_ABORTED);
    create_callback_pending_ = false;
  }
}

::ppapi::thunk::PPB_Audio_API* PPB_Audio_Impl::AsPPB_Audio_API() {
  return this;
}

::ppapi::thunk::PPB_AudioTrusted_API* PPB_Audio_Impl::AsPPB_AudioTrusted_API() {
  return this;
}

bool PPB_Audio_Impl::Init(PP_Resource config_id,
                          PPB_Audio_Callback callback, void* user_data) {
  // Validate the config and keep a reference to it.
  ::ppapi::thunk::EnterResourceNoLock< ::ppapi::thunk::PPB_AudioConfig_API>
      enter(config_id, true);
  if (enter.failed())
    return false;
  config_id_ = config_id;
  ResourceTracker::Get()->AddRefResource(config_id);

  if (!callback)
    return false;
  SetCallback(callback, user_data);

  // When the stream is created, we'll get called back on StreamCreated().
  CHECK(!audio_);
  audio_ = instance()->delegate()->CreateAudio(
      enter.object()->GetSampleRate(),
      enter.object()->GetSampleFrameCount(),
      this);
  return audio_ != NULL;
}

PP_Resource PPB_Audio_Impl::GetCurrentConfig() {
  // AddRef on behalf of caller.
  ResourceTracker::Get()->AddRefResource(config_id_);
  return config_id_;
}

PP_Bool PPB_Audio_Impl::StartPlayback() {
  if (!audio_)
    return PP_FALSE;
  if (playing())
    return PP_TRUE;
  SetStartPlaybackState();
  return BoolToPPBool(audio_->StartPlayback());
}

PP_Bool PPB_Audio_Impl::StopPlayback() {
  if (!audio_)
    return PP_FALSE;
  if (!playing())
    return PP_TRUE;
  if (!audio_->StopPlayback())
    return PP_FALSE;
  SetStopPlaybackState();
  return PP_TRUE;
}

int32_t PPB_Audio_Impl::OpenTrusted(PP_Resource config_id,
                                    PP_CompletionCallback create_callback) {

  // Validate the config and keep a reference to it.
  ::ppapi::thunk::EnterResourceNoLock< ::ppapi::thunk::PPB_AudioConfig_API>
      enter(config_id, true);
  if (enter.failed())
    return false;
  config_id_ = config_id;
  ResourceTracker::Get()->AddRefResource(config_id);

  // When the stream is created, we'll get called back on StreamCreated().
  DCHECK(!audio_);
  audio_ = instance()->delegate()->CreateAudio(
      enter.object()->GetSampleRate(),
      enter.object()->GetSampleFrameCount(),
      this);
  if (!audio_)
    return PP_ERROR_FAILED;

  // At this point, we are guaranteeing ownership of the completion
  // callback.  Audio promises to fire the completion callback
  // once and only once.
  create_callback_ = create_callback;
  create_callback_pending_ = true;
  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_Audio_Impl::GetSyncSocket(int* sync_socket) {
  if (socket_for_create_callback_.get()) {
#if defined(OS_POSIX)
    *sync_socket = socket_for_create_callback_->handle();
#elif defined(OS_WIN)
    *sync_socket = reinterpret_cast<int>(socket_for_create_callback_->handle());
#else
    #error "Platform not supported."
#endif
    return PP_OK;
  }
  return PP_ERROR_FAILED;
}

int32_t PPB_Audio_Impl::GetSharedMemory(int* shm_handle, uint32_t* shm_size) {
  if (shared_memory_for_create_callback_.get()) {
#if defined(OS_POSIX)
    *shm_handle = shared_memory_for_create_callback_->handle().fd;
#elif defined(OS_WIN)
    *shm_handle = reinterpret_cast<int>(
        shared_memory_for_create_callback_->handle());
#else
    #error "Platform not supported."
#endif
    *shm_size = shared_memory_size_for_create_callback_;
    return PP_OK;
  }
  return PP_ERROR_FAILED;
}

void PPB_Audio_Impl::StreamCreated(
    base::SharedMemoryHandle shared_memory_handle,
    size_t shared_memory_size,
    base::SyncSocket::Handle socket_handle) {
  if (create_callback_pending_) {
    // Trusted side of proxy can specify a callback to recieve handles. In
    // this case we don't need to map any data or start the thread since it
    // will be handled by the proxy.
    shared_memory_for_create_callback_.reset(
        new base::SharedMemory(shared_memory_handle, false));
    shared_memory_size_for_create_callback_ = shared_memory_size;
    socket_for_create_callback_.reset(new base::SyncSocket(socket_handle));

    PP_RunCompletionCallback(&create_callback_, 0);
    create_callback_pending_ = false;

    // It might be nice to close the handles here to free up some system
    // resources, but we can't since there's a race condition. The handles must
    // be valid until they're sent over IPC, which is done from the I/O thread
    // which will often get done after this code executes. We could do
    // something more elaborate like an ACK from the plugin or post a task to
    // the I/O thread and back, but this extra complexity doesn't seem worth it
    // just to clean up these handles faster.
  } else {
    SetStreamInfo(shared_memory_handle, shared_memory_size, socket_handle);
  }
}

}  // namespace ppapi
}  // namespace webkit
