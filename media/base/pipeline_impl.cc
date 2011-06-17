// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// TODO(scherkus): clean up PipelineImpl... too many crazy function names,
// potential deadlocks, etc...

#include "media/base/pipeline_impl.h"

#include <algorithm>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/stl_util-inl.h"
#include "base/synchronization/condition_variable.h"
#include "media/filters/rtc_video_decoder.h"
#include "media/base/clock.h"
#include "media/base/filter_collection.h"
#include "media/base/media_format.h"

namespace media {

PipelineStatusNotification::PipelineStatusNotification()
    : cv_(&lock_), status_(PIPELINE_OK), notified_(false) {
  callback_.reset(NewCallback(this, &PipelineStatusNotification::Notify));
}

PipelineStatusNotification::~PipelineStatusNotification() {
  DCHECK(notified_);
}

media::PipelineStatusCallback* PipelineStatusNotification::Callback() {
  return callback_.release();
}

void PipelineStatusNotification::Notify(media::PipelineStatus status) {
  base::AutoLock auto_lock(lock_);
  DCHECK(!notified_);
  notified_ = true;
  status_ = status;
  cv_.Signal();
}

void PipelineStatusNotification::Wait() {
  base::AutoLock auto_lock(lock_);
  while (!notified_)
    cv_.Wait();
}

media::PipelineStatus PipelineStatusNotification::status() {
  base::AutoLock auto_lock(lock_);
  DCHECK(notified_);
  return status_;
}

class PipelineImpl::PipelineInitState {
 public:
  scoped_refptr<AudioDecoder> audio_decoder_;
  scoped_refptr<VideoDecoder> video_decoder_;
  scoped_refptr<CompositeFilter> composite_;
};

PipelineImpl::PipelineImpl(MessageLoop* message_loop)
    : message_loop_(message_loop),
      clock_(new Clock(&base::Time::Now)),
      waiting_for_clock_update_(false),
      state_(kCreated),
      current_bytes_(0) {
  ResetState();
}

PipelineImpl::~PipelineImpl() {
  base::AutoLock auto_lock(lock_);
  DCHECK(!running_) << "Stop() must complete before destroying object";
  DCHECK(!stop_pending_);
  DCHECK(!seek_pending_);
}

void PipelineImpl::Init(PipelineStatusCallback* ended_callback,
                        PipelineStatusCallback* error_callback,
                        PipelineStatusCallback* network_callback) {
  DCHECK(!IsRunning())
      << "Init() should be called before the pipeline has started";
  ended_callback_.reset(ended_callback);
  error_callback_.reset(error_callback);
  network_callback_.reset(network_callback);
}

// Creates the PipelineInternal and calls it's start method.
bool PipelineImpl::Start(FilterCollection* collection,
                         const std::string& url,
                         PipelineStatusCallback* start_callback) {
  base::AutoLock auto_lock(lock_);
  scoped_ptr<PipelineStatusCallback> callback(start_callback);
  scoped_ptr<FilterCollection> filter_collection(collection);

  if (running_) {
    VLOG(1) << "Media pipeline is already running";
    return false;
  }

  if (collection->IsEmpty()) {
    return false;
  }

  // Kick off initialization!
  running_ = true;
  message_loop_->PostTask(
      FROM_HERE,
      NewRunnableMethod(this,
                        &PipelineImpl::StartTask,
                        filter_collection.release(),
                        url,
                        callback.release()));
  return true;
}

void PipelineImpl::Stop(PipelineStatusCallback* stop_callback) {
  base::AutoLock auto_lock(lock_);
  scoped_ptr<PipelineStatusCallback> callback(stop_callback);
  if (!running_) {
    VLOG(1) << "Media pipeline has already stopped";
    return;
  }

  // Stop the pipeline, which will set |running_| to false on behalf.
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineImpl::StopTask, callback.release()));
}

void PipelineImpl::Seek(base::TimeDelta time,
                        PipelineStatusCallback* seek_callback) {
  base::AutoLock auto_lock(lock_);
  scoped_ptr<PipelineStatusCallback> callback(seek_callback);
  if (!running_) {
    VLOG(1) << "Media pipeline must be running";
    return;
  }

  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineImpl::SeekTask, time,
                        callback.release()));
}

bool PipelineImpl::IsRunning() const {
  base::AutoLock auto_lock(lock_);
  return running_;
}

bool PipelineImpl::IsInitialized() const {
  // TODO(scherkus): perhaps replace this with a bool that is set/get under the
  // lock, because this is breaching the contract that |state_| is only accessed
  // on |message_loop_|.
  base::AutoLock auto_lock(lock_);
  switch (state_) {
    case kPausing:
    case kFlushing:
    case kSeeking:
    case kStarting:
    case kStarted:
    case kEnded:
      return true;
    default:
      return false;
  }
}

