// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/pepper/pepper_platform_audio_input_impl.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop_proxy.h"
#include "build/build_config.h"
#include "content/common/child_process.h"
#include "content/common/media/audio_messages.h"
#include "content/renderer/pepper/pepper_plugin_delegate_impl.h"
#include "content/renderer/render_thread_impl.h"
#include "media/audio/audio_manager_base.h"

namespace content {

// static
PepperPlatformAudioInputImpl* PepperPlatformAudioInputImpl::Create(
    const base::WeakPtr<PepperPluginDelegateImpl>& plugin_delegate,
    const std::string& device_id,
    int sample_rate,
    int frames_per_buffer,
    webkit::ppapi::PluginDelegate::PlatformAudioInputClient* client) {
  scoped_refptr<PepperPlatformAudioInputImpl> audio_input(
      new PepperPlatformAudioInputImpl());
  if (audio_input->Initialize(plugin_delegate, device_id, sample_rate,
                              frames_per_buffer, client)) {
    // Balanced by Release invoked in
    // PepperPlatformAudioInputImpl::ShutDownOnIOThread().
    return audio_input.release();
  }
  return NULL;
}

void PepperPlatformAudioInputImpl::StartCapture() {
  DCHECK(main_message_loop_proxy_->BelongsToCurrentThread());

  ChildProcess::current()->io_message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&PepperPlatformAudioInputImpl::StartCaptureOnIOThread, this));
}

void PepperPlatformAudioInputImpl::StopCapture() {
  DCHECK(main_message_loop_proxy_->BelongsToCurrentThread());

  ChildProcess::current()->io_message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&PepperPlatformAudioInputImpl::StopCaptureOnIOThread, this));
}

void PepperPlatformAudioInputImpl::ShutDown() {
  DCHECK(main_message_loop_proxy_->BelongsToCurrentThread());

  // Called on the main thread to stop all audio callbacks. We must only change
  // the client on the main thread, and the delegates from the I/O thread.
  client_ = NULL;
  ChildProcess::current()->io_message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&PepperPlatformAudioInputImpl::ShutDownOnIOThread, this));
}

void PepperPlatformAudioInputImpl::OnStreamCreated(
    base::SharedMemoryHandle handle,
    base::SyncSocket::Handle socket_handle,
    int length) {
#if defined(OS_WIN)
  DCHECK(handle);
  DCHECK(socket_handle);
#else
  DCHECK_NE(-1, handle.fd);
  DCHECK_NE(-1, socket_handle);
#endif
  DCHECK(length);

  if (base::MessageLoopProxy::current() != main_message_loop_proxy_) {
    // No need to check |shutdown_called_| here. If shutdown has occurred,
    // |client_| will be NULL and the handles will be cleaned up on the main
    // thread.
    main_message_loop_proxy_->PostTask(
        FROM_HERE,
        base::Bind(&PepperPlatformAudioInputImpl::OnStreamCreated, this,
                   handle, socket_handle, length));
  } else {
    // Must dereference the client only on the main thread. Shutdown may have
    // occurred while the request was in-flight, so we need to NULL check.
    if (client_) {
      client_->StreamCreated(handle, length, socket_handle);
    } else {
      // Clean up the handles.
      base::SyncSocket temp_socket(socket_handle);
      base::SharedMemory temp_shared_memory(handle, false);
    }
  }
}

void PepperPlatformAudioInputImpl::OnVolume(double volume) {}

void PepperPlatformAudioInputImpl::OnStateChanged(
    media::AudioInputIPCDelegate::State state) {
}

void PepperPlatformAudioInputImpl::OnDeviceReady(const std::string& device_id) {
  DCHECK(ChildProcess::current()->io_message_loop_proxy()->
      BelongsToCurrentThread());

  if (shutdown_called_)
    return;

  if (device_id.empty()) {
    main_message_loop_proxy_->PostTask(
        FROM_HERE,
        base::Bind(&PepperPlatformAudioInputImpl::NotifyStreamCreationFailed,
                   this));
  } else {
    // We will be notified by OnStreamCreated().
    ipc_->CreateStream(stream_id_, params_, device_id, false);
  }
}

void PepperPlatformAudioInputImpl::OnIPCClosed() {
  ipc_ = NULL;
}

PepperPlatformAudioInputImpl::~PepperPlatformAudioInputImpl() {
  // Make sure we have been shut down. Warning: this may happen on the I/O
  // thread!
  // Although these members should be accessed on a specific thread (either the
  // main thread or the I/O thread), it should be fine to examine their value
  // here.
  DCHECK_EQ(0, stream_id_);
  DCHECK(!client_);
  DCHECK(label_.empty());
  DCHECK(shutdown_called_);
}

PepperPlatformAudioInputImpl::PepperPlatformAudioInputImpl()
    : client_(NULL),
      stream_id_(0),
      main_message_loop_proxy_(base::MessageLoopProxy::current()),
      shutdown_called_(false) {
  ipc_ = RenderThreadImpl::current()->audio_input_message_filter();
}

