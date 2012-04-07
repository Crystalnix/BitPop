// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/audio/audio_output_controller.h"

#include "base/bind.h"
#include "base/debug/trace_event.h"
#include "base/message_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "base/time.h"

using base::Time;
using base::WaitableEvent;

namespace media {

// Signal a pause in low-latency mode.
const int AudioOutputController::kPauseMark = -1;

// Polling-related constants.
const int AudioOutputController::kPollNumAttempts = 3;
const int AudioOutputController::kPollPauseInMilliseconds = 3;

AudioOutputController::AudioOutputController(AudioManager* audio_manager,
                                             EventHandler* handler,
                                             uint32 capacity,
                                             SyncReader* sync_reader)
    : audio_manager_(audio_manager),
      handler_(handler),
      stream_(NULL),
      volume_(1.0),
      state_(kEmpty),
      buffer_(0, capacity),
      pending_request_(false),
      sync_reader_(sync_reader),
      message_loop_(NULL),
      number_polling_attempts_left_(0),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_this_(this)) {
}

AudioOutputController::~AudioOutputController() {
  DCHECK_EQ(kClosed, state_);
  DCHECK(message_loop_);

  if (!message_loop_.get() || message_loop_->BelongsToCurrentThread()) {
    DoStopCloseAndClearStream(NULL);
  } else {
    WaitableEvent completion(true /* manual reset */,
                             false /* initial state */);
    message_loop_->PostTask(FROM_HERE,
        base::Bind(&AudioOutputController::DoStopCloseAndClearStream,
                   base::Unretained(this),
                   &completion));
    completion.Wait();
  }
}

// static
scoped_refptr<AudioOutputController> AudioOutputController::Create(
    AudioManager* audio_manager,
    EventHandler* event_handler,
    const AudioParameters& params,
    uint32 buffer_capacity) {
  DCHECK(audio_manager);
  if (!params.IsValid() ||  !audio_manager)
    return NULL;

  // Starts the audio controller thread.
  scoped_refptr<AudioOutputController> controller(new AudioOutputController(
      audio_manager, event_handler, buffer_capacity, NULL));

  controller->message_loop_ = audio_manager->GetMessageLoop();
  controller->message_loop_->PostTask(FROM_HERE, base::Bind(
      &AudioOutputController::DoCreate, base::Unretained(controller.get()),
      params));
  return controller;
}

// static
scoped_refptr<AudioOutputController> AudioOutputController::CreateLowLatency(
    AudioManager* audio_manager,
    EventHandler* event_handler,
    const AudioParameters& params,
    SyncReader* sync_reader) {
  DCHECK(audio_manager);
  DCHECK(sync_reader);

  if (!params.IsValid() || !audio_manager)
    return NULL;

  // Starts the audio controller thread.
  scoped_refptr<AudioOutputController> controller(new AudioOutputController(
      audio_manager, event_handler, 0, sync_reader));

  controller->message_loop_ = audio_manager->GetMessageLoop();
  controller->message_loop_->PostTask(FROM_HERE, base::Bind(
      &AudioOutputController::DoCreate, base::Unretained(controller.get()),
      params));
  return controller;
}

void AudioOutputController::Play() {
  DCHECK(message_loop_);
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &AudioOutputController::DoPlay, base::Unretained(this)));
}

void AudioOutputController::Pause() {
  DCHECK(message_loop_);
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &AudioOutputController::DoPause, base::Unretained(this)));
}

void AudioOutputController::Flush() {
  DCHECK(message_loop_);
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &AudioOutputController::DoFlush, base::Unretained(this)));
}

void AudioOutputController::Close(const base::Closure& closed_task) {
  DCHECK(!closed_task.is_null());
  DCHECK(message_loop_);
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &AudioOutputController::DoClose, base::Unretained(this), closed_task));
}

void AudioOutputController::SetVolume(double volume) {
  DCHECK(message_loop_);
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &AudioOutputController::DoSetVolume, base::Unretained(this), volume));
}

void AudioOutputController::EnqueueData(const uint8* data, uint32 size) {
  // Write data to the push source and ask for more data if needed.
  base::AutoLock auto_lock(lock_);
  pending_request_ = false;
  // If |size| is set to 0, it indicates that the audio source doesn't have
  // more data right now, and so it doesn't make sense to send additional
  // request.
  if (size) {
    buffer_.Append(data, size);
    SubmitOnMoreData_Locked();
  }
}