bool PipelineImpl::IsNetworkActive() const {
  base::AutoLock auto_lock(lock_);
  return network_activity_;
}

bool PipelineImpl::HasAudio() const {
  base::AutoLock auto_lock(lock_);
  return has_audio_;
}

bool PipelineImpl::HasVideo() const {
  base::AutoLock auto_lock(lock_);
  return has_video_;
}

float PipelineImpl::GetPlaybackRate() const {
  base::AutoLock auto_lock(lock_);
  return playback_rate_;
}

void PipelineImpl::SetPlaybackRate(float playback_rate) {
  if (playback_rate < 0.0f) {
    return;
  }

  base::AutoLock auto_lock(lock_);
  playback_rate_ = playback_rate;
  if (running_) {
    message_loop_->PostTask(FROM_HERE,
        NewRunnableMethod(this, &PipelineImpl::PlaybackRateChangedTask,
                          playback_rate));
  }
}

float PipelineImpl::GetVolume() const {
  base::AutoLock auto_lock(lock_);
  return volume_;
}

void PipelineImpl::SetVolume(float volume) {
  if (volume < 0.0f || volume > 1.0f) {
    return;
  }

  base::AutoLock auto_lock(lock_);
  volume_ = volume;
  if (running_) {
    message_loop_->PostTask(FROM_HERE,
        NewRunnableMethod(this, &PipelineImpl::VolumeChangedTask,
                          volume));
  }
}

Preload PipelineImpl::GetPreload() const {
  base::AutoLock auto_lock(lock_);
  return preload_;
}

void PipelineImpl::SetPreload(Preload preload) {
  base::AutoLock auto_lock(lock_);
  preload_ = preload;
  if (running_) {
    message_loop_->PostTask(FROM_HERE,
        NewRunnableMethod(this, &PipelineImpl::PreloadChangedTask,
                          preload));
  }
}

base::TimeDelta PipelineImpl::GetCurrentTime() const {
  // TODO(scherkus): perhaps replace checking state_ == kEnded with a bool that
  // is set/get under the lock, because this is breaching the contract that
  // |state_| is only accessed on |message_loop_|.
  base::AutoLock auto_lock(lock_);
  return GetCurrentTime_Locked();
}

base::TimeDelta PipelineImpl::GetCurrentTime_Locked() const {
  base::TimeDelta elapsed = clock_->Elapsed();
  if (state_ == kEnded || elapsed > duration_) {
    return duration_;
  }
  return elapsed;
}

base::TimeDelta PipelineImpl::GetBufferedTime() {
  base::AutoLock auto_lock(lock_);

  // If media is fully loaded, then return duration.
  if (loaded_ || total_bytes_ == buffered_bytes_) {
    max_buffered_time_ = duration_;
    return duration_;
  }

  base::TimeDelta current_time = GetCurrentTime_Locked();

  // If buffered time was set, we report that value directly.
  if (buffered_time_.ToInternalValue() > 0) {
    return std::max(buffered_time_, current_time);
  }

  if (total_bytes_ == 0)
    return base::TimeDelta();

  // If buffered time was not set, we use current time, current bytes, and
  // buffered bytes to estimate the buffered time.
  double estimated_rate = duration_.InMillisecondsF() / total_bytes_;
  double estimated_current_time = estimated_rate * current_bytes_;
  DCHECK_GE(buffered_bytes_, current_bytes_);
  base::TimeDelta buffered_time = base::TimeDelta::FromMilliseconds(
      static_cast<int64>(estimated_rate * (buffered_bytes_ - current_bytes_) +
                         estimated_current_time));

  // Cap approximated buffered time at the length of the video.
  buffered_time = std::min(buffered_time, duration_);

  // Make sure buffered_time is at least the current time
  buffered_time = std::max(buffered_time, current_time);

  // Only print the max buffered time for smooth buffering.
  max_buffered_time_ = std::max(buffered_time, max_buffered_time_);

  return max_buffered_time_;
}

base::TimeDelta PipelineImpl::GetMediaDuration() const {
  base::AutoLock auto_lock(lock_);
  return duration_;
}

int64 PipelineImpl::GetBufferedBytes() const {
  base::AutoLock auto_lock(lock_);
  return buffered_bytes_;
}

int64 PipelineImpl::GetTotalBytes() const {
  base::AutoLock auto_lock(lock_);
  return total_bytes_;
}

void PipelineImpl::GetVideoSize(size_t* width_out, size_t* height_out) const {
  CHECK(width_out);
  CHECK(height_out);
  base::AutoLock auto_lock(lock_);
  *width_out = video_width_;
  *height_out = video_height_;
}

bool PipelineImpl::IsStreaming() const {
  base::AutoLock auto_lock(lock_);
  return streaming_;
}

bool PipelineImpl::IsLoaded() const {
  base::AutoLock auto_lock(lock_);
  return loaded_;
}

