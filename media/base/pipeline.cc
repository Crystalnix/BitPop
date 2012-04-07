// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/pipeline.h"

#include <algorithm>

#include "base/bind.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/metrics/histogram.h"
#include "base/message_loop.h"
#include "base/stl_util.h"
#include "base/string_util.h"
#include "base/synchronization/condition_variable.h"
#include "media/base/clock.h"
#include "media/base/composite_filter.h"
#include "media/base/filter_collection.h"
#include "media/base/media_log.h"

namespace media {

PipelineStatusNotification::PipelineStatusNotification()
    : cv_(&lock_), status_(PIPELINE_OK), notified_(false) {
}

PipelineStatusNotification::~PipelineStatusNotification() {
  DCHECK(notified_);
}

PipelineStatusCB PipelineStatusNotification::Callback() {
  return base::Bind(&PipelineStatusNotification::Notify,
                    base::Unretained(this));
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

class Pipeline::PipelineInitState {
 public:
  scoped_refptr<AudioDecoder> audio_decoder_;
  scoped_refptr<VideoDecoder> video_decoder_;
  scoped_refptr<CompositeFilter> composite_;
};

Pipeline::Pipeline(MessageLoop* message_loop, MediaLog* media_log)
    : message_loop_(message_loop),
      media_log_(media_log),
      clock_(new Clock(&base::Time::Now)),
      waiting_for_clock_update_(false),
      state_(kCreated),
      current_bytes_(0),
      creation_time_(base::Time::Now()),
      is_downloading_data_(false) {
  media_log_->AddEvent(media_log_->CreatePipelineStateChangedEvent(kCreated));
  ResetState();
  media_log_->AddEvent(
      media_log_->CreateEvent(MediaLogEvent::PIPELINE_CREATED));
}

Pipeline::~Pipeline() {
  base::AutoLock auto_lock(lock_);
  DCHECK(!running_) << "Stop() must complete before destroying object";
  DCHECK(!stop_pending_);
  DCHECK(!seek_pending_);

  media_log_->AddEvent(
      media_log_->CreateEvent(MediaLogEvent::PIPELINE_DESTROYED));
}

void Pipeline::Start(scoped_ptr<FilterCollection> collection,
                     const std::string& url,
                     const PipelineStatusCB& ended_callback,
                     const PipelineStatusCB& error_callback,
                     const NetworkEventCB& network_callback,
                     const PipelineStatusCB& start_callback) {
  base::AutoLock auto_lock(lock_);
  CHECK(!running_) << "Media pipeline is already running";

  running_ = true;
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &Pipeline::StartTask, this, base::Passed(&collection),
      url, ended_callback, error_callback, network_callback, start_callback));
}

void Pipeline::Stop(const PipelineStatusCB& stop_callback) {
  base::AutoLock auto_lock(lock_);
  CHECK(running_) << "Media pipeline isn't running";

  if (video_decoder_) {
    video_decoder_->PrepareForShutdownHack();
    video_decoder_ = NULL;
  }

  // Stop the pipeline, which will set |running_| to false on our behalf.
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &Pipeline::StopTask, this, stop_callback));
}

void Pipeline::Seek(base::TimeDelta time,
                    const PipelineStatusCB& seek_callback) {
  base::AutoLock auto_lock(lock_);
  CHECK(running_) << "Media pipeline isn't running";

  download_rate_monitor_.Stop();

  message_loop_->PostTask(FROM_HERE, base::Bind(
      &Pipeline::SeekTask, this, time, seek_callback));
}

bool Pipeline::IsRunning() const {
  base::AutoLock auto_lock(lock_);
  return running_;
}