void AudioOutputController::DoCreate(const AudioParameters& params) {
  DCHECK(message_loop_->BelongsToCurrentThread());

  // Close() can be called before DoCreate() is executed.
  if (state_ == kClosed)
    return;
  DCHECK_EQ(kEmpty, state_);

  DoStopCloseAndClearStream(NULL);
  stream_ = audio_manager_->MakeAudioOutputStreamProxy(params);
  if (!stream_) {
    // TODO(hclam): Define error types.
    handler_->OnError(this, 0);
    return;
  }

  if (!stream_->Open()) {
    DoStopCloseAndClearStream(NULL);

    // TODO(hclam): Define error types.
    handler_->OnError(this, 0);
    return;
  }

  // We have successfully opened the stream. Set the initial volume.
  stream_->SetVolume(volume_);

  // Finally set the state to kCreated.
  state_ = kCreated;

  // And then report we have been created.
  handler_->OnCreated(this);

  // If in normal latency mode then start buffering.
  if (!LowLatencyMode()) {
    base::AutoLock auto_lock(lock_);
    SubmitOnMoreData_Locked();
  }
}

void AudioOutputController::DoPlay() {
  DCHECK(message_loop_->BelongsToCurrentThread());

  // We can start from created or paused state.
  if (state_ != kCreated && state_ != kPaused)
    return;

  if (LowLatencyMode()) {
    state_ = kStarting;

    // Ask for first packet.
    sync_reader_->UpdatePendingBytes(0);

    // Cannot start stream immediately, should give renderer some time
    // to deliver data.
    number_polling_attempts_left_ = kPollNumAttempts;
    message_loop_->PostDelayedTask(
        FROM_HERE,
        base::Bind(&AudioOutputController::PollAndStartIfDataReady,
        weak_this_.GetWeakPtr()),
        kPollPauseInMilliseconds);
  } else {
    StartStream();
  }
}

void AudioOutputController::PollAndStartIfDataReady() {
  DCHECK(message_loop_->BelongsToCurrentThread());

  // Being paranoid: do nothing if state unexpectedly changed.
  if ((state_ != kStarting) && (state_ != kPausedWhenStarting))
    return;

  bool pausing = (state_ == kPausedWhenStarting);
  // If we are ready to start the stream, start it.
  // Of course we may have to stop it immediately...
  if (--number_polling_attempts_left_ == 0 ||
      pausing ||
      sync_reader_->DataReady()) {
    StartStream();
    if (pausing) {
      DoPause();
    }
  } else {
    message_loop_->PostDelayedTask(
        FROM_HERE,
        base::Bind(&AudioOutputController::PollAndStartIfDataReady,
        weak_this_.GetWeakPtr()),
        kPollPauseInMilliseconds);
  }
}

void AudioOutputController::StartStream() {
  DCHECK(message_loop_->BelongsToCurrentThread());
  state_ = kPlaying;

  // We start the AudioOutputStream lazily.
  stream_->Start(this);

  // Tell the event handler that we are now playing.
  handler_->OnPlaying(this);
}

void AudioOutputController::DoPause() {
  DCHECK(message_loop_->BelongsToCurrentThread());

  if (stream_)
    stream_->Stop();

  switch (state_) {
    case kStarting:
      // We were asked to pause while starting. There is delayed task that will
      // try starting playback, and there is no way to remove that task from the
      // queue. If we stop now that task will be executed anyway.
      // Delay pausing, let delayed task to do pause after it start playback.
      state_ = kPausedWhenStarting;
      break;
    case kPlaying:
      state_ = kPaused;

      // Then we stop the audio device. This is not the perfect solution
      // because it discards all the internal buffer in the audio device.
      // TODO(hclam): Actually pause the audio device.
      stream_->Stop();

      if (LowLatencyMode()) {
        // Send a special pause mark to the low-latency audio thread.
        sync_reader_->UpdatePendingBytes(kPauseMark);
      }

      handler_->OnPaused(this);
      break;
    default:
      return;
  }
}