PipelineStatistics PipelineImpl::GetStatistics() const {
  base::AutoLock auto_lock(lock_);
  return statistics_;
}

void PipelineImpl::SetClockForTesting(Clock* clock) {
  clock_.reset(clock);
}

void PipelineImpl::SetCurrentReadPosition(int64 offset) {
  base::AutoLock auto_lock(lock_);

  // The current read position should never be ahead of the buffered byte
  // position but threading issues between BufferedDataSource::DoneRead_Locked()
  // and BufferedDataSource::NetworkEventCallback() can cause them to be
  // temporarily out of sync. The easiest fix for this is to cap both
  // buffered_bytes_ and current_bytes_ to always be legal values in
  // SetCurrentReadPosition() and in SetBufferedBytes().
  if (offset > buffered_bytes_)
    buffered_bytes_ = offset;
  current_bytes_ = offset;
}

int64 PipelineImpl::GetCurrentReadPosition() {
  base::AutoLock auto_lock(lock_);
  return current_bytes_;
}

void PipelineImpl::ResetState() {
  base::AutoLock auto_lock(lock_);
  const base::TimeDelta kZero;
  running_          = false;
  stop_pending_     = false;
  seek_pending_     = false;
  tearing_down_     = false;
  error_caused_teardown_ = false;
  playback_rate_change_pending_ = false;
  duration_         = kZero;
  buffered_time_    = kZero;
  buffered_bytes_   = 0;
  streaming_        = false;
  loaded_           = false;
  total_bytes_      = 0;
  video_width_      = 0;
  video_height_     = 0;
  volume_           = 1.0f;
  preload_          = AUTO;
  playback_rate_    = 0.0f;
  pending_playback_rate_ = 0.0f;
  status_           = PIPELINE_OK;
  has_audio_        = false;
  has_video_        = false;
  waiting_for_clock_update_ = false;
  audio_disabled_   = false;
  clock_->SetTime(kZero);
}

void PipelineImpl::set_state(State next_state) {
  state_ = next_state;
}

bool PipelineImpl::IsPipelineOk() {
  base::AutoLock auto_lock(lock_);
  return status_ == PIPELINE_OK;
}

bool PipelineImpl::IsPipelineStopped() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  return state_ == kStopped || state_ == kError;
}

bool PipelineImpl::IsPipelineTearingDown() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  return tearing_down_;
}

bool PipelineImpl::IsPipelineStopPending() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  return stop_pending_;
}

bool PipelineImpl::IsPipelineSeeking() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  if (!seek_pending_)
    return false;
  DCHECK(kSeeking == state_ || kPausing == state_ ||
         kFlushing == state_ || kStarting == state_)
      << "Current state : " << state_;
  return true;
}

void PipelineImpl::FinishInitialization() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  // Execute the seek callback, if present.  Note that this might be the
  // initial callback passed into Start().
  if (seek_callback_.get()) {
    seek_callback_->Run(status_);
    seek_callback_.reset();
  }
}

// static
bool PipelineImpl::TransientState(State state) {
  return state == kPausing ||
         state == kFlushing ||
         state == kSeeking ||
         state == kStarting ||
         state == kStopping;
}

// static
PipelineImpl::State PipelineImpl::FindNextState(State current) {
  // TODO(scherkus): refactor InitializeTask() to make use of this function.
  if (current == kPausing) {
    return kFlushing;
  } else if (current == kFlushing) {
    // We will always honor Seek() before Stop(). This is based on the
    // assumption that we never accept Seek() after Stop().
    DCHECK(IsPipelineSeeking() ||
           IsPipelineStopPending() ||
           IsPipelineTearingDown());
    return IsPipelineSeeking() ? kSeeking : kStopping;
  } else if (current == kSeeking) {
    return kStarting;
  } else if (current == kStarting) {
    return kStarted;
  } else if (current == kStopping) {
    return error_caused_teardown_ ? kError : kStopped;
  } else {
    return current;
  }
}

void PipelineImpl::SetError(PipelineStatus error) {
  DCHECK(IsRunning());
  DCHECK(error != PIPELINE_OK) << "PIPELINE_OK isn't an error!";
  VLOG(1) << "Media pipeline error: " << error;

  message_loop_->PostTask(FROM_HERE,
     NewRunnableMethod(this, &PipelineImpl::ErrorChangedTask, error));
}

base::TimeDelta PipelineImpl::GetTime() const {
  DCHECK(IsRunning());
  return GetCurrentTime();
}

base::TimeDelta PipelineImpl::GetDuration() const {
  DCHECK(IsRunning());
  return GetMediaDuration();
}

void PipelineImpl::SetTime(base::TimeDelta time) {
  DCHECK(IsRunning());
  base::AutoLock auto_lock(lock_);

  // If we were waiting for a valid timestamp and such timestamp arrives, we
  // need to clear the flag for waiting and start the clock.
  if (waiting_for_clock_update_) {
    if (time < clock_->Elapsed())
      return;
    waiting_for_clock_update_ = false;
    clock_->SetTime(time);
    clock_->Play();
    return;
  }
  clock_->SetTime(time);
}