bool Pipeline::IsInitialized() const {
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

bool Pipeline::HasAudio() const {
  base::AutoLock auto_lock(lock_);
  return has_audio_;
}

bool Pipeline::HasVideo() const {
  base::AutoLock auto_lock(lock_);
  return has_video_;
}

float Pipeline::GetPlaybackRate() const {
  base::AutoLock auto_lock(lock_);
  return playback_rate_;
}

void Pipeline::SetPlaybackRate(float playback_rate) {
  if (playback_rate < 0.0f)
    return;

  base::AutoLock auto_lock(lock_);
  playback_rate_ = playback_rate;
  if (running_ && !tearing_down_) {
    message_loop_->PostTask(FROM_HERE, base::Bind(
        &Pipeline::PlaybackRateChangedTask, this, playback_rate));
  }
}

float Pipeline::GetVolume() const {
  base::AutoLock auto_lock(lock_);
  return volume_;
}

void Pipeline::SetVolume(float volume) {
  if (volume < 0.0f || volume > 1.0f)
    return;

  base::AutoLock auto_lock(lock_);
  volume_ = volume;
  if (running_ && !tearing_down_) {
    message_loop_->PostTask(FROM_HERE, base::Bind(
        &Pipeline::VolumeChangedTask, this, volume));
  }
}

Preload Pipeline::GetPreload() const {
  base::AutoLock auto_lock(lock_);
  return preload_;
}

void Pipeline::SetPreload(Preload preload) {
  base::AutoLock auto_lock(lock_);
  preload_ = preload;
  if (running_ && !tearing_down_) {
    message_loop_->PostTask(FROM_HERE, base::Bind(
        &Pipeline::PreloadChangedTask, this, preload));
  }
}

base::TimeDelta Pipeline::GetCurrentTime() const {
  base::AutoLock auto_lock(lock_);
  return GetCurrentTime_Locked();
}

base::TimeDelta Pipeline::GetCurrentTime_Locked() const {
  lock_.AssertAcquired();
  base::TimeDelta elapsed = clock_->Elapsed();
  if (elapsed > duration_)
    return duration_;

  return elapsed;
}

base::TimeDelta Pipeline::GetBufferedTime() {
  base::AutoLock auto_lock(lock_);

  // If media is fully loaded, then return duration.
  if (local_source_ || total_bytes_ == buffered_bytes_) {
    max_buffered_time_ = duration_;
    return duration_;
  }

  base::TimeDelta current_time = GetCurrentTime_Locked();

  // If buffered time was set, we report that value directly.
  if (buffered_time_.ToInternalValue() > 0)
    return std::max(buffered_time_, current_time);

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

base::TimeDelta Pipeline::GetMediaDuration() const {
  base::AutoLock auto_lock(lock_);
  return duration_;
}

int64 Pipeline::GetBufferedBytes() const {
  base::AutoLock auto_lock(lock_);
  return buffered_bytes_;
}

int64 Pipeline::GetTotalBytes() const {
  base::AutoLock auto_lock(lock_);
  return total_bytes_;
}

void Pipeline::GetNaturalVideoSize(gfx::Size* out_size) const {
  CHECK(out_size);
  base::AutoLock auto_lock(lock_);
  *out_size = natural_size_;
}

bool Pipeline::IsStreaming() const {
  base::AutoLock auto_lock(lock_);
  return streaming_;
}

bool Pipeline::IsLocalSource() const {
  base::AutoLock auto_lock(lock_);
  return local_source_;
}

PipelineStatistics Pipeline::GetStatistics() const {
  base::AutoLock auto_lock(lock_);
  return statistics_;
}

void Pipeline::SetClockForTesting(Clock* clock) {
  clock_.reset(clock);
}

void Pipeline::SetCurrentReadPosition(int64 offset) {
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

void Pipeline::ResetState() {
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
  local_source_     = false;
  total_bytes_      = 0;
  natural_size_.SetSize(0, 0);
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
  download_rate_monitor_.Reset();
}

void Pipeline::SetState(State next_state) {
  if (state_ != kStarted && next_state == kStarted &&
      !creation_time_.is_null()) {
    UMA_HISTOGRAM_TIMES(
        "Media.TimeToPipelineStarted", base::Time::Now() - creation_time_);
    creation_time_ = base::Time();
  }
  state_ = next_state;
  media_log_->AddEvent(media_log_->CreatePipelineStateChangedEvent(next_state));
}

bool Pipeline::IsPipelineOk() {
  base::AutoLock auto_lock(lock_);
  return status_ == PIPELINE_OK;
}

bool Pipeline::IsPipelineStopped() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  return state_ == kStopped || state_ == kError;
}

bool Pipeline::IsPipelineTearingDown() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  return tearing_down_;
}

bool Pipeline::IsPipelineStopPending() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  return stop_pending_;
}

