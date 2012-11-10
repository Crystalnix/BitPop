// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/filters/ffmpeg_audio_decoder.h"

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "media/base/audio_decoder_config.h"
#include "media/base/data_buffer.h"
#include "media/base/decoder_buffer.h"
#include "media/base/demuxer.h"
#include "media/base/pipeline.h"
#include "media/ffmpeg/ffmpeg_common.h"
#include "media/filters/ffmpeg_glue.h"

namespace media {

// Returns true if the decode result was end of stream.
static inline bool IsEndOfStream(int result, int decoded_size, Buffer* input) {
  // Three conditions to meet to declare end of stream for this decoder:
  // 1. FFmpeg didn't read anything.
  // 2. FFmpeg didn't output anything.
  // 3. An end of stream buffer is received.
  return result == 0 && decoded_size == 0 && input->IsEndOfStream();
}

FFmpegAudioDecoder::FFmpegAudioDecoder(
    const base::Callback<MessageLoop*()>& message_loop_cb)
    : message_loop_factory_cb_(message_loop_cb),
      message_loop_(NULL),
      codec_context_(NULL),
      bits_per_channel_(0),
      channel_layout_(CHANNEL_LAYOUT_NONE),
      samples_per_second_(0),
      bytes_per_frame_(0),
      output_timestamp_base_(kNoTimestamp()),
      total_frames_decoded_(0),
      last_input_timestamp_(kNoTimestamp()),
      output_bytes_to_drop_(0),
      av_frame_(NULL) {
}

void FFmpegAudioDecoder::Initialize(
    const scoped_refptr<DemuxerStream>& stream,
    const PipelineStatusCB& status_cb,
    const StatisticsCB& statistics_cb) {
  // Ensure FFmpeg has been initialized
  FFmpegGlue::GetInstance();

  if (!message_loop_) {
    message_loop_ = message_loop_factory_cb_.Run();
    message_loop_factory_cb_.Reset();
  } else {
    // TODO(scherkus): initialization currently happens more than once in
    // PipelineIntegrationTest.BasicPlayback.
    LOG(ERROR) << "Initialize has already been called.";
  }
  message_loop_->PostTask(
      FROM_HERE,
      base::Bind(&FFmpegAudioDecoder::DoInitialize, this,
                 stream, status_cb, statistics_cb));
}

void FFmpegAudioDecoder::Read(const ReadCB& read_cb) {
  // Complete operation asynchronously on different stack of execution as per
  // the API contract of AudioDecoder::Read()
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &FFmpegAudioDecoder::DoRead, this, read_cb));
}

int FFmpegAudioDecoder::bits_per_channel() {
  return bits_per_channel_;
}

ChannelLayout FFmpegAudioDecoder::channel_layout() {
  return channel_layout_;
}

int FFmpegAudioDecoder::samples_per_second() {
  return samples_per_second_;
}

void FFmpegAudioDecoder::Reset(const base::Closure& closure) {
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &FFmpegAudioDecoder::DoReset, this, closure));
}

FFmpegAudioDecoder::~FFmpegAudioDecoder() {
  // TODO(scherkus): should we require Stop() to be called? this might end up
  // getting called on a random thread due to refcounting.
  if (codec_context_) {
    av_free(codec_context_->extradata);
    avcodec_close(codec_context_);
    av_free(codec_context_);
  }

  if (av_frame_) {
    av_free(av_frame_);
    av_frame_ = NULL;
  }
}

void FFmpegAudioDecoder::DoInitialize(
    const scoped_refptr<DemuxerStream>& stream,
    const PipelineStatusCB& status_cb,
    const StatisticsCB& statistics_cb) {
  demuxer_stream_ = stream;
  const AudioDecoderConfig& config = stream->audio_decoder_config();
  statistics_cb_ = statistics_cb;

  // TODO(scherkus): this check should go in Pipeline prior to creating
  // decoder objects.
  if (!config.IsValidConfig()) {
    DLOG(ERROR) << "Invalid audio stream -"
                << " codec: " << config.codec()
                << " channel layout: " << config.channel_layout()
                << " bits per channel: " << config.bits_per_channel()
                << " samples per second: " << config.samples_per_second();

    status_cb.Run(DECODER_ERROR_NOT_SUPPORTED);
    return;
  }

  // Initialize AVCodecContext structure.
  codec_context_ = avcodec_alloc_context3(NULL);
  AudioDecoderConfigToAVCodecContext(config, codec_context_);

  AVCodec* codec = avcodec_find_decoder(codec_context_->codec_id);
  if (!codec || avcodec_open2(codec_context_, codec, NULL) < 0) {
    DLOG(ERROR) << "Could not initialize audio decoder: "
                << codec_context_->codec_id;

    status_cb.Run(DECODER_ERROR_NOT_SUPPORTED);
    return;
  }

  // Success!
  av_frame_ = avcodec_alloc_frame();
  bits_per_channel_ = config.bits_per_channel();
  channel_layout_ = config.channel_layout();
  samples_per_second_ = config.samples_per_second();
  bytes_per_frame_ = codec_context_->channels * bits_per_channel_ / 8;
  status_cb.Run(PIPELINE_OK);
}