void PipelineImpl::SetDuration(base::TimeDelta duration) {
  DCHECK(IsRunning());
  base::AutoLock auto_lock(lock_);
  duration_ = duration;
}

void PipelineImpl::SetBufferedTime(base::TimeDelta buffered_time) {
  DCHECK(IsRunning());
  base::AutoLock auto_lock(lock_);
  buffered_time_ = buffered_time;
}

void PipelineImpl::SetTotalBytes(int64 total_bytes) {
  DCHECK(IsRunning());
  base::AutoLock auto_lock(lock_);
  total_bytes_ = total_bytes;
}

void PipelineImpl::SetBufferedBytes(int64 buffered_bytes) {
  DCHECK(IsRunning());
  base::AutoLock auto_lock(lock_);

  // See comments in SetCurrentReadPosition() about capping.
  if (buffered_bytes < current_bytes_)
    current_bytes_ = buffered_bytes;
  buffered_bytes_ = buffered_bytes;
}

void PipelineImpl::SetVideoSize(size_t width, size_t height) {
  DCHECK(IsRunning());
  base::AutoLock auto_lock(lock_);
  video_width_ = width;
  video_height_ = height;
}

void PipelineImpl::SetStreaming(bool streaming) {
  DCHECK(IsRunning());
  base::AutoLock auto_lock(lock_);
  streaming_ = streaming;
}

void PipelineImpl::NotifyEnded() {
  DCHECK(IsRunning());
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineImpl::NotifyEndedTask));
}

void PipelineImpl::SetLoaded(bool loaded) {
  DCHECK(IsRunning());
  base::AutoLock auto_lock(lock_);
  loaded_ = loaded;
}

void PipelineImpl::SetNetworkActivity(bool network_activity) {
  DCHECK(IsRunning());
  {
    base::AutoLock auto_lock(lock_);
    network_activity_ = network_activity;
  }
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineImpl::NotifyNetworkEventTask));
}

void PipelineImpl::DisableAudioRenderer() {
  DCHECK(IsRunning());

  // Disable renderer on the message loop.
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineImpl::DisableAudioRendererTask));
}

// Called from any thread.
void PipelineImpl::OnFilterInitialize() {
  // Continue the initialize task by proceeding to the next stage.
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineImpl::InitializeTask));
}

// Called from any thread.
void PipelineImpl::OnFilterStateTransition() {
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineImpl::FilterStateTransitionTask));
}

void PipelineImpl::OnTeardownStateTransition() {
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineImpl::TeardownStateTransitionTask));
}

// Called from any thread.
void PipelineImpl::OnUpdateStatistics(const PipelineStatistics& stats) {
  base::AutoLock auto_lock(lock_);
  statistics_.audio_bytes_decoded += stats.audio_bytes_decoded;
  statistics_.video_bytes_decoded += stats.video_bytes_decoded;
  statistics_.video_frames_decoded += stats.video_frames_decoded;
  statistics_.video_frames_dropped += stats.video_frames_dropped;
}

void PipelineImpl::StartTask(FilterCollection* filter_collection,
                             const std::string& url,
                             PipelineStatusCallback* start_callback) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK_EQ(kCreated, state_);
  filter_collection_.reset(filter_collection);
  url_ = url;
  seek_callback_.reset(start_callback);

  // Kick off initialization.
  pipeline_init_state_.reset(new PipelineInitState());
  pipeline_init_state_->composite_ = new CompositeFilter(message_loop_);
  pipeline_init_state_->composite_->set_host(this);

  if (RTCVideoDecoder::IsUrlSupported(url)) {
    set_state(kInitVideoDecoder);
    InitializeVideoDecoder(NULL);
  } else {
    set_state(kInitDemuxer);
    InitializeDemuxer();
  }
}

