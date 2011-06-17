// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/audio_input_renderer_host.h"

#include "base/metrics/histogram.h"
#include "base/process.h"
#include "base/shared_memory.h"
#include "content/browser/renderer_host/audio_common.h"
#include "content/browser/renderer_host/audio_input_sync_writer.h"
#include "content/common/audio_messages.h"
#include "ipc/ipc_logging.h"


AudioInputRendererHost::AudioEntry::AudioEntry()
    : render_view_id(0),
      stream_id(0),
      pending_close(false) {
}

AudioInputRendererHost::AudioEntry::~AudioEntry() {}

AudioInputRendererHost::AudioInputRendererHost() {}

AudioInputRendererHost::~AudioInputRendererHost() {
  DCHECK(audio_entries_.empty());
}

void AudioInputRendererHost::OnChannelClosing() {
  BrowserMessageFilter::OnChannelClosing();

  // Since the IPC channel is gone, close all requested audio streams.
  DeleteEntries();
}

void AudioInputRendererHost::OnDestruct() const {
  BrowserThread::DeleteOnIOThread::Destruct(this);
}

void AudioInputRendererHost::OnCreated(
    media::AudioInputController* controller) {
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      NewRunnableMethod(
          this,
          &AudioInputRendererHost::DoCompleteCreation,
          make_scoped_refptr(controller)));
}

void AudioInputRendererHost::OnRecording(
    media::AudioInputController* controller) {
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      NewRunnableMethod(
          this,
          &AudioInputRendererHost::DoSendRecordingMessage,
          make_scoped_refptr(controller)));
}

void AudioInputRendererHost::OnError(
    media::AudioInputController* controller,
    int error_code) {
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      NewRunnableMethod(this,
                        &AudioInputRendererHost::DoHandleError,
                        make_scoped_refptr(controller),
                        error_code));
}

void AudioInputRendererHost::OnData(media::AudioInputController* controller,
                                    const uint8* data,
                                    uint32 size) {
  NOTREACHED() << "Only low-latency mode is supported.";
}

void AudioInputRendererHost::DoCompleteCreation(
    media::AudioInputController* controller) {
  VLOG(1) << "AudioInputRendererHost::DoCompleteCreation()";
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  AudioEntry* entry = LookupByController(controller);
  if (!entry)
    return;

  if (!peer_handle()) {
    NOTREACHED() << "Renderer process handle is invalid.";
    DeleteEntryOnError(entry);
    return;
  }

  if (!entry->controller->LowLatencyMode()) {
    NOTREACHED() << "Only low-latency mode is supported.";
    DeleteEntryOnError(entry);
    return;
  }

  // Once the audio stream is created then complete the creation process by
  // mapping shared memory and sharing with the renderer process.
  base::SharedMemoryHandle foreign_memory_handle;
  if (!entry->shared_memory.ShareToProcess(peer_handle(),
                                           &foreign_memory_handle)) {
    // If we failed to map and share the shared memory then close the audio
    // stream and send an error message.
    DeleteEntryOnError(entry);
    return;
  }

  if (entry->controller->LowLatencyMode()) {
    AudioInputSyncWriter* writer =
        static_cast<AudioInputSyncWriter*>(entry->writer.get());

#if defined(OS_WIN)
    base::SyncSocket::Handle foreign_socket_handle;
#else
    base::FileDescriptor foreign_socket_handle;
#endif

    // If we failed to prepare the sync socket for the renderer then we fail
    // the construction of audio input stream.
    if (!writer->PrepareForeignSocketHandle(peer_handle(),
                                            &foreign_socket_handle)) {
      DeleteEntryOnError(entry);
      return;
    }

    Send(new AudioInputMsg_NotifyLowLatencyStreamCreated(
             entry->render_view_id, entry->stream_id, foreign_memory_handle,
             foreign_socket_handle, entry->shared_memory.created_size()));
    return;
  }
}

void AudioInputRendererHost::DoSendRecordingMessage(
    media::AudioInputController* controller) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // TODO(henrika): TBI?
  NOTIMPLEMENTED();
}

void AudioInputRendererHost::DoSendPausedMessage(
    media::AudioInputController* controller) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // TODO(henrika): TBI?
  NOTREACHED();
}

void AudioInputRendererHost::DoHandleError(
    media::AudioInputController* controller,
    int error_code) {
  DLOG(WARNING) << "AudioInputRendererHost::DoHandleError(error_code="
                << error_code << ")";
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  AudioEntry* entry = LookupByController(controller);
  if (!entry)
    return;

  DeleteEntryOnError(entry);
}

bool AudioInputRendererHost::OnMessageReceived(const IPC::Message& message,
                                               bool* message_was_ok) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_EX(AudioInputRendererHost, message, *message_was_ok)
    IPC_MESSAGE_HANDLER(AudioInputHostMsg_CreateStream, OnCreateStream)
    IPC_MESSAGE_HANDLER(AudioInputHostMsg_RecordStream, OnRecordStream)
    IPC_MESSAGE_HANDLER(AudioInputHostMsg_CloseStream, OnCloseStream)
    IPC_MESSAGE_HANDLER(AudioInputHostMsg_GetVolume, OnGetVolume)
    IPC_MESSAGE_HANDLER(AudioInputHostMsg_SetVolume, OnSetVolume)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP_EX()

  return handled;
}

