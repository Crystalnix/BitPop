// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_DEMUXER_H_
#define MEDIA_BASE_DEMUXER_H_

#include "base/memory/ref_counted.h"
#include "base/time.h"
#include "media/base/data_source.h"
#include "media/base/demuxer_stream.h"
#include "media/base/media_export.h"
#include "media/base/pipeline_status.h"
#include "media/base/preload.h"

namespace media {

class MEDIA_EXPORT DemuxerHost : public DataSourceHost {
 public:
  virtual ~DemuxerHost();

  // Get the duration of the media in microseconds.  If the duration has not
  // been determined yet, then returns 0.
  virtual void SetDuration(base::TimeDelta duration) = 0;

  // Set the approximate amount of playable data buffered so far in micro-
  // seconds.
  virtual void SetBufferedTime(base::TimeDelta buffered_time) = 0;

  // Sets the byte offset at which the client is requesting the video.
  virtual void SetCurrentReadPosition(int64 offset) = 0;

  // Stops execution of the pipeline due to a fatal error.  Do not call this
  // method with PIPELINE_OK.
  virtual void OnDemuxerError(PipelineStatus error) = 0;
};

class MEDIA_EXPORT Demuxer
    : public base::RefCountedThreadSafe<Demuxer> {
 public:
  Demuxer();

  // Sets the private member |host_|. This is the first method called by
  // the DemuxerHost after a demuxer is created.  The host holds a strong
  // reference to the demuxer.  The reference held by the host is guaranteed
  // to be released before the host object is destroyed by the pipeline.
  virtual void set_host(DemuxerHost* host);

  // The pipeline playback rate has been changed.  Demuxers may implement this
  // method if they need to respond to this call.
  virtual void SetPlaybackRate(float playback_rate);

  // Carry out any actions required to seek to the given time, executing the
  // callback upon completion.
  virtual void Seek(base::TimeDelta time, const PipelineStatusCB& callback);

  // The pipeline is being stopped either as a result of an error or because
  // the client called Stop().
  virtual void Stop(const base::Closure& callback);

  // This method is called from the pipeline when the audio renderer
  // is disabled. Demuxers can ignore the notification if they do not
  // need to react to this event.
  //
  // TODO(acolwell): Change to generic DisableStream(DemuxerStream::Type).
  virtual void OnAudioRendererDisabled();

  // Returns the given stream type, or NULL if that type is not present.
  virtual scoped_refptr<DemuxerStream> GetStream(DemuxerStream::Type type) = 0;

  // Alert the Demuxer that the video preload value has been changed.
  virtual void SetPreload(Preload preload) = 0;

  // Returns the starting time for the media file.
  virtual base::TimeDelta GetStartTime() const = 0;

  // Returns the content bitrate. May be obtained from container or
  // approximated. Returns 0 if it is unknown.
  virtual int GetBitrate() = 0;

  // Returns true if the source is from a local file or stream (such as a
  // webcam stream), false otherwise.
  virtual bool IsLocalSource() = 0;

  // Returns true if seeking is possible; false otherwise.
  virtual bool IsSeekable() = 0;

 protected:
  // Only allow derived objects access to the DemuxerHost. This is
  // kept out of the public interface because demuxers need to be
  // aware of all calls made to the host object so they can insure
  // the state presented to the host is always consistent with its own
  // state.
  DemuxerHost* host() { return host_; }

  friend class base::RefCountedThreadSafe<Demuxer>;
  virtual ~Demuxer();

 private:
  DemuxerHost* host_;

  DISALLOW_COPY_AND_ASSIGN(Demuxer);
};

}  // namespace media

#endif  // MEDIA_BASE_DEMUXER_H_