// Main initialization method called on the pipeline thread.  This code attempts
// to use the specified filter factory to build a pipeline.
// Initialization step performed in this method depends on current state of this
// object, indicated by |state_|.  After each step of initialization, this
// object transits to the next stage.  It starts by creating a Demuxer, and then
// connects the Demuxer's audio stream to an AudioDecoder which is then
// connected to an AudioRenderer.  If the media has video, then it connects a
// VideoDecoder to the Demuxer's video stream, and then connects the
// VideoDecoder to a VideoRenderer.
//
// When all required filters have been created and have called their
// FilterHost's InitializationComplete() method, the pipeline will update its
// state to kStarted and |init_callback_|, will be executed.
//
// TODO(hclam): InitializeTask() is now starting the pipeline asynchronously. It
// works like a big state change table. If we no longer need to start filters
// in order, we need to get rid of all the state change.
void PipelineImpl::InitializeTask() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  // If we have received the stop or error signal, return immediately.
  if (IsPipelineStopPending() || IsPipelineStopped() || !IsPipelineOk())
    return;

  DCHECK(state_ == kInitDemuxer ||
         state_ == kInitAudioDecoder ||
         state_ == kInitAudioRenderer ||
         state_ == kInitVideoDecoder ||
         state_ == kInitVideoRenderer);


  // Demuxer created, create audio decoder.
  if (state_ == kInitDemuxer) {
    set_state(kInitAudioDecoder);
    // If this method returns false, then there's no audio stream.
    if (InitializeAudioDecoder(demuxer_))
      return;
  }

  // Assuming audio decoder was created, create audio renderer.
  if (state_ == kInitAudioDecoder) {
    set_state(kInitAudioRenderer);
    // Returns false if there's no audio stream.
    if (InitializeAudioRenderer(pipeline_init_state_->audio_decoder_)) {
      base::AutoLock auto_lock(lock_);
      has_audio_ = true;
      return;
    }
  }

  // Assuming audio renderer was created, create video decoder.
  if (state_ == kInitAudioRenderer) {
    // Then perform the stage of initialization, i.e. initialize video decoder.
    set_state(kInitVideoDecoder);
    if (InitializeVideoDecoder(demuxer_))
      return;
  }

  // Assuming video decoder was created, create video renderer.
  if (state_ == kInitVideoDecoder) {
    set_state(kInitVideoRenderer);
    if (InitializeVideoRenderer(pipeline_init_state_->video_decoder_)) {
      base::AutoLock auto_lock(lock_);
      has_video_ = true;
      return;
    }
  }

  if (state_ == kInitVideoRenderer) {
    if (!IsPipelineOk() || !(HasAudio() || HasVideo())) {
      SetError(PIPELINE_ERROR_COULD_NOT_RENDER);
      return;
    }

    // Clear the collection of filters.
    filter_collection_->Clear();

    pipeline_filter_ = pipeline_init_state_->composite_;

    // Clear init state since we're done initializing.
    pipeline_init_state_.reset();

    if (audio_disabled_) {
      // Audio was disabled at some point during initialization. Notify
      // the pipeline filter now that it has been initialized.
      pipeline_filter_->OnAudioRendererDisabled();
    }

    // Initialization was successful, we are now considered paused, so it's safe
    // to set the initial playback rate and volume.
    PreloadChangedTask(GetPreload());
    PlaybackRateChangedTask(GetPlaybackRate());
    VolumeChangedTask(GetVolume());

    // Fire the seek request to get the filters to preroll.
    seek_pending_ = true;
    set_state(kSeeking);
    seek_timestamp_ = base::TimeDelta();
    pipeline_filter_->Seek(seek_timestamp_,
        NewCallback(this, &PipelineImpl::OnFilterStateTransition));
  }
}

// This method is called as a result of the client calling Pipeline::Stop() or
// as the result of an error condition.
// We stop the filters in the reverse order.
//
// TODO(scherkus): beware!  this can get posted multiple times since we post
// Stop() tasks even if we've already stopped.  Perhaps this should no-op for
// additional calls, however most of this logic will be changing.
void PipelineImpl::StopTask(PipelineStatusCallback* stop_callback) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(!IsPipelineStopPending());
  DCHECK_NE(state_, kStopped);

  if (state_ == kStopped) {
    // Already stopped so just run callback.
    stop_callback->Run(status_);
    delete stop_callback;
    return;
  }

  if (IsPipelineTearingDown() && error_caused_teardown_) {
    // If we are stopping due to SetError(), stop normally instead of
    // going to error state and calling |error_callback_|. This converts
    // the teardown in progress from an error teardown into one that acts
    // like the error never occurred.
    base::AutoLock auto_lock(lock_);
    status_ = PIPELINE_OK;
    error_caused_teardown_ = false;
  }

  stop_callback_.reset(stop_callback);

  stop_pending_ = true;
  if (!IsPipelineSeeking() && !IsPipelineTearingDown()) {
    // We will tear down pipeline immediately when there is no seek operation
    // pending and no teardown in progress. This should include the case where
    // we are partially initialized.
    TearDownPipeline();
  }
}

void PipelineImpl::ErrorChangedTask(PipelineStatus error) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK_NE(PIPELINE_OK, error) << "PIPELINE_OK isn't an error!";

  // Suppress executing additional error logic. Note that if we are currently
  // performing a normal stop, then we return immediately and continue the
  // normal stop.
  if (IsPipelineStopped() || IsPipelineTearingDown()) {
    return;
  }

  base::AutoLock auto_lock(lock_);
  status_ = error;

  error_caused_teardown_ = true;

  // Posting TearDownPipeline() to message loop so that we can make sure
  // it runs after any pending callbacks that are already queued.
  // |tearing_down_| is set early here to make sure that pending callbacks
  // don't modify the state before TeadDownPipeline() can run.
  tearing_down_ = true;
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineImpl::TearDownPipeline));
}