bool Pipeline::IsPipelineSeeking() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  if (!seek_pending_)
    return false;
  DCHECK(kSeeking == state_ || kPausing == state_ ||
         kFlushing == state_ || kStarting == state_)
      << "Current state : " << state_;
  return true;
}

void Pipeline::FinishInitialization() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  // Execute the seek callback, if present.  Note that this might be the
  // initial callback passed into Start().
  if (!seek_callback_.is_null()) {
    seek_callback_.Run(status_);
    seek_callback_.Reset();
  }
}

// static
bool Pipeline::TransientState(State state) {
  return state == kPausing ||
         state == kFlushing ||
         state == kSeeking ||
         state == kStarting ||
         state == kStopping;
}

// static
Pipeline::State Pipeline::FindNextState(State current) {
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

void Pipeline::OnDemuxerError(PipelineStatus error) {
  SetError(error);
}

void Pipeline::SetError(PipelineStatus error) {
  DCHECK(IsRunning());
  DCHECK_NE(PIPELINE_OK, error);
  VLOG(1) << "Media pipeline error: " << error;

  message_loop_->PostTask(FROM_HERE, base::Bind(
      &Pipeline::ErrorChangedTask, this, error));

  media_log_->AddEvent(media_log_->CreatePipelineErrorEvent(error));
}

base::TimeDelta Pipeline::GetTime() const {
  DCHECK(IsRunning());
  return GetCurrentTime();
}

base::TimeDelta Pipeline::GetDuration() const {
  DCHECK(IsRunning());
  return GetMediaDuration();
}

void Pipeline::SetTime(base::TimeDelta time) {
  DCHECK(IsRunning());
  base::AutoLock auto_lock(lock_);

  // If we were waiting for a valid timestamp and such timestamp arrives, we
  // need to clear the flag for waiting and start the clock.
  if (waiting_for_clock_update_) {
    if (time < clock_->Elapsed())
      return;
    clock_->SetTime(time);
    StartClockIfWaitingForTimeUpdate_Locked();
    return;
  }
  clock_->SetTime(time);
}

void Pipeline::SetDuration(base::TimeDelta duration) {
  DCHECK(IsRunning());
  media_log_->AddEvent(
      media_log_->CreateTimeEvent(
          MediaLogEvent::DURATION_SET, "duration", duration));
  UMA_HISTOGRAM_LONG_TIMES("Media.Duration", duration);

  base::AutoLock auto_lock(lock_);
  duration_ = duration;
}

void Pipeline::SetBufferedTime(base::TimeDelta buffered_time) {
  DCHECK(IsRunning());
  base::AutoLock auto_lock(lock_);
  buffered_time_ = buffered_time;
}

void Pipeline::SetTotalBytes(int64 total_bytes) {
  DCHECK(IsRunning());
  media_log_->AddEvent(
      media_log_->CreateIntegerEvent(
          MediaLogEvent::TOTAL_BYTES_SET, "total_bytes", total_bytes));
  int64 total_mbytes = total_bytes >> 20;
  if (total_mbytes > kint32max)
    total_mbytes = kint32max;
  UMA_HISTOGRAM_CUSTOM_COUNTS(
      "Media.TotalMBytes", static_cast<int32>(total_mbytes), 1, kint32max, 50);

  base::AutoLock auto_lock(lock_);
  total_bytes_ = total_bytes;
  download_rate_monitor_.set_total_bytes(total_bytes_);
}

void Pipeline::SetBufferedBytes(int64 buffered_bytes) {
  DCHECK(IsRunning());
  base::AutoLock auto_lock(lock_);
  // See comments in SetCurrentReadPosition() about capping.
  if (buffered_bytes < current_bytes_)
    current_bytes_ = buffered_bytes;
  buffered_bytes_ = buffered_bytes;
  download_rate_monitor_.SetBufferedBytes(buffered_bytes, base::Time::Now());
}

void Pipeline::SetNaturalVideoSize(const gfx::Size& size) {
  DCHECK(IsRunning());
  media_log_->AddEvent(media_log_->CreateVideoSizeSetEvent(
      size.width(), size.height()));

  base::AutoLock auto_lock(lock_);
  natural_size_ = size;
}

void Pipeline::NotifyEnded() {
  DCHECK(IsRunning());
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &Pipeline::NotifyEndedTask, this));
  media_log_->AddEvent(media_log_->CreateEvent(MediaLogEvent::ENDED));
}

