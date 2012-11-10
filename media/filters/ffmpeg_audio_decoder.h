// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_FILTERS_FFMPEG_AUDIO_DECODER_H_
#define MEDIA_FILTERS_FFMPEG_AUDIO_DECODER_H_

#include <list>

#include "base/callback.h"
#include "base/message_loop.h"
#include "media/base/audio_decoder.h"
#include "media/base/demuxer_stream.h"

struct AVCodecContext;
struct AVFrame;

namespace media {

class DataBuffer;
class DecoderBuffer;

class MEDIA_EXPORT FFmpegAudioDecoder : public AudioDecoder {
 public:
  FFmpegAudioDecoder(const base::Callback<MessageLoop*()>& message_loop_cb);

  // AudioDecoder implementation.
  virtual void Initialize(const scoped_refptr<DemuxerStream>& stream,
                          const PipelineStatusCB& status_cb,
                          const StatisticsCB& statistics_cb) OVERRIDE;
  virtual void Read(const ReadCB& read_cb) OVERRIDE;
  virtual int bits_per_channel() OVERRIDE;
  virtual ChannelLayout channel_layout() OVERRIDE;
  virtual int samples_per_second() OVERRIDE;
  virtual void Reset(const base::Closure& closure) OVERRIDE;

 protected:
  virtual ~FFmpegAudioDecoder();

 private:
  // Methods running on decoder thread.
  void DoInitialize(const scoped_refptr<DemuxerStream>& stream,
                    const PipelineStatusCB& status_cb,
                    const StatisticsCB& statistics_cb);
  void DoReset(const base::Closure& closure);
  void DoRead(const ReadCB& read_cb);
  void DoDecodeBuffer(DemuxerStream::Status status,
                      const scoped_refptr<DecoderBuffer>& input);

  // Reads from the demuxer stream with corresponding callback method.
  void ReadFromDemuxerStream();
  void DecodeBuffer(DemuxerStream::Status status,
                    const scoped_refptr<DecoderBuffer>& buffer);

  // Returns the timestamp that should be used for the next buffer returned
  // via |read_cb_|. It is calculated from |output_timestamp_base_| and
  // |total_frames_decoded_|.
  base::TimeDelta GetNextOutputTimestamp() const;

  // This is !is_null() iff Initialize() hasn't been called.
  base::Callback<MessageLoop*()> message_loop_factory_cb_;
  MessageLoop* message_loop_;

  scoped_refptr<DemuxerStream> demuxer_stream_;
  StatisticsCB statistics_cb_;
  AVCodecContext* codec_context_;

  // Decoded audio format.
  int bits_per_channel_;
  ChannelLayout channel_layout_;
  int samples_per_second_;

  // Used for computing output timestamps.
  int bytes_per_frame_;
  base::TimeDelta output_timestamp_base_;
  double total_frames_decoded_;
  base::TimeDelta last_input_timestamp_;

  // Number of output sample bytes to drop before generating
  // output buffers.
  int output_bytes_to_drop_;

  // Holds decoded audio.
  AVFrame* av_frame_;

  ReadCB read_cb_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(FFmpegAudioDecoder);
};

}  // namespace media

#endif  // MEDIA_FILTERS_FFMPEG_AUDIO_DECODER_H_