void AudioInputRendererHost::OnCreateStream(
    const IPC::Message& msg, int stream_id,
    const AudioParameters& params, bool low_latency) {
  VLOG(1) << "AudioInputRendererHost::OnCreateStream(stream_id="
          << stream_id << ")";
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(LookupById(msg.routing_id(), stream_id) == NULL);

  // Prevent the renderer process from asking for a normal-latency
  // input stream.
  if (!low_latency) {
    NOTREACHED() << "Current implementation only supports low-latency mode.";
    return;
  }

  AudioParameters audio_params(params);

  // Select the hardware packet size if not specified.
  if (!audio_params.samples_per_packet) {
    audio_params.samples_per_packet = SelectSamplesPerPacket(audio_params);
  }
  uint32 packet_size = audio_params.GetPacketSize();

  scoped_ptr<AudioEntry> entry(new AudioEntry());
  // Create the shared memory and share with the renderer process.
  if (!entry->shared_memory.CreateAndMapAnonymous(packet_size)) {
    // If creation of shared memory failed then send an error message.
    SendErrorMessage(msg.routing_id(), stream_id);
    return;
  }

  // This is a low latency mode, hence we need to construct a SyncWriter first.
  scoped_ptr<AudioInputSyncWriter> writer(
      new AudioInputSyncWriter(&entry->shared_memory));

  // Then try to initialize the sync writer.
  if (!writer->Init()) {
    SendErrorMessage(msg.routing_id(), stream_id);
    return;
  }

  // If we have successfully created the SyncWriter then assign it to the
  // entry and construct an AudioInputController.
  entry->writer.reset(writer.release());
  entry->controller =
      media::AudioInputController::CreateLowLatency(this,
                                                    audio_params,
                                                    entry->writer.get());

  if (!entry->controller) {
    SendErrorMessage(msg.routing_id(), stream_id);
    return;
  }

  // If we have created the controller successfully create a entry and add it
  // to the map.
  entry->render_view_id = msg.routing_id();
  entry->stream_id = stream_id;

  audio_entries_.insert(std::make_pair(
      AudioEntryId(msg.routing_id(), stream_id),
      entry.release()));
}

void AudioInputRendererHost::OnRecordStream(const IPC::Message& msg,
                                            int stream_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  AudioEntry* entry = LookupById(msg.routing_id(), stream_id);
  if (!entry) {
    SendErrorMessage(msg.routing_id(), stream_id);
    return;
  }

  entry->controller->Record();
}

void AudioInputRendererHost::OnCloseStream(const IPC::Message& msg,
                                           int stream_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  AudioEntry* entry = LookupById(msg.routing_id(), stream_id);

  if (entry)
    CloseAndDeleteStream(entry);
}

void AudioInputRendererHost::OnSetVolume(const IPC::Message& msg, int stream_id,
                                    double volume) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  AudioEntry* entry = LookupById(msg.routing_id(), stream_id);
  if (!entry) {
    SendErrorMessage(msg.routing_id(), stream_id);
    return;
  }

  // TODO(henrika): TBI.
  NOTIMPLEMENTED();
}

void AudioInputRendererHost::OnGetVolume(const IPC::Message& msg,
                                         int stream_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  AudioEntry* entry = LookupById(msg.routing_id(), stream_id);
  if (!entry) {
    SendErrorMessage(msg.routing_id(), stream_id);
    return;
  }

  // TODO(henrika): TBI.
  NOTIMPLEMENTED();
}

void AudioInputRendererHost::SendErrorMessage(int32 render_view_id,
                                              int32 stream_id) {
  // TODO(henrika): error state for audio input is not unique
  Send(new AudioMsg_NotifyStreamStateChanged(render_view_id,
                                             stream_id,
                                             kAudioStreamError));
}

void AudioInputRendererHost::DeleteEntries() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  for (AudioEntryMap::iterator i = audio_entries_.begin();
       i != audio_entries_.end(); ++i) {
    CloseAndDeleteStream(i->second);
  }
}

void AudioInputRendererHost::CloseAndDeleteStream(AudioEntry* entry) {
  if (!entry->pending_close) {
    entry->pending_close = true;
    // TODO(henrika): AudioRendererHost uses an alternative method
    // to close down the AudioController. Try to refactor and merge
    // the implementations.
    entry->controller->Close();
    OnStreamClosed(entry);
  }
}

void AudioInputRendererHost::OnStreamClosed(AudioEntry* entry) {
  // Delete the entry after we've closed the stream.
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      NewRunnableMethod(this, &AudioInputRendererHost::DeleteEntry, entry));
}

void AudioInputRendererHost::DeleteEntry(AudioEntry* entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Delete the entry when this method goes out of scope.
  scoped_ptr<AudioEntry> entry_deleter(entry);

  // Erase the entry from the map.
  audio_entries_.erase(
      AudioEntryId(entry->render_view_id, entry->stream_id));
}

void AudioInputRendererHost::DeleteEntryOnError(AudioEntry* entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Sends the error message first before we close the stream because
  // |entry| is destroyed in DeleteEntry().
  SendErrorMessage(entry->render_view_id, entry->stream_id);
  CloseAndDeleteStream(entry);
}

AudioInputRendererHost::AudioEntry* AudioInputRendererHost::LookupById(
    int route_id, int stream_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  AudioEntryMap::iterator i = audio_entries_.find(
      AudioEntryId(route_id, stream_id));
  if (i != audio_entries_.end())
    return i->second;
  return NULL;
}

AudioInputRendererHost::AudioEntry* AudioInputRendererHost::LookupByController(
    media::AudioInputController* controller) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Iterate the map of entries.
  // TODO(hclam): Implement a faster look up method.
  for (AudioEntryMap::iterator i = audio_entries_.begin();
       i != audio_entries_.end(); ++i) {
    if (controller == i->second->controller.get())
      return i->second;
  }
  return NULL;
}