void Pipeline::SetNetworkActivity(bool is_downloading_data) {
  DCHECK(IsRunning());

  NetworkEvent type = DOWNLOAD_PAUSED;
  if (is_downloading_data)
    type = DOWNLOAD_CONTINUED;

  {
    base::AutoLock auto_lock(lock_);
    download_rate_monitor_.SetNetworkActivity(is_downloading_data);
  }

  message_loop_->PostTask(FROM_HERE, base::Bind(
      &Pipeline::NotifyNetworkEventTask, this, type));
  media_log_->AddEvent(
      media_log_->CreateBooleanEvent(
          MediaLogEvent::NETWORK_ACTIVITY_SET,
          "is_downloading_data", is_downloading_data));
}

void Pipeline::DisableAudioRenderer() {
  DCHECK(IsRunning());

  // Disable renderer on the message loop.
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &Pipeline::DisableAudioRendererTask, this));
  media_log_->AddEvent(
      media_log_->CreateEvent(MediaLogEvent::AUDIO_RENDERER_DISABLED));
}

// Called from any thread.
void Pipeline::OnFilterInitialize(PipelineStatus status) {
  // Continue the initialize task by proceeding to the next stage.
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &Pipeline::InitializeTask, this, status));
}

// Called from any thread.
void Pipeline::OnFilterStateTransition() {
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &Pipeline::FilterStateTransitionTask, this));
}

// Called from any thread.
// This method makes the FilterStatusCB behave like a Closure. It
// makes it look like a host()->SetError() call followed by a call to
// OnFilterStateTransition() when errors occur.
//
// TODO: Revisit this code when SetError() is removed from FilterHost and
//       all the Closures are converted to FilterStatusCB.
void Pipeline::OnFilterStateTransitionWithStatus(PipelineStatus status) {
  if (status != PIPELINE_OK)
    SetError(status);
  OnFilterStateTransition();
}

void Pipeline::OnTeardownStateTransition() {
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &Pipeline::TeardownStateTransitionTask, this));
}

// Called from any thread.
void Pipeline::OnUpdateStatistics(const PipelineStatistics& stats) {
  base::AutoLock auto_lock(lock_);
  statistics_.audio_bytes_decoded += stats.audio_bytes_decoded;
  statistics_.video_bytes_decoded += stats.video_bytes_decoded;
  statistics_.video_frames_decoded += stats.video_frames_decoded;
  statistics_.video_frames_dropped += stats.video_frames_dropped;
  media_log_->QueueStatisticsUpdatedEvent(statistics_);
}