void AudioOutputController::DoFlush() {
  DCHECK(message_loop_->BelongsToCurrentThread());

  // TODO(hclam): Actually flush the audio device.

  // If we are in the regular latency mode then flush the push source.
  if (!sync_reader_) {
    if (state_ != kPaused)
      return;
    base::AutoLock auto_lock(lock_);
    buffer_.Clear();
  }
}

void AudioOutputController::DoClose(const base::Closure& closed_task) {
  DCHECK(message_loop_->BelongsToCurrentThread());

  if (state_ != kClosed) {
    DoStopCloseAndClearStream(NULL);

    if (LowLatencyMode()) {
      sync_reader_->Close();
    }

    state_ = kClosed;
  }

  closed_task.Run();
}

void AudioOutputController::DoSetVolume(double volume) {
  DCHECK(message_loop_->BelongsToCurrentThread());

  // Saves the volume to a member first. We may not be able to set the volume
  // right away but when the stream is created we'll set the volume.
  volume_ = volume;

  switch (state_) {
    case kCreated:
    case kStarting:
    case kPausedWhenStarting:
    case kPlaying:
    case kPaused:
      stream_->SetVolume(volume_);
      break;
    default:
      return;
  }
}

void AudioOutputController::DoReportError(int code) {
  DCHECK(message_loop_->BelongsToCurrentThread());
  if (state_ != kClosed)
    handler_->OnError(this, code);
}

uint32 AudioOutputController::OnMoreData(
    AudioOutputStream* stream, uint8* dest,
    uint32 max_size, AudioBuffersState buffers_state) {
  TRACE_EVENT0("audio", "AudioOutputController::OnMoreData");

  // If regular latency mode is used.
  if (!sync_reader_) {
    base::AutoLock auto_lock(lock_);

    // Save current buffers state.
    buffers_state_ = buffers_state;

    if (state_ != kPlaying) {
      // Don't read anything. Save the number of bytes in the hardware buffer.
      return 0;
    }

    uint32 size = buffer_.Read(dest, max_size);
    buffers_state_.pending_bytes += size;
    SubmitOnMoreData_Locked();
    return size;
  }

  // Low latency mode.
  {
    // Check state and do nothing if we are not playing.
    // We are on the hardware audio thread, so lock is needed.
    base::AutoLock auto_lock(lock_);
    if (state_ != kPlaying) {
      return 0;
    }
  }
  uint32 size =  sync_reader_->Read(dest, max_size);
  sync_reader_->UpdatePendingBytes(buffers_state.total_bytes() + size);
  return size;
}

void AudioOutputController::WaitTillDataReady() {
  if (LowLatencyMode() && !sync_reader_->DataReady()) {
    // In the different place we use different mechanism to poll, get max
    // polling delay from constants used there.
    const base::TimeDelta kMaxPollingDelay = base::TimeDelta::FromMilliseconds(
        kPollNumAttempts * kPollPauseInMilliseconds);
    Time start_time = Time::Now();
    do {
      base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(1));
    } while (!sync_reader_->DataReady() &&
             Time::Now() - start_time < kMaxPollingDelay);
  }
}

void AudioOutputController::OnError(AudioOutputStream* stream, int code) {
  // Handle error on the audio controller thread.
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &AudioOutputController::DoReportError, base::Unretained(this), code));
}

void AudioOutputController::SubmitOnMoreData_Locked() {
  lock_.AssertAcquired();

  if (buffer_.forward_bytes() > buffer_.forward_capacity())
    return;

  if (pending_request_)
    return;
  pending_request_ = true;

  AudioBuffersState buffers_state = buffers_state_;
  buffers_state.pending_bytes += buffer_.forward_bytes();

  // If we need more data then call the event handler to ask for more data.
  // It is okay that we don't lock in this block because the parameters are
  // correct and in the worst case we are just asking more data than needed.
  base::AutoUnlock auto_unlock(lock_);
  handler_->OnMoreData(this, buffers_state);
}

void AudioOutputController::DoStopCloseAndClearStream(WaitableEvent *done) {
  DCHECK(message_loop_->BelongsToCurrentThread());

  // Allow calling unconditionally and bail if we don't have a stream_ to close.
  if (stream_ != NULL) {
    stream_->Stop();
    stream_->Close();
    stream_ = NULL;
    weak_this_.InvalidateWeakPtrs();
  }

  // Should be last in the method, do not touch "this" from here on.
  if (done != NULL)
    done->Signal();
}

}  // namespace media
