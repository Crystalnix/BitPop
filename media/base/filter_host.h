// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// FilterHost describes an interface for individual filters to access and
// modify global playback information.  Every filter is given a filter host
// reference as part of initialization.
//
// This interface is intentionally verbose to cover the needs for the different
// types of filters (see media/base/filters.h for filter definitions).  Filters
// typically use parts of the interface that are relevant to their function.
// For example, an audio renderer filter typically calls SetTime as it feeds
// data to the audio hardware.  A video renderer filter typically calls GetTime
// to synchronize video with audio.  An audio and video decoder would typically
// have no need to call either SetTime or GetTime.

#ifndef MEDIA_BASE_FILTER_HOST_H_
#define MEDIA_BASE_FILTER_HOST_H_

#include "media/base/filters.h"
#include "media/base/pipeline.h"

namespace media {

class FilterHost {
 public:
  // Stops execution of the pipeline due to a fatal error.  Do not call this
  // method with PIPELINE_OK.
  virtual void SetError(PipelineStatus error) = 0;

  // Gets the current time in microseconds.
  virtual base::TimeDelta GetTime() const = 0;

  // Gets the duration in microseconds.
  virtual base::TimeDelta GetDuration() const = 0;

  // Updates the current time.  Other filters should poll to examine the updated
  // time.
  virtual void SetTime(base::TimeDelta time) = 0;

  // Get the duration of the media in microseconds.  If the duration has not
  // been determined yet, then returns 0.
  virtual void SetDuration(base::TimeDelta duration) = 0;

  // Set the approximate amount of playable data buffered so far in micro-
  // seconds.
  virtual void SetBufferedTime(base::TimeDelta buffered_time) = 0;

  // Set the total size of the media file.
  virtual void SetTotalBytes(int64 total_bytes) = 0;

  // Sets the total number of bytes that are buffered on the client and ready to
  // be played.
  virtual void SetBufferedBytes(int64 buffered_bytes) = 0;

  // Sets the size of the video output in pixel units.
  virtual void SetVideoSize(size_t width, size_t height) = 0;

  // Sets the flag to indicate that we are doing streaming.
  virtual void SetStreaming(bool streaming) = 0;

  // Notifies that this filter has ended, typically only called by filter graph
  // endpoints such as renderers.
  virtual void NotifyEnded() = 0;

  // Sets the flag to indicate that our media is now loaded.
  virtual void SetLoaded(bool loaded) = 0;

  // Sets the flag to indicate current network activity.
  virtual void SetNetworkActivity(bool network_activity) = 0;

  // Disable audio renderer by calling OnAudioRendererDisabled() on all
  // filters.
  virtual void DisableAudioRenderer() = 0;

  // Sets the byte offset at which the client is requesting the video.
  virtual void SetCurrentReadPosition(int64 offset) = 0;

  // Gets the byte offset at which the client is requesting the video.
  virtual int64 GetCurrentReadPosition() = 0;

 protected:
  virtual ~FilterHost() {}
};

}  // namespace media

#endif  // MEDIA_BASE_FILTER_HOST_H_