void Pipeline::StartTask(scoped_ptr<FilterCollection> filter_collection,
                         const std::string& url,
                         const PipelineStatusCB& ended_callback,
                         const PipelineStatusCB& error_callback,
                         const NetworkEventCB& network_callback,
                         const PipelineStatusCB& start_callback) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK_EQ(kCreated, state_);
  filter_collection_ = filter_collection.Pass();
  url_ = url;
  ended_callback_ = ended_callback;
  error_callback_ = error_callback;
  network_callback_ = network_callback;
  seek_callback_ = start_callback;

  // Kick off initialization.
  pipeline_init_state_.reset(new PipelineInitState());
  pipeline_init_state_->composite_ = new CompositeFilter(message_loop_);
  pipeline_init_state_->composite_->set_host(this);

  SetState(kInitDemuxer);
  InitializeDemuxer();
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
void Pipeline::InitializeTask(PipelineStatus last_stage_status) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  if (last_stage_status != PIPELINE_OK) {
    // Currently only VideoDecoders have a recoverable error code.
    if (state_ == kInitVideoDecoder &&
        last_stage_status == DECODER_ERROR_NOT_SUPPORTED) {
      pipeline_init_state_->composite_->RemoveFilter(
          pipeline_init_state_->video_decoder_.get());
      state_ = kInitAudioRenderer;
    } else {
      SetError(last_stage_status);
    }
  }

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
    SetState(kInitAudioDecoder);
    // If this method returns false, then there's no audio stream.
    if (InitializeAudioDecoder(demuxer_))
      return;
  }

  // Assuming audio decoder was created, create audio renderer.
  if (state_ == kInitAudioDecoder) {
    SetState(kInitAudioRenderer);

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
    SetState(kInitVideoDecoder);
    if (InitializeVideoDecoder(demuxer_))
      return;
  }

  // Assuming video decoder was created, create video renderer.
  if (state_ == kInitVideoDecoder) {
    SetState(kInitVideoRenderer);
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
      demuxer_->OnAudioRendererDisabled();
      pipeline_filter_->OnAudioRendererDisabled();
    }

    // Initialization was successful, we are now considered paused, so it's safe
    // to set the initial playback rate and volume.
    PreloadChangedTask(GetPreload());
    PlaybackRateChangedTask(GetPlaybackRate());
    VolumeChangedTask(GetVolume());

    // Fire the seek request to get the filters to preroll.
    seek_pending_ = true;
    SetState(kSeeking);
    seek_timestamp_ = demuxer_->GetStartTime();
    DoSeek(seek_timestamp_);
  }
}

// This method is called as a result of the client calling Pipeline::Stop() or
// as the result of an error condition.
// We stop the filters in the reverse order.
//
// TODO(scherkus): beware!  this can get posted multiple times since we post
// Stop() tasks even if we've already stopped.  Perhaps this should no-op for
// additional calls, however most of this logic will be changing.
void Pipeline::StopTask(const PipelineStatusCB& stop_callback) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(!IsPipelineStopPending());
  DCHECK_NE(state_, kStopped);

  if (state_ == kStopped) {
    // Already stopped so just run callback.
    stop_callback.Run(status_);
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

  stop_callback_ = stop_callback;

  stop_pending_ = true;
  if (!IsPipelineSeeking() && !IsPipelineTearingDown()) {
    // We will tear down pipeline immediately when there is no seek operation
    // pending and no teardown in progress. This should include the case where
    // we are partially initialized.
    TearDownPipeline();
  }
}

void Pipeline::ErrorChangedTask(PipelineStatus error) {
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
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &Pipeline::TearDownPipeline, this));
}

void Pipeline::PlaybackRateChangedTask(float playback_rate) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  if (!running_ || tearing_down_)
    return;

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
    DCHECK(demuxer_);
    demuxer_->SetPlaybackRate(playback_rate);
    pipeline_filter_->SetPlaybackRate(playback_rate);
  }
}

void Pipeline::VolumeChangedTask(float volume) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  if (!running_ || tearing_down_)
    return;

  if (audio_renderer_)
    audio_renderer_->SetVolume(volume);
}

void Pipeline::PreloadChangedTask(Preload preload) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  if (!running_ || tearing_down_)
    return;

  if (demuxer_)
    demuxer_->SetPreload(preload);
}