void PipelineImpl::PlaybackRateChangedTask(float playback_rate) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  // Suppress rate change until after seeking.
  if (IsPipelineSeeking()) {
    pending_playback_rate_ = playback_rate;
    playback_rate_change_pending_ = true;
    return;
  }

  {
    base::AutoLock auto_lock(lock_);
    clock_->SetPlaybackRate(playback_rate);
  }

  // Notify |pipeline_filter_| if it has been initialized. If initialization
  // hasn't completed yet, the playback rate will be set when initialization
  // completes.
  if (pipeline_filter_) {
    pipeline_filter_->SetPlaybackRate(playback_rate);
  }
}

void PipelineImpl::VolumeChangedTask(float volume) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  if (audio_renderer_) {
    audio_renderer_->SetVolume(volume);
  }
}

void PipelineImpl::PreloadChangedTask(Preload preload) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  if (demuxer_)
    demuxer_->SetPreload(preload);
}

void PipelineImpl::SeekTask(base::TimeDelta time,
                            PipelineStatusCallback* seek_callback) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(!IsPipelineStopPending());

  // Suppress seeking if we're not fully started.
  if (state_ != kStarted && state_ != kEnded) {
    // TODO(scherkus): should we run the callback?  I'm tempted to say the API
    // will only execute the first Seek() request.
    VLOG(1) << "Media pipeline has not started, ignoring seek to "
              << time.InMicroseconds();
    delete seek_callback;
    return;
  }

  DCHECK(!seek_pending_);
  seek_pending_ = true;

  // We'll need to pause every filter before seeking.  The state transition
  // is as follows:
  //   kStarted/kEnded
  //   kPausing (for each filter)
  //   kSeeking (for each filter)
  //   kStarting (for each filter)
  //   kStarted
  set_state(kPausing);
  seek_timestamp_ = time;
  seek_callback_.reset(seek_callback);

  // Kick off seeking!
  {
    base::AutoLock auto_lock(lock_);
    // If we are waiting for a clock update, the clock hasn't been played yet.
    if (!waiting_for_clock_update_)
      clock_->Pause();
  }
  pipeline_filter_->Pause(
      NewCallback(this, &PipelineImpl::OnFilterStateTransition));
}

void PipelineImpl::NotifyEndedTask() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  // We can only end if we were actually playing.
  if (state_ != kStarted) {
    return;
  }

  DCHECK(audio_renderer_ || video_renderer_);

  // Make sure every extant renderer has ended.
  if (audio_renderer_ && !audio_disabled_) {
    if (!audio_renderer_->HasEnded()) {
      return;
    }

    if (waiting_for_clock_update_) {
      // Start clock since there is no more audio to
      // trigger clock updates.
      waiting_for_clock_update_ = false;
      clock_->Play();
    }
  }

  if (video_renderer_ && !video_renderer_->HasEnded()) {
    return;
  }

  // Transition to ended, executing the callback if present.
  set_state(kEnded);
  if (ended_callback_.get()) {
    ended_callback_->Run(status_);
  }
}

void PipelineImpl::NotifyNetworkEventTask() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  if (network_callback_.get()) {
    network_callback_->Run(status_);
  }
}

void PipelineImpl::DisableAudioRendererTask() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  base::AutoLock auto_lock(lock_);
  has_audio_ = false;
  audio_disabled_ = true;

  // Notify all filters of disabled audio renderer. If the filter isn't
  // initialized yet, OnAudioRendererDisabled() will be called when
  // initialization is complete.
  if (pipeline_filter_) {
    pipeline_filter_->OnAudioRendererDisabled();
  }
}