void FFmpegAudioDecoder::DoReset(const base::Closure& closure) {
  avcodec_flush_buffers(codec_context_);
  output_timestamp_base_ = kNoTimestamp();
  total_frames_decoded_ = 0;
  last_input_timestamp_ = kNoTimestamp();
  output_bytes_to_drop_ = 0;
  closure.Run();
}

void FFmpegAudioDecoder::DoRead(const ReadCB& read_cb) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(!read_cb.is_null());
  CHECK(read_cb_.is_null()) << "Overlapping decodes are not supported.";

  read_cb_ = read_cb;
  ReadFromDemuxerStream();
}

void FFmpegAudioDecoder::DoDecodeBuffer(
    DemuxerStream::Status status,
    const scoped_refptr<DecoderBuffer>& input) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(!read_cb_.is_null());

  if (status != DemuxerStream::kOk) {
    DCHECK(!input);
    // TODO(acolwell): Add support for reinitializing the decoder when
    // |status| == kConfigChanged. For now we just trigger a decode error.
    AudioDecoder::Status decoder_status =
        (status == DemuxerStream::kAborted) ? kAborted : kDecodeError;
    base::ResetAndReturn(&read_cb_).Run(decoder_status, NULL);
    return;
  }

  // Make sure we are notified if http://crbug.com/49709 returns.  Issue also
  // occurs with some damaged files.
  if (!input->IsEndOfStream() && input->GetTimestamp() == kNoTimestamp() &&
      output_timestamp_base_ == kNoTimestamp()) {
    DVLOG(1) << "Received a buffer without timestamps!";
    base::ResetAndReturn(&read_cb_).Run(kDecodeError, NULL);
    return;
  }

  bool is_vorbis = codec_context_->codec_id == CODEC_ID_VORBIS;
  if (!input->IsEndOfStream()) {
    if (last_input_timestamp_ == kNoTimestamp()) {
      if (is_vorbis && (input->GetTimestamp() < base::TimeDelta())) {
        // Dropping frames for negative timestamps as outlined in section A.2
        // in the Vorbis spec. http://xiph.org/vorbis/doc/Vorbis_I_spec.html
        int frames_to_drop = floor(
            0.5 + -input->GetTimestamp().InSecondsF() * samples_per_second_);
        output_bytes_to_drop_ = bytes_per_frame_ * frames_to_drop;
      } else {
        last_input_timestamp_ = input->GetTimestamp();
      }
    } else if (input->GetTimestamp() != kNoTimestamp()) {
      if (input->GetTimestamp() < last_input_timestamp_) {
        base::TimeDelta diff = input->GetTimestamp() - last_input_timestamp_;
        DVLOG(1) << "Input timestamps are not monotonically increasing! "
                 << " ts " << input->GetTimestamp().InMicroseconds() << " us"
                 << " diff " << diff.InMicroseconds() << " us";
        base::ResetAndReturn(&read_cb_).Run(kDecodeError, NULL);
        return;
      }

      last_input_timestamp_ = input->GetTimestamp();
    }
  }

  AVPacket packet;
  av_init_packet(&packet);
  packet.data = const_cast<uint8*>(input->GetData());
  packet.size = input->GetDataSize();

  PipelineStatistics statistics;
  statistics.audio_bytes_decoded = input->GetDataSize();

  // Reset frame to default values.
  avcodec_get_frame_defaults(av_frame_);

  int frame_decoded = 0;
  int result = avcodec_decode_audio4(
      codec_context_, av_frame_, &frame_decoded, &packet);

  if (result < 0) {
    DCHECK(!input->IsEndOfStream())
        << "End of stream buffer produced an error! "
        << "This is quite possibly a bug in the audio decoder not handling "
        << "end of stream AVPackets correctly.";

    DLOG(ERROR) << "Error decoding an audio frame with timestamp: "
                << input->GetTimestamp().InMicroseconds() << " us, duration: "
                << input->GetDuration().InMicroseconds() << " us, packet size: "
                << input->GetDataSize() << " bytes";

    ReadFromDemuxerStream();
    return;
  }

  if (result > 0)
    DCHECK_EQ(result, input->GetDataSize());

  if (output_timestamp_base_ == kNoTimestamp() && !input->IsEndOfStream()) {
    DCHECK(input->GetTimestamp() != kNoTimestamp());
    if (output_bytes_to_drop_ > 0) {
      // Currently Vorbis is the only codec that causes us to drop samples.
      // If we have to drop samples it always means the timeline starts at 0.
      DCHECK(is_vorbis);
      output_timestamp_base_ = base::TimeDelta();
    } else {
      output_timestamp_base_ = input->GetTimestamp();
    }
  }

  const uint8* decoded_audio_data = NULL;
  int decoded_audio_size = 0;
  if (frame_decoded) {
    int output_sample_rate = av_frame_->sample_rate;
    if (output_sample_rate != samples_per_second_) {
      DLOG(ERROR) << "Output sample rate (" << output_sample_rate
                  << ") doesn't match expected rate " << samples_per_second_;
      base::ResetAndReturn(&read_cb_).Run(kDecodeError, NULL);
      return;
    }

    decoded_audio_data = av_frame_->data[0];
    decoded_audio_size = av_samples_get_buffer_size(
        NULL, codec_context_->channels, av_frame_->nb_samples,
        codec_context_->sample_fmt, 1);
  }

  scoped_refptr<DataBuffer> output;

  if (decoded_audio_size > 0 && output_bytes_to_drop_ > 0) {
    int dropped_size = std::min(decoded_audio_size, output_bytes_to_drop_);
    decoded_audio_data += dropped_size;
    decoded_audio_size -= dropped_size;
    output_bytes_to_drop_ -= dropped_size;
  }

  if (decoded_audio_size > 0) {
    DCHECK_EQ(decoded_audio_size % bytes_per_frame_, 0)
        << "Decoder didn't output full frames";

    // Copy the audio samples into an output buffer.
    output = new DataBuffer(decoded_audio_size);
    output->SetDataSize(decoded_audio_size);
    uint8* data = output->GetWritableData();
    memcpy(data, decoded_audio_data, decoded_audio_size);

    base::TimeDelta timestamp = GetNextOutputTimestamp();
    total_frames_decoded_ += decoded_audio_size / bytes_per_frame_;

    output->SetTimestamp(timestamp);
    output->SetDuration(GetNextOutputTimestamp() - timestamp);
  } else if (IsEndOfStream(result, decoded_audio_size, input)) {
    // Create an end of stream output buffer.
    output = new DataBuffer(0);
  }

  // Decoding finished successfully, update stats and execute callback.
  statistics_cb_.Run(statistics);

  if (!output) {
    ReadFromDemuxerStream();
    return;
  }
  base::ResetAndReturn(&read_cb_).Run(kOk, output);
}

void FFmpegAudioDecoder::ReadFromDemuxerStream() {
  DCHECK(!read_cb_.is_null());

  demuxer_stream_->Read(base::Bind(&FFmpegAudioDecoder::DecodeBuffer, this));
}

void FFmpegAudioDecoder::DecodeBuffer(
    DemuxerStream::Status status,
    const scoped_refptr<DecoderBuffer>& buffer) {
  DCHECK_EQ(status != DemuxerStream::kOk, !buffer) << status;

  // TODO(scherkus): fix FFmpegDemuxerStream::Read() to not execute our read
  // callback on the same execution stack so we can get rid of forced task post.
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &FFmpegAudioDecoder::DoDecodeBuffer, this, status, buffer));
}

base::TimeDelta FFmpegAudioDecoder::GetNextOutputTimestamp() const {
  DCHECK(output_timestamp_base_ != kNoTimestamp());
  double decoded_us = (total_frames_decoded_ / samples_per_second_) *
      base::Time::kMicrosecondsPerSecond;
  return output_timestamp_base_ + base::TimeDelta::FromMicroseconds(decoded_us);
}
}  // namespace media