void Pipeline::SeekTask(base::TimeDelta time,
                        const PipelineStatusCB& seek_callback) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(!IsPipelineStopPending());

  // Suppress seeking if we're not fully started.
  if (state_ != kStarted && state_ != kEnded) {
    // TODO(scherkus): should we run the callback?  I'm tempted to say the API
    // will only execute the first Seek() request.
    VLOG(1) << "Media pipeline has not started, ignoring seek to "
            << time.InMicroseconds();
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
  SetState(kPausing);
  seek_timestamp_ = time;
  seek_callback_ = seek_callback;

  // Kick off seeking!
  {
    base::AutoLock auto_lock(lock_);
    if (clock_->IsPlaying())
      clock_->Pause();
  }
  pipeline_filter_->Pause(
      base::Bind(&Pipeline::OnFilterStateTransition, this));
}

void Pipeline::NotifyEndedTask() {
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

    // Start clock since there is no more audio to
    // trigger clock updates.
    base::AutoLock auto_lock(lock_);
    StartClockIfWaitingForTimeUpdate_Locked();
  }

  if (video_renderer_ && !video_renderer_->HasEnded()) {
    return;
  }

  // Transition to ended, executing the callback if present.
  SetState(kEnded);
  {
    base::AutoLock auto_lock(lock_);
    clock_->Pause();
    clock_->SetTime(duration_);
  }

  if (!ended_callback_.is_null()) {
    ended_callback_.Run(status_);
  }
}

void Pipeline::NotifyNetworkEventTask(NetworkEvent type) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  if (!network_callback_.is_null())
    network_callback_.Run(type);
}

void Pipeline::DisableAudioRendererTask() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  base::AutoLock auto_lock(lock_);
  has_audio_ = false;
  audio_disabled_ = true;

  // Notify all filters of disabled audio renderer. If the filter isn't
  // initialized yet, OnAudioRendererDisabled() will be called when
  // initialization is complete.
  if (pipeline_filter_) {
    DCHECK(demuxer_);
    demuxer_->OnAudioRendererDisabled();
    pipeline_filter_->OnAudioRendererDisabled();
  }

  // Start clock since there is no more audio to
  // trigger clock updates.
  StartClockIfWaitingForTimeUpdate_Locked();
}

void Pipeline::FilterStateTransitionTask() {
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
  SetState(FindNextState(state_));
  if (state_ == kSeeking) {
    base::AutoLock auto_lock(lock_);
    clock_->SetTime(seek_timestamp_);
  }

  // Carry out the action for the current state.
  if (TransientState(state_)) {
    if (state_ == kPausing) {
      pipeline_filter_->Pause(
          base::Bind(&Pipeline::OnFilterStateTransition, this));
    } else if (state_ == kFlushing) {
      pipeline_filter_->Flush(
          base::Bind(&Pipeline::OnFilterStateTransition, this));
    } else if (state_ == kSeeking) {
      DoSeek(seek_timestamp_);
    } else if (state_ == kStarting) {
      pipeline_filter_->Play(
          base::Bind(&Pipeline::OnFilterStateTransition, this));
    } else if (state_ == kStopping) {
      DoStop(base::Bind(&Pipeline::OnFilterStateTransition, this));
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
    waiting_for_clock_update_ = true;
    if (!has_audio_)
      StartClockIfWaitingForTimeUpdate_Locked();

    // Start monitoring rate of downloading.
    int bitrate = 0;
    if (demuxer_.get()) {
      bitrate = demuxer_->GetBitrate();
      local_source_ = demuxer_->IsLocalSource();
      streaming_ = !demuxer_->IsSeekable();
    }
    // Needs to be locked because most other calls to |download_rate_monitor_|
    // occur on the renderer thread.
    download_rate_monitor_.Start(
        base::Bind(&Pipeline::OnCanPlayThrough, this),
        bitrate, streaming_, local_source_);
    download_rate_monitor_.SetBufferedBytes(buffered_bytes_, base::Time::Now());

    if (IsPipelineStopPending()) {
      // We had a pending stop request need to be honored right now.
      TearDownPipeline();
    }
  } else {
    NOTREACHED() << "Unexpected state: " << state_;
  }
}

void Pipeline::TeardownStateTransitionTask() {
  DCHECK(IsPipelineTearingDown());
  switch (state_) {
    case kStopping:
      SetState(error_caused_teardown_ ? kError : kStopped);
      FinishDestroyingFiltersTask();
      break;
    case kPausing:
      SetState(kFlushing);
      pipeline_filter_->Flush(
          base::Bind(&Pipeline::OnTeardownStateTransition, this));
      break;
    case kFlushing:
      SetState(kStopping);
      DoStop(base::Bind(&Pipeline::OnTeardownStateTransition, this));
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

void Pipeline::FinishDestroyingFiltersTask() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineStopped());

  // Clear filter references.
  audio_renderer_ = NULL;
  video_renderer_ = NULL;
  demuxer_ = NULL;

  pipeline_filter_ = NULL;

  if (error_caused_teardown_ && !IsPipelineOk() && !error_callback_.is_null())
    error_callback_.Run(status_);

  if (stop_pending_) {
    stop_pending_ = false;
    ResetState();
    PipelineStatusCB stop_cb;
    std::swap(stop_cb, stop_callback_);
    // Notify the client that stopping has finished.
    if (!stop_cb.is_null()) {
      stop_cb.Run(status_);
    }
  }

  tearing_down_ = false;
  error_caused_teardown_ = false;
}

bool Pipeline::PrepareFilter(scoped_refptr<Filter> filter) {
  bool ret = pipeline_init_state_->composite_->AddFilter(filter.get());
  if (!ret)
    SetError(PIPELINE_ERROR_INITIALIZATION_FAILED);
  return ret;
}

void Pipeline::InitializeDemuxer() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineOk());

  filter_collection_->GetDemuxerFactory()->Build(
      url_, base::Bind(&Pipeline::OnDemuxerBuilt, this));
}