void PipelineImpl::FilterStateTransitionTask() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  // No reason transitioning if we've errored or have stopped.
  if (IsPipelineStopped()) {
    return;
  }

  // If we are tearing down, don't allow any state changes. Teardown
  // state changes will come in via TeardownStateTransitionTask().
  if (IsPipelineTearingDown()) {
    return;
  }

  if (!TransientState(state_)) {
    NOTREACHED() << "Invalid current state: " << state_;
    SetError(PIPELINE_ERROR_ABORT);
    return;
  }

  // Decrement the number of remaining transitions, making sure to transition
  // to the next state if needed.
  set_state(FindNextState(state_));
  if (state_ == kSeeking) {
    base::AutoLock auto_lock(lock_);
    clock_->SetTime(seek_timestamp_);
  }

  // Carry out the action for the current state.
  if (TransientState(state_)) {
    if (state_ == kPausing) {
      pipeline_filter_->Pause(
          NewCallback(this, &PipelineImpl::OnFilterStateTransition));
    } else if (state_ == kFlushing) {
      pipeline_filter_->Flush(
          NewCallback(this, &PipelineImpl::OnFilterStateTransition));
    } else if (state_ == kSeeking) {
      pipeline_filter_->Seek(seek_timestamp_,
          NewCallback(this, &PipelineImpl::OnFilterStateTransition));
    } else if (state_ == kStarting) {
      pipeline_filter_->Play(
          NewCallback(this, &PipelineImpl::OnFilterStateTransition));
    } else if (state_ == kStopping) {
      pipeline_filter_->Stop(
          NewCallback(this, &PipelineImpl::OnFilterStateTransition));
    } else {
      NOTREACHED() << "Unexpected state: " << state_;
    }
  } else if (state_ == kStarted) {
    FinishInitialization();

    // Finally, reset our seeking timestamp back to zero.
    seek_timestamp_ = base::TimeDelta();
    seek_pending_ = false;

    // If a playback rate change was requested during a seek, do it now that
    // the seek has compelted.
    if (playback_rate_change_pending_) {
      playback_rate_change_pending_ = false;
      PlaybackRateChangedTask(pending_playback_rate_);
    }

    base::AutoLock auto_lock(lock_);
    // We use audio stream to update the clock. So if there is such a stream,
    // we pause the clock until we receive a valid timestamp.
    waiting_for_clock_update_ = has_audio_;
    if (!waiting_for_clock_update_)
      clock_->Play();

    if (IsPipelineStopPending()) {
      // We had a pending stop request need to be honored right now.
      TearDownPipeline();
    }
  } else {
    NOTREACHED() << "Unexpected state: " << state_;
  }
}

void PipelineImpl::TeardownStateTransitionTask() {
  DCHECK(IsPipelineTearingDown());
  switch (state_) {
    case kStopping:
      set_state(error_caused_teardown_ ? kError : kStopped);
      FinishDestroyingFiltersTask();
      break;
    case kPausing:
      set_state(kFlushing);
      pipeline_filter_->Flush(
          NewCallback(this, &PipelineImpl::OnTeardownStateTransition));
      break;
    case kFlushing:
      set_state(kStopping);
      pipeline_filter_->Stop(
          NewCallback(this, &PipelineImpl::OnTeardownStateTransition));
      break;

    case kCreated:
    case kError:
    case kInitDemuxer:
    case kInitAudioDecoder:
    case kInitAudioRenderer:
    case kInitVideoDecoder:
    case kInitVideoRenderer:
    case kSeeking:
    case kStarting:
    case kStopped:
    case kStarted:
    case kEnded:
      NOTREACHED() << "Unexpected state for teardown: " << state_;
      break;
    // default: intentionally left out to force new states to cause compiler
    // errors.
  };
}

void PipelineImpl::FinishDestroyingFiltersTask() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineStopped());

  // Clear filter references.
  audio_renderer_ = NULL;
  video_renderer_ = NULL;
  demuxer_ = NULL;

  pipeline_filter_ = NULL;

  if (error_caused_teardown_ && !IsPipelineOk() && error_callback_.get())
    error_callback_->Run(status_);

  if (stop_pending_) {
    stop_pending_ = false;
    ResetState();
    scoped_ptr<PipelineStatusCallback> stop_callback(stop_callback_.release());
    // Notify the client that stopping has finished.
    if (stop_callback.get()) {
      stop_callback->Run(status_);
    }
  }

  tearing_down_ = false;
  error_caused_teardown_ = false;
}

bool PipelineImpl::PrepareFilter(scoped_refptr<Filter> filter) {
  bool ret = pipeline_init_state_->composite_->AddFilter(filter.get());

  if (!ret) {
    SetError(PIPELINE_ERROR_INITIALIZATION_FAILED);
  }
  return ret;
}

void PipelineImpl::InitializeDemuxer() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineOk());

  filter_collection_->GetDemuxerFactory()->Build(url_,
      NewCallback(this, &PipelineImpl::OnDemuxerBuilt));
}

void PipelineImpl::OnDemuxerBuilt(PipelineStatus status, Demuxer* demuxer) {
  if (MessageLoop::current() != message_loop_) {
    message_loop_->PostTask(FROM_HERE,
                            NewRunnableMethod(this,
                                              &PipelineImpl::OnDemuxerBuilt,
                                              status,
                                              make_scoped_refptr(demuxer)));
    return;
  }

  if (status != PIPELINE_OK) {
    SetError(status);
    return;
  }

  if (!demuxer) {
    SetError(PIPELINE_ERROR_REQUIRED_FILTER_MISSING);
    return;
  }

  if (!PrepareFilter(demuxer))
    return;

  demuxer_ = demuxer;
  OnFilterInitialize();
}

