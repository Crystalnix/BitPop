// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Filters are connected in a strongly typed manner, with downstream filters
// always reading data from upstream filters.  Upstream filters have no clue
// who is actually reading from them, and return the results via callbacks.
//
//                         DemuxerStream(Video) <- VideoDecoder <- VideoRenderer
// DataSource <- Demuxer <
//                         DemuxerStream(Audio) <- AudioDecoder <- AudioRenderer
//
// Upstream -------------------------------------------------------> Downstream
//                         <- Reads flow this way
//                    Buffer assignments flow this way ->
//
// Every filter maintains a reference to the scheduler, who maintains data
// shared between filters (i.e., reference clock value, playback state).  The
// scheduler is also responsible for scheduling filter tasks (i.e., a read on
// a VideoDecoder would result in scheduling a Decode task).  Filters can also
// use the scheduler to signal errors and shutdown playback.

#ifndef MEDIA_BASE_FILTERS_H_
#define MEDIA_BASE_FILTERS_H_

#include <limits>
#include <string>

#include "base/callback.h"
#include "base/callback_old.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/time.h"
#include "media/base/audio_decoder_config.h"
#include "media/base/media_format.h"
#include "media/base/pipeline_status.h"
#include "media/base/video_frame.h"

struct AVStream;

namespace media {

class Buffer;
class Decoder;
class DemuxerStream;
class Filter;
class FilterHost;

struct PipelineStatistics;

// Used to specify video preload states. They are "hints" to the browser about
// how aggressively the browser should load and buffer data.
// Please see the HTML5 spec for the descriptions of these values:
// http://www.w3.org/TR/html5/video.html#attr-media-preload
//
// Enum values must match the values in WebCore::MediaPlayer::Preload and
// there will be assertions at compile time if they do not match.
enum Preload {
  NONE,
  METADATA,
  AUTO,
};

// Used for completing asynchronous methods.
typedef Callback0::Type FilterCallback;
typedef base::Callback<void(PipelineStatus)> FilterStatusCB;

// This function copies |cb|, calls Reset() on |cb|, and then calls Run()
// on the copy. This is used in the common case where you need to clear
// a callback member variable before running the callback.
void ResetAndRunCB(FilterStatusCB* cb, PipelineStatus status);

// Used for updating pipeline statistics.
typedef Callback1<const PipelineStatistics&>::Type StatisticsCallback;

class Filter : public base::RefCountedThreadSafe<Filter> {
 public:
  Filter();

  // Sets the private member |host_|. This is the first method called by
  // the FilterHost after a filter is created.  The host holds a strong
  // reference to the filter.  The reference held by the host is guaranteed
  // to be released before the host object is destroyed by the pipeline.
  virtual void set_host(FilterHost* host);

  virtual FilterHost* host();

  // The pipeline has resumed playback.  Filters can continue requesting reads.
  // Filters may implement this method if they need to respond to this call.
  // TODO(boliu): Check that callback is not NULL in subclasses.
  virtual void Play(FilterCallback* callback);

  // The pipeline has paused playback.  Filters should stop buffer exchange.
  // Filters may implement this method if they need to respond to this call.
  // TODO(boliu): Check that callback is not NULL in subclasses.
  virtual void Pause(FilterCallback* callback);

  // The pipeline has been flushed.  Filters should return buffer to owners.
  // Filters may implement this method if they need to respond to this call.
  // TODO(boliu): Check that callback is not NULL in subclasses.
  virtual void Flush(FilterCallback* callback);

  // The pipeline is being stopped either as a result of an error or because
  // the client called Stop().
  // TODO(boliu): Check that callback is not NULL in subclasses.
  virtual void Stop(FilterCallback* callback);

  // The pipeline playback rate has been changed.  Filters may implement this
  // method if they need to respond to this call.
  virtual void SetPlaybackRate(float playback_rate);

  // Carry out any actions required to seek to the given time, executing the
  // callback upon completion.
  virtual void Seek(base::TimeDelta time, const FilterStatusCB& callback);

  // This method is called from the pipeline when the audio renderer
  // is disabled. Filters can ignore the notification if they do not
  // need to react to this event.
  virtual void OnAudioRendererDisabled();

 protected:
  // Only allow scoped_refptr<> to delete filters.
  friend class base::RefCountedThreadSafe<Filter>;
  virtual ~Filter();

  FilterHost* host() const { return host_; }

 private:
  FilterHost* host_;

  DISALLOW_COPY_AND_ASSIGN(Filter);
};

class DataSource : public Filter {
 public:
  typedef Callback1<size_t>::Type ReadCallback;
  static const size_t kReadError = static_cast<size_t>(-1);

  // Reads |size| bytes from |position| into |data|. And when the read is done
  // or failed, |read_callback| is called with the number of bytes read or
  // kReadError in case of error.
  // TODO(hclam): should change |size| to int! It makes the code so messy
  // with size_t and int all over the place..
  virtual void Read(int64 position, size_t size,
                    uint8* data, ReadCallback* read_callback) = 0;

  // Returns true and the file size, false if the file size could not be
  // retrieved.
  virtual bool GetSize(int64* size_out) = 0;

  // Returns true if we are performing streaming. In this case seeking is
  // not possible.
  virtual bool IsStreaming() = 0;