void Pipeline::OnDemuxerBuilt(PipelineStatus status, Demuxer* demuxer) {
  if (MessageLoop::current() != message_loop_) {
    message_loop_->PostTask(FROM_HERE, base::Bind(
        &Pipeline::OnDemuxerBuilt, this, status, make_scoped_refptr(demuxer)));
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

  demuxer_ = demuxer;
  demuxer_->set_host(this);

  {
    base::AutoLock auto_lock(lock_);
    // We do not want to start the clock running. We only want to set the base
    // media time so our timestamp calculations will be correct.
    clock_->SetTime(demuxer_->GetStartTime());
  }

  OnFilterInitialize(PIPELINE_OK);
}

bool Pipeline::InitializeAudioDecoder(
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
      base::Bind(&Pipeline::OnFilterInitialize, this, PIPELINE_OK),
      base::Bind(&Pipeline::OnUpdateStatistics, this));
  return true;
}

bool Pipeline::InitializeVideoDecoder(
    const scoped_refptr<Demuxer>& demuxer) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineOk());

  scoped_refptr<DemuxerStream> stream;

  if (demuxer) {
    stream = demuxer->GetStream(DemuxerStream::VIDEO);

    if (!stream)
      return false;
  }

  filter_collection_->SelectVideoDecoder(&video_decoder_);

  if (!video_decoder_) {
    SetError(PIPELINE_ERROR_REQUIRED_FILTER_MISSING);
    return false;
  }

  if (!PrepareFilter(video_decoder_))
    return false;

  pipeline_init_state_->video_decoder_ = video_decoder_;
  video_decoder_->Initialize(
      stream,
      base::Bind(&Pipeline::OnFilterInitialize, this),
      base::Bind(&Pipeline::OnUpdateStatistics, this));
  return true;
}

bool Pipeline::InitializeAudioRenderer(
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
      decoder,
      base::Bind(&Pipeline::OnFilterInitialize, this, PIPELINE_OK),
      base::Bind(&Pipeline::OnAudioUnderflow, this));
  return true;
}

bool Pipeline::InitializeVideoRenderer(
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
      base::Bind(&Pipeline::OnFilterInitialize, this, PIPELINE_OK),
      base::Bind(&Pipeline::OnUpdateStatistics, this));
  return true;
}

