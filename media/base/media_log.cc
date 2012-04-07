// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/media_log.h"

#include <string>

#include "base/atomic_sequence_num.h"
#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/values.h"

namespace media {

// A count of all MediaLogs created on this render process.
// Used to generate unique ids.
static base::LazyInstance<base::AtomicSequenceNumber>::Leaky media_log_count =
    LAZY_INSTANCE_INITIALIZER;

const char* MediaLog::EventTypeToString(MediaLogEvent::Type type) {
  switch (type) {
    case MediaLogEvent::WEBMEDIAPLAYER_CREATED:
      return "WEBMEDIAPLAYER_CREATED";
    case MediaLogEvent::WEBMEDIAPLAYER_DESTROYED:
      return "WEBMEDIAPLAYER_DESTROYED";
    case MediaLogEvent::PIPELINE_CREATED:
      return "PIPELINE_CREATED";
    case MediaLogEvent::PIPELINE_DESTROYED:
      return "PIPELINE_DESTROYED";
    case MediaLogEvent::LOAD:
      return "LOAD";
    case MediaLogEvent::SEEK:
      return "SEEK";
    case MediaLogEvent::PLAY:
      return "PLAY";
    case MediaLogEvent::PAUSE:
      return "PAUSE";
    case MediaLogEvent::PIPELINE_STATE_CHANGED:
      return "PIPELINE_STATE_CHANGED";
    case MediaLogEvent::PIPELINE_ERROR:
      return "PIPELINE_ERROR";
    case MediaLogEvent::VIDEO_SIZE_SET:
      return "VIDEO_SIZE_SET";
    case MediaLogEvent::DURATION_SET:
      return "DURATION_SET";
    case MediaLogEvent::TOTAL_BYTES_SET:
      return "TOTAL_BYTES_SET";
    case MediaLogEvent::NETWORK_ACTIVITY_SET:
      return "NETWORK_ACTIVITY_SET";
    case MediaLogEvent::ENDED:
      return "ENDED";
    case MediaLogEvent::AUDIO_RENDERER_DISABLED:
      return "AUDIO_RENDERER_DISABLED";
    case MediaLogEvent::BUFFERED_EXTENTS_CHANGED:
      return "BUFFERED_EXTENTS_CHANGED";
    case MediaLogEvent::STATISTICS_UPDATED:
      return "STATISTICS_UPDATED";
  }
  NOTREACHED();
  return NULL;
}

const char* MediaLog::PipelineStateToString(Pipeline::State state) {
  switch (state) {
    case Pipeline::kCreated:
      return "created";
    case Pipeline::kInitDemuxer:
      return "initDemuxer";
    case Pipeline::kInitAudioDecoder:
      return "initAudioDecoder";
    case Pipeline::kInitAudioRenderer:
      return "initAudioRenderer";
    case Pipeline::kInitVideoDecoder:
      return "initVideoDecoder";
    case Pipeline::kInitVideoRenderer:
      return "initVideoRenderer";
    case Pipeline::kPausing:
      return "pausing";
    case Pipeline::kSeeking:
      return "seeking";
    case Pipeline::kFlushing:
      return "flushing";
    case Pipeline::kStarting:
      return "starting";
    case Pipeline::kStarted:
      return "started";
    case Pipeline::kEnded:
      return "ended";
    case Pipeline::kStopping:
      return "stopping";
    case Pipeline::kStopped:
      return "stopped";
    case Pipeline::kError:
      return "error";
  }
  NOTREACHED();
  return NULL;
}

const char* MediaLog::PipelineStatusToString(PipelineStatus status) {
  switch (status) {
    case PIPELINE_OK:
      return "pipeline: ok";
    case PIPELINE_ERROR_URL_NOT_FOUND:
      return "pipeline: url not found";
    case PIPELINE_ERROR_NETWORK:
      return "pipeline: network error";
    case PIPELINE_ERROR_DECODE:
      return "pipeline: decode error";
    case PIPELINE_ERROR_ABORT:
      return "pipeline: abort";
    case PIPELINE_ERROR_INITIALIZATION_FAILED:
      return "pipeline: initialization failed";
    case PIPELINE_ERROR_REQUIRED_FILTER_MISSING:
      return "pipeline: required filter missing";
    case PIPELINE_ERROR_OUT_OF_MEMORY:
      return "pipeline: out of memory";
    case PIPELINE_ERROR_COULD_NOT_RENDER:
      return "pipeline: could not render";
    case PIPELINE_ERROR_READ:
      return "pipeline: read error";
    case PIPELINE_ERROR_AUDIO_HARDWARE:
      return "pipeline: audio hardware error";
    case PIPELINE_ERROR_OPERATION_PENDING:
      return "pipeline: operation pending";
    case PIPELINE_ERROR_INVALID_STATE:
      return "pipeline: invalid state";
    case DEMUXER_ERROR_COULD_NOT_OPEN:
      return "demuxer: could not open";
    case DEMUXER_ERROR_COULD_NOT_PARSE:
      return "dumuxer: could not parse";
    case DEMUXER_ERROR_NO_SUPPORTED_STREAMS:
      return "demuxer: no supported streams";
    case DEMUXER_ERROR_COULD_NOT_CREATE_THREAD:
      return "demuxer: could not create thread";
    case DECODER_ERROR_NOT_SUPPORTED:
      return "decoder: not supported";
    case DATASOURCE_ERROR_URL_NOT_SUPPORTED:
      return "data source: url not supported";
  }
  NOTREACHED();
  return NULL;
}

MediaLog::MediaLog() {
  id_ = media_log_count.Get().GetNext();
  stats_update_pending_ = false;
}

MediaLog::~MediaLog() {}

void MediaLog::AddEvent(scoped_ptr<MediaLogEvent> event) {
}

scoped_ptr<MediaLogEvent> MediaLog::CreateEvent(MediaLogEvent::Type type) {
  scoped_ptr<MediaLogEvent> event(new MediaLogEvent);
  event->id = id_;
  event->type = type;
  event->time = base::Time::Now();
  return event.Pass();
}

scoped_ptr<MediaLogEvent> MediaLog::CreateBooleanEvent(
    MediaLogEvent::Type type, const char* property, bool value) {
  scoped_ptr<MediaLogEvent> event(CreateEvent(type));
  event->params.SetBoolean(property, value);
  return event.Pass();
}

scoped_ptr<MediaLogEvent> MediaLog::CreateIntegerEvent(
    MediaLogEvent::Type type, const char* property, int64 value) {
  scoped_ptr<MediaLogEvent> event(CreateEvent(type));
  event->params.SetInteger(property, value);
  return event.Pass();
}

scoped_ptr<MediaLogEvent> MediaLog::CreateTimeEvent(
    MediaLogEvent::Type type, const char* property, base::TimeDelta value) {
  scoped_ptr<MediaLogEvent> event(CreateEvent(type));
  event->params.SetDouble(property, value.InSecondsF());
  return event.Pass();
}

scoped_ptr<MediaLogEvent> MediaLog::CreateLoadEvent(const std::string& url) {
  scoped_ptr<MediaLogEvent> event(CreateEvent(MediaLogEvent::LOAD));
  event->params.SetString("url", url);
  return event.Pass();
}

scoped_ptr<MediaLogEvent> MediaLog::CreateSeekEvent(float seconds) {
  scoped_ptr<MediaLogEvent> event(CreateEvent(MediaLogEvent::SEEK));
  event->params.SetDouble("seek_target", seconds);
  return event.Pass();
}

scoped_ptr<MediaLogEvent> MediaLog::CreatePipelineStateChangedEvent(
    Pipeline::State state) {
  scoped_ptr<MediaLogEvent> event(
      CreateEvent(MediaLogEvent::PIPELINE_STATE_CHANGED));
  event->params.SetString("pipeline_state", PipelineStateToString(state));
  return event.Pass();
}

scoped_ptr<MediaLogEvent> MediaLog::CreatePipelineErrorEvent(
    PipelineStatus error) {
  scoped_ptr<MediaLogEvent> event(CreateEvent(MediaLogEvent::PIPELINE_ERROR));
  event->params.SetString("pipeline_error", PipelineStatusToString(error));
  return event.Pass();
}

scoped_ptr<MediaLogEvent> MediaLog::CreateVideoSizeSetEvent(
    size_t width, size_t height) {
  scoped_ptr<MediaLogEvent> event(CreateEvent(MediaLogEvent::VIDEO_SIZE_SET));
  event->params.SetInteger("width", width);
  event->params.SetInteger("height", height);
  return event.Pass();
}

scoped_ptr<MediaLogEvent> MediaLog::CreateBufferedExtentsChangedEvent(
    size_t start, size_t current, size_t end) {
  scoped_ptr<MediaLogEvent> event(
      CreateEvent(MediaLogEvent::BUFFERED_EXTENTS_CHANGED));
  event->params.SetInteger("buffer_start", start);
  event->params.SetInteger("buffer_current", current);
  event->params.SetInteger("buffer_end", end);
  return event.Pass();
}

void MediaLog::QueueStatisticsUpdatedEvent(PipelineStatistics stats) {
  base::AutoLock auto_lock(stats_lock_);
  last_statistics_ = stats;

  // Sadly, this function can get dispatched on threads not running a message
  // loop.  Happily, this is pretty rare (only VideoRendererBase at this time)
  // so we simply leave stats updating for another call to trigger.
  if (!stats_update_pending_ && MessageLoop::current()) {
    stats_update_pending_ = true;
    MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&media::MediaLog::AddStatisticsUpdatedEvent, this),
        base::TimeDelta::FromMilliseconds(500));
  }
}

void MediaLog::AddStatisticsUpdatedEvent() {
  base::AutoLock auto_lock(stats_lock_);
  scoped_ptr<MediaLogEvent> event(
      CreateEvent(MediaLogEvent::STATISTICS_UPDATED));
  event->params.SetInteger("audio_bytes_decoded",
                           last_statistics_.audio_bytes_decoded);
  event->params.SetInteger("video_bytes_decoded",
                           last_statistics_.video_bytes_decoded);
  event->params.SetInteger("video_frames_decoded",
                           last_statistics_.video_frames_decoded);
  event->params.SetInteger("video_frames_dropped",
                           last_statistics_.video_frames_dropped);
  AddEvent(event.Pass());
  stats_update_pending_ = false;
}

}  //namespace media