  // Alert the DataSource that the video preload value has been changed.
  virtual void SetPreload(Preload preload) = 0;
};

class DemuxerStream : public base::RefCountedThreadSafe<DemuxerStream> {
 public:
  typedef base::Callback<void(Buffer*)> ReadCallback;

  enum Type {
    UNKNOWN,
    AUDIO,
    VIDEO,
    NUM_TYPES,  // Always keep this entry as the last one!
  };

  // Schedules a read.  When the |read_callback| is called, the downstream
  // filter takes ownership of the buffer by AddRef()'ing the buffer.
  virtual void Read(const ReadCallback& read_callback) = 0;

  // Returns an |AVStream*| if supported, or NULL.
  virtual AVStream* GetAVStream();

  // Returns the type of stream.
  virtual Type type() = 0;

  // Returns the media format of this stream.
  virtual const MediaFormat& media_format() = 0;

  virtual void EnableBitstreamConverter() = 0;

 protected:
  friend class base::RefCountedThreadSafe<DemuxerStream>;
  virtual ~DemuxerStream();
};

class Demuxer : public Filter {
 public:
  // Returns the given stream type, or NULL if that type is not present.
  virtual scoped_refptr<DemuxerStream> GetStream(DemuxerStream::Type type) = 0;

  // Alert the Demuxer that the video preload value has been changed.
  virtual void SetPreload(Preload preload) = 0;
};


class VideoDecoder : public Filter {
 public:
  // Initialize a VideoDecoder with the given DemuxerStream, executing the
  // callback upon completion.
  // stats_callback is used to update global pipeline statistics.
  virtual void Initialize(DemuxerStream* stream, FilterCallback* callback,
                          StatisticsCallback* stats_callback) = 0;

  // Renderer provides an output buffer for Decoder to write to. These buffers
  // will be recycled to renderer via the permanent callback.
  //
  // We could also pass empty pointer here to let decoder provide buffers pool.
  virtual void ProduceVideoFrame(scoped_refptr<VideoFrame> frame) = 0;

  // Installs a permanent callback for passing decoded video output.
  //
  // A NULL frame represents a decoding error.
  typedef base::Callback<void(scoped_refptr<VideoFrame>)> ConsumeVideoFrameCB;
  void set_consume_video_frame_callback(const ConsumeVideoFrameCB& callback) {
    consume_video_frame_callback_ = callback;
  }

  // Indicate whether decoder provides its own output buffers
  virtual bool ProvidesBuffer() = 0;

  // Returns the media format produced by this decoder.
  virtual const MediaFormat& media_format() = 0;

 protected:
  // Executes the permanent callback to pass off decoded video.
  //
  // TODO(scherkus): name this ConsumeVideoFrame() once we fix the TODO in
  // VideoDecodeEngine::EventHandler to remove ConsumeVideoFrame() from there.
  void VideoFrameReady(scoped_refptr<VideoFrame> frame) {
    consume_video_frame_callback_.Run(frame);
  }

  VideoDecoder();
  virtual ~VideoDecoder();

 private:
  ConsumeVideoFrameCB consume_video_frame_callback_;
};


class AudioDecoder : public Filter {
 public:
  // Initialize a AudioDecoder with the given DemuxerStream, executing the
  // callback upon completion.
  // stats_callback is used to update global pipeline statistics.
  virtual void Initialize(DemuxerStream* stream, FilterCallback* callback,
                          StatisticsCallback* stats_callback) = 0;

  virtual AudioDecoderConfig config() = 0;

  // Renderer provides an output buffer for Decoder to write to. These buffers
  // will be recycled to renderer via the permanent callback.
  //
  // We could also pass empty pointer here to let decoder provide buffers pool.
  virtual void ProduceAudioSamples(scoped_refptr<Buffer> buffer) = 0;

  // Installs a permanent callback for passing decoded audio output.
  typedef base::Callback<void(scoped_refptr<Buffer>)> ConsumeAudioSamplesCB;
  void set_consume_audio_samples_callback(
      const ConsumeAudioSamplesCB& callback) {
    consume_audio_samples_callback_ = callback;
  }

 protected:
  AudioDecoder();
  virtual ~AudioDecoder();

  // Executes the permanent callback to pass off decoded audio.
  void ConsumeAudioSamples(scoped_refptr<Buffer> buffer);

 private:
  ConsumeAudioSamplesCB consume_audio_samples_callback_;
};


class VideoRenderer : public Filter {
 public:
  // Initialize a VideoRenderer with the given VideoDecoder, executing the
  // callback upon completion.
  virtual void Initialize(VideoDecoder* decoder, FilterCallback* callback,
                          StatisticsCallback* stats_callback) = 0;

  // Returns true if this filter has received and processed an end-of-stream
  // buffer.
  virtual bool HasEnded() = 0;
};


class AudioRenderer : public Filter {
 public:
  // Initialize a AudioRenderer with the given AudioDecoder, executing the
  // callback upon completion.
  virtual void Initialize(AudioDecoder* decoder, FilterCallback* callback) = 0;

  // Returns true if this filter has received and processed an end-of-stream
  // buffer.
  virtual bool HasEnded() = 0;

  // Sets the output volume.
  virtual void SetVolume(float volume) = 0;
};

}  // namespace media

#endif  // MEDIA_BASE_FILTERS_H_