void Pipeline::TearDownPipeline() {
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
      SetState(kStopped);
      // Need to put this in the message loop to make sure that it comes
      // after any pending callback tasks that are already queued.
      message_loop_->PostTask(FROM_HERE, base::Bind(
          &Pipeline::FinishDestroyingFiltersTask, this));
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

      SetState(kStopping);
      DoStop(base::Bind(&Pipeline::OnTeardownStateTransition, this));

      FinishInitialization();
      break;

    case kPausing:
    case kSeeking:
    case kFlushing:
    case kStarting:
      SetState(kStopping);
      DoStop(base::Bind(&Pipeline::OnTeardownStateTransition, this));

      if (seek_pending_) {
        seek_pending_ = false;
        FinishInitialization();
      }

      break;

    case kStarted:
    case kEnded:
      SetState(kPausing);
      pipeline_filter_->Pause(
          base::Bind(&Pipeline::OnTeardownStateTransition, this));
      break;

    case kStopping:
    case kStopped:
      NOTREACHED() << "Unexpected state for teardown: " << state_;
      break;
    // default: intentionally left out to force new states to cause compiler
    // errors.
  };
}

void Pipeline::DoStop(const base::Closure& callback) {
  if (demuxer_) {
    demuxer_->Stop(base::Bind(
        &Pipeline::OnDemuxerStopDone, this, callback));
    return;
  }

  OnDemuxerStopDone(callback);
}

void Pipeline::OnDemuxerStopDone(const base::Closure& callback) {
  if (MessageLoop::current() != message_loop_) {
    message_loop_->PostTask(FROM_HERE, base::Bind(
        &Pipeline::OnDemuxerStopDone, this, callback));
    return;
  }

  if (pipeline_filter_) {
    pipeline_filter_->Stop(callback);
    return;
  }

  callback.Run();

}

void Pipeline::DoSeek(base::TimeDelta seek_timestamp) {
  // TODO(acolwell) : We might be able to convert this if (demuxer_) into a
  // DCHECK(). Further investigation is needed to make sure this won't introduce
  // a bug.
  if (demuxer_) {
    demuxer_->Seek(seek_timestamp, base::Bind(
        &Pipeline::OnDemuxerSeekDone, this, seek_timestamp));
    return;
  }

  OnDemuxerSeekDone(seek_timestamp, PIPELINE_OK);
}

void Pipeline::OnDemuxerSeekDone(base::TimeDelta seek_timestamp,
                                 PipelineStatus status) {
  if (MessageLoop::current() != message_loop_) {
    message_loop_->PostTask(FROM_HERE, base::Bind(
        &Pipeline::OnDemuxerSeekDone, this, seek_timestamp, status));
    return;
  }

  PipelineStatusCB done_cb =
      base::Bind(&Pipeline::OnFilterStateTransitionWithStatus, this);

  if (status == PIPELINE_OK && pipeline_filter_) {
    pipeline_filter_->Seek(seek_timestamp, done_cb);
    return;
  }

  done_cb.Run(status);
}

void Pipeline::OnAudioUnderflow() {
  if (MessageLoop::current() != message_loop_) {
    message_loop_->PostTask(FROM_HERE, base::Bind(
        &Pipeline::OnAudioUnderflow, this));
    return;
  }

  if (state_ != kStarted)
    return;

  if (audio_renderer_)
    audio_renderer_->ResumeAfterUnderflow(true);
}

void Pipeline::OnCanPlayThrough() {
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &Pipeline::NotifyCanPlayThrough, this));
}

void Pipeline::NotifyCanPlayThrough() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  NotifyNetworkEventTask(CAN_PLAY_THROUGH);
}

void Pipeline::StartClockIfWaitingForTimeUpdate_Locked() {
  lock_.AssertAcquired();
  if (!waiting_for_clock_update_)
    return;

  waiting_for_clock_update_ = false;
  clock_->Play();
}

}  // namespace media