bool PepperPlatformAudioInputImpl::Initialize(
    const base::WeakPtr<PepperPluginDelegateImpl>& plugin_delegate,
    const std::string& device_id,
    int sample_rate,
    int frames_per_buffer,
    webkit::ppapi::PluginDelegate::PlatformAudioInputClient* client) {
  DCHECK(main_message_loop_proxy_->BelongsToCurrentThread());

  if (!plugin_delegate || !client)
    return false;

  plugin_delegate_ = plugin_delegate;
  client_ = client;

  params_.Reset(media::AudioParameters::AUDIO_PCM_LINEAR, CHANNEL_LAYOUT_MONO,
                sample_rate, 16, frames_per_buffer);

  if (device_id.empty()) {
    // Use the default device.
    ChildProcess::current()->io_message_loop()->PostTask(
        FROM_HERE,
        base::Bind(&PepperPlatformAudioInputImpl::InitializeOnIOThread,
                   this, 0));
  } else {
    // We need to open the device and obtain the label and session ID before
    // initializing.
    plugin_delegate_->OpenDevice(
        PP_DEVICETYPE_DEV_AUDIOCAPTURE, device_id,
        base::Bind(&PepperPlatformAudioInputImpl::OnDeviceOpened, this));
  }
  return true;
}

void PepperPlatformAudioInputImpl::InitializeOnIOThread(int session_id) {
  DCHECK(ChildProcess::current()->io_message_loop_proxy()->
      BelongsToCurrentThread());

  if (shutdown_called_)
    return;

  // Make sure we don't call init more than once.
  DCHECK_EQ(0, stream_id_);
  stream_id_ = ipc_->AddDelegate(this);
  DCHECK_NE(0, stream_id_);

  if (!session_id) {
    // We will be notified by OnStreamCreated().
    ipc_->CreateStream(stream_id_, params_,
        media::AudioManagerBase::kDefaultDeviceId, false);
  } else {
    // We will be notified by OnDeviceReady().
    ipc_->StartDevice(stream_id_, session_id);
  }
}

void PepperPlatformAudioInputImpl::StartCaptureOnIOThread() {
  DCHECK(ChildProcess::current()->io_message_loop_proxy()->
      BelongsToCurrentThread());

  if (stream_id_)
    ipc_->RecordStream(stream_id_);
}

void PepperPlatformAudioInputImpl::StopCaptureOnIOThread() {
  DCHECK(ChildProcess::current()->io_message_loop_proxy()->
      BelongsToCurrentThread());

  // TODO(yzshen): We cannot re-start capturing if the stream is closed.
  if (stream_id_)
    ipc_->CloseStream(stream_id_);
}

void PepperPlatformAudioInputImpl::ShutDownOnIOThread() {
  DCHECK(ChildProcess::current()->io_message_loop_proxy()->
      BelongsToCurrentThread());

  // Make sure we don't call shutdown more than once.
  if (shutdown_called_)
    return;
  shutdown_called_ = true;

  if (stream_id_) {
    ipc_->CloseStream(stream_id_);
    ipc_->RemoveDelegate(stream_id_);
    stream_id_ = 0;
  }

  main_message_loop_proxy_->PostTask(
      FROM_HERE,
      base::Bind(&PepperPlatformAudioInputImpl::CloseDevice, this));

  Release();  // Release for the delegate, balances out the reference taken in
              // PepperPluginDelegateImpl::CreateAudioInput.
}

void PepperPlatformAudioInputImpl::OnDeviceOpened(int request_id,
                                                  bool succeeded,
                                                  const std::string& label) {
  DCHECK(main_message_loop_proxy_->BelongsToCurrentThread());

  if (succeeded && plugin_delegate_) {
    DCHECK(!label.empty());
    label_ = label;

    if (client_) {
      int session_id = plugin_delegate_->GetSessionID(
          PP_DEVICETYPE_DEV_AUDIOCAPTURE, label);
      ChildProcess::current()->io_message_loop()->PostTask(
          FROM_HERE,
          base::Bind(&PepperPlatformAudioInputImpl::InitializeOnIOThread,
                     this, session_id));
    } else {
      // Shutdown has occurred.
      CloseDevice();
    }
  } else {
    NotifyStreamCreationFailed();
  }
}

void PepperPlatformAudioInputImpl::CloseDevice() {
  DCHECK(main_message_loop_proxy_->BelongsToCurrentThread());

  if (plugin_delegate_ && !label_.empty()) {
    plugin_delegate_->CloseDevice(label_);
    label_.clear();
  }
}

void PepperPlatformAudioInputImpl::NotifyStreamCreationFailed() {
  DCHECK(main_message_loop_proxy_->BelongsToCurrentThread());

  if (client_)
    client_->StreamCreationFailed();
}

}  // namespace content