bool PipelineImpl::InitializeAudioDecoder(
    const scoped_refptr<Demuxer>& demuxer) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineOk());

  scoped_refptr<DemuxerStream> stream =
      demuxer->GetStream(DemuxerStream::AUDIO);

  if (!stream)
    return false;

  scoped_refptr<AudioDecoder> audio_decoder;
  filter_collection_->SelectAudioDecoder(&audio_decoder);

  if (!audio_decoder) {
    SetError(PIPELINE_ERROR_REQUIRED_FILTER_MISSING);
    return false;
  }

  if (!PrepareFilter(audio_decoder))
    return false;

  pipeline_init_state_->audio_decoder_ = audio_decoder;
  audio_decoder->Initialize(
      stream,
      NewCallback(this, &PipelineImpl::OnFilterInitialize),
      NewCallback(this, &PipelineImpl::OnUpdateStatistics));
  return true;
}

bool PipelineImpl::InitializeVideoDecoder(
    const scoped_refptr<Demuxer>& demuxer) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineOk());

  scoped_refptr<DemuxerStream> stream;

  if (demuxer) {
    stream = demuxer->GetStream(DemuxerStream::VIDEO);

    if (!stream)
      return false;
  }

  scoped_refptr<VideoDecoder> video_decoder;
  filter_collection_->SelectVideoDecoder(&video_decoder);

  if (!video_decoder) {
    SetError(PIPELINE_ERROR_REQUIRED_FILTER_MISSING);
    return false;
  }

  if (!PrepareFilter(video_decoder))
    return false;

  pipeline_init_state_->video_decoder_ = video_decoder;
  video_decoder->Initialize(
      stream,
      NewCallback(this, &PipelineImpl::OnFilterInitialize),
      NewCallback(this, &PipelineImpl::OnUpdateStatistics));
  return true;
}

bool PipelineImpl::InitializeAudioRenderer(
    const scoped_refptr<AudioDecoder>& decoder) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineOk());

  if (!decoder)
    return false;

  filter_collection_->SelectAudioRenderer(&audio_renderer_);
  if (!audio_renderer_) {
    SetError(PIPELINE_ERROR_REQUIRED_FILTER_MISSING);
    return false;
  }

  if (!PrepareFilter(audio_renderer_))
    return false;

  audio_renderer_->Initialize(
      decoder, NewCallback(this, &PipelineImpl::OnFilterInitialize));
  return true;
}

bool PipelineImpl::InitializeVideoRenderer(
    const scoped_refptr<VideoDecoder>& decoder) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineOk());

  if (!decoder)
    return false;

  filter_collection_->SelectVideoRenderer(&video_renderer_);
  if (!video_renderer_) {
    SetError(PIPELINE_ERROR_REQUIRED_FILTER_MISSING);
    return false;
  }

  if (!PrepareFilter(video_renderer_))
    return false;

  video_renderer_->Initialize(
      decoder,
      NewCallback(this, &PipelineImpl::OnFilterInitialize),
      NewCallback(this, &PipelineImpl::OnUpdateStatistics));
  return true;
}

void PipelineImpl::TearDownPipeline() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK_NE(kStopped, state_);

  DCHECK(!tearing_down_ ||  // Teardown on Stop().
         (tearing_down_ && error_caused_teardown_) ||  // Teardown on error.
         (tearing_down_ && stop_pending_));  // Stop during teardown by error.

  // Mark that we already start tearing down operation.
  tearing_down_ = true;

  switch (state_) {
    case kCreated:
    case kError:
      set_state(kStopped);
      // Need to put this in the message loop to make sure that it comes
      // after any pending callback tasks that are already queued.
      message_loop_->PostTask(FROM_HERE,
          NewRunnableMethod(this, &PipelineImpl::FinishDestroyingFiltersTask));
      break;

    case kInitDemuxer:
    case kInitAudioDecoder:
    case kInitAudioRenderer:
    case kInitVideoDecoder:
    case kInitVideoRenderer:
      // Make it look like initialization was successful.
      pipeline_filter_ = pipeline_init_state_->composite_;
      pipeline_init_state_.reset();
      filter_collection_.reset();

      set_state(kStopping);
      pipeline_filter_->Stop(
          NewCallback(this, &PipelineImpl::OnTeardownStateTransition));

      FinishInitialization();
      break;

    case kPausing:
    case kSeeking:
    case kFlushing:
    case kStarting:
      set_state(kStopping);
      pipeline_filter_->Stop(
          NewCallback(this, &PipelineImpl::OnTeardownStateTransition));

      if (seek_pending_) {
        seek_pending_ = false;
        FinishInitialization();
      }

      break;

    case kStarted:
    case kEnded:
      set_state(kPausing);
      pipeline_filter_->Pause(
          NewCallback(this, &PipelineImpl::OnTeardownStateTransition));
      break;

    case kStopping:
    case kStopped:
      NOTREACHED() << "Unexpected state for teardown: " << state_;
      break;
    // default: intentionally left out to force new states to cause compiler
    // errors.
  };
}

}  // namespace media
