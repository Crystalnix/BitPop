// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/filters/ffmpeg_video_decoder.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/message_loop.h"
#include "base/string_number_conversions.h"
#include "media/base/demuxer_stream.h"
#include "media/base/filter_host.h"
#include "media/base/limits.h"
#include "media/base/media_switches.h"
#include "media/base/pipeline.h"
#include "media/base/video_decoder_config.h"
#include "media/base/video_frame.h"
#include "media/base/video_util.h"
#include "media/ffmpeg/ffmpeg_common.h"

namespace media {

// Always try to use three threads for video decoding.  There is little reason
// not to since current day CPUs tend to be multi-core and we measured
// performance benefits on older machines such as P4s with hyperthreading.
//
// Handling decoding on separate threads also frees up the pipeline thread to
// continue processing. Although it'd be nice to have the option of a single
// decoding thread, FFmpeg treats having one thread the same as having zero
// threads (i.e., avcodec_decode_video() will execute on the calling thread).
// Yet another reason for having two threads :)
static const int kDecodeThreads = 2;
static const int kMaxDecodeThreads = 16;

// Returns the number of threads given the FFmpeg CodecID. Also inspects the
// command line for a valid --video-threads flag.
static int GetThreadCount(CodecID codec_id) {
  // TODO(scherkus): As of 07/21/2011 we still can't enable Theora multithreaded
  // decoding due to bugs in FFmpeg. Dig in and send fixes upstream!
  //
  // Refer to http://crbug.com/93932 for tsan suppressions on decoding.
  int decode_threads = (codec_id == CODEC_ID_THEORA ? 1 : kDecodeThreads);

  const CommandLine* cmd_line = CommandLine::ForCurrentProcess();
  std::string threads(cmd_line->GetSwitchValueASCII(switches::kVideoThreads));
  if (threads.empty() || !base::StringToInt(threads, &decode_threads))
    return decode_threads;

  decode_threads = std::max(decode_threads, 0);
  decode_threads = std::min(decode_threads, kMaxDecodeThreads);
  return decode_threads;
}

FFmpegVideoDecoder::FFmpegVideoDecoder(MessageLoop* message_loop)
    : message_loop_(message_loop),
      state_(kUninitialized),
      codec_context_(NULL),
      av_frame_(NULL),
      frame_rate_numerator_(0),
      frame_rate_denominator_(0) {
}

FFmpegVideoDecoder::~FFmpegVideoDecoder() {
  ReleaseFFmpegResources();
}

void FFmpegVideoDecoder::Initialize(DemuxerStream* demuxer_stream,
                                    const PipelineStatusCB& callback,
                                    const StatisticsCallback& stats_callback) {
  if (MessageLoop::current() != message_loop_) {
    message_loop_->PostTask(FROM_HERE, base::Bind(
        &FFmpegVideoDecoder::Initialize, this,
        make_scoped_refptr(demuxer_stream), callback, stats_callback));
    return;
  }

  DCHECK(!demuxer_stream_);

  if (!demuxer_stream) {
    callback.Run(PIPELINE_ERROR_DECODE);
    return;
  }

  demuxer_stream_ = demuxer_stream;
  statistics_callback_ = stats_callback;

  const VideoDecoderConfig& config = demuxer_stream->video_decoder_config();

  // TODO(scherkus): this check should go in Pipeline prior to creating
  // decoder objects.
  if (!config.IsValidConfig()) {
    DLOG(ERROR) << "Invalid video stream - " << config.AsHumanReadableString();
    callback.Run(PIPELINE_ERROR_DECODE);
    return;
  }

  // Initialize AVCodecContext structure.
  codec_context_ = avcodec_alloc_context();
  VideoDecoderConfigToAVCodecContext(config, codec_context_);

  // Enable motion vector search (potentially slow), strong deblocking filter
  // for damaged macroblocks, and set our error detection sensitivity.
  codec_context_->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;
  codec_context_->error_recognition = FF_ER_CAREFUL;
  codec_context_->thread_count = GetThreadCount(codec_context_->codec_id);

  AVCodec* codec = avcodec_find_decoder(codec_context_->codec_id);
  if (!codec) {
    callback.Run(PIPELINE_ERROR_DECODE);
    return;
  }

  if (avcodec_open(codec_context_, codec) < 0) {
    callback.Run(PIPELINE_ERROR_DECODE);
    return;
  }

  // Success!
  state_ = kNormal;
  av_frame_ = avcodec_alloc_frame();
  natural_size_ = config.natural_size();
  frame_rate_numerator_ = config.frame_rate_numerator();
  frame_rate_denominator_ = config.frame_rate_denominator();
  callback.Run(PIPELINE_OK);
}

void FFmpegVideoDecoder::Stop(const base::Closure& callback) {
  if (MessageLoop::current() != message_loop_) {
    message_loop_->PostTask(FROM_HERE, base::Bind(
        &FFmpegVideoDecoder::Stop, this, callback));
    return;
  }

  ReleaseFFmpegResources();
  state_ = kUninitialized;
  callback.Run();
}

void FFmpegVideoDecoder::Seek(base::TimeDelta time, const FilterStatusCB& cb) {
  if (MessageLoop::current() != message_loop_) {
    message_loop_->PostTask(FROM_HERE, base::Bind(
        &FFmpegVideoDecoder::Seek, this, time, cb));
    return;
  }

  cb.Run(PIPELINE_OK);
}

void FFmpegVideoDecoder::Pause(const base::Closure& callback) {
  if (MessageLoop::current() != message_loop_) {
    message_loop_->PostTask(FROM_HERE, base::Bind(
        &FFmpegVideoDecoder::Pause, this, callback));
    return;
  }

  callback.Run();
}

void FFmpegVideoDecoder::Flush(const base::Closure& callback) {
  if (MessageLoop::current() != message_loop_) {
    message_loop_->PostTask(FROM_HERE, base::Bind(
        &FFmpegVideoDecoder::Flush, this, callback));
    return;
  }

  avcodec_flush_buffers(codec_context_);
  state_ = kNormal;
  callback.Run();
}

void FFmpegVideoDecoder::Read(const ReadCB& callback) {
  // Complete operation asynchronously on different stack of execution as per
  // the API contract of VideoDecoder::Read()
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &FFmpegVideoDecoder::DoRead, this, callback));
}

const gfx::Size& FFmpegVideoDecoder::natural_size() {
  return natural_size_;
}

void FFmpegVideoDecoder::DoRead(const ReadCB& callback) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(!callback.is_null());
  CHECK(read_cb_.is_null()) << "Overlapping decodes are not supported.";

  // This can happen during shutdown after Stop() has been called.
  if (state_ == kUninitialized) {
    return;
  }

  // Return empty frames if decoding has finished.
  if (state_ == kDecodeFinished) {
    callback.Run(VideoFrame::CreateEmptyFrame());
    return;
  }

  read_cb_ = callback;
  ReadFromDemuxerStream();
}


void FFmpegVideoDecoder::ReadFromDemuxerStream() {
  DCHECK_NE(state_, kUninitialized);
  DCHECK_NE(state_, kDecodeFinished);
  DCHECK(!read_cb_.is_null());

  demuxer_stream_->Read(base::Bind(&FFmpegVideoDecoder::DecodeBuffer, this));
}

void FFmpegVideoDecoder::DecodeBuffer(const scoped_refptr<Buffer>& buffer) {
  // TODO(scherkus): fix FFmpegDemuxerStream::Read() to not execute our read
  // callback on the same execution stack so we can get rid of forced task post.
  message_loop_->PostTask(FROM_HERE, base::Bind(
      &FFmpegVideoDecoder::DoDecodeBuffer, this, buffer));
}

void FFmpegVideoDecoder::DoDecodeBuffer(const scoped_refptr<Buffer>& buffer) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK_NE(state_, kUninitialized);
  DCHECK_NE(state_, kDecodeFinished);
  DCHECK(!read_cb_.is_null());

  if (!buffer) {
    DeliverFrame(NULL);
    return;
  }

  // During decode, because reads are issued asynchronously, it is possible to
  // receive multiple end of stream buffers since each read is acked. When the
  // first end of stream buffer is read, FFmpeg may still have frames queued
  // up in the decoder so we need to go through the decode loop until it stops
  // giving sensible data.  After that, the decoder should output empty
  // frames.  There are three states the decoder can be in:
  //
  //   kNormal: This is the starting state. Buffers are decoded. Decode errors
  //            are discarded.
  //   kFlushCodec: There isn't any more input data. Call avcodec_decode_video2
  //                until no more data is returned to flush out remaining
  //                frames. The input buffer is ignored at this point.
  //   kDecodeFinished: All calls return empty frames.
  //
  // These are the possible state transitions.
  //
  // kNormal -> kFlushCodec:
  //     When buffer->IsEndOfStream() is first true.
  // kNormal -> kDecodeFinished:
  //     A decoding error occurs and decoding needs to stop.
  // kFlushCodec -> kDecodeFinished:
  //     When avcodec_decode_video2() returns 0 data or errors out.
  // (any state) -> kNormal:
  //     Any time Flush() is called.

  // Transition to kFlushCodec on the first end of stream buffer.
  if (state_ == kNormal && buffer->IsEndOfStream()) {
    state_ = kFlushCodec;
  }

  scoped_refptr<VideoFrame> video_frame;
  if (!Decode(buffer, &video_frame)) {
    state_ = kDecodeFinished;
    DeliverFrame(VideoFrame::CreateEmptyFrame());
    host()->SetError(PIPELINE_ERROR_DECODE);
    return;
  }

  // Any successful decode counts!
  if (buffer->GetDataSize()) {
    PipelineStatistics statistics;
    statistics.video_bytes_decoded = buffer->GetDataSize();
    statistics_callback_.Run(statistics);
  }

  // If we didn't get a frame then we've either completely finished decoding or
  // we need more data.
  if (!video_frame) {
    if (state_ == kFlushCodec) {
      state_ = kDecodeFinished;
      DeliverFrame(VideoFrame::CreateEmptyFrame());
      return;
    }

    ReadFromDemuxerStream();
    return;
  }

  DeliverFrame(video_frame);
}

bool FFmpegVideoDecoder::Decode(
    const scoped_refptr<Buffer>& buffer,
    scoped_refptr<VideoFrame>* video_frame) {
  DCHECK(video_frame);

  // Create a packet for input data.
  // Due to FFmpeg API changes we no longer have const read-only pointers.
  AVPacket packet;
  av_init_packet(&packet);
  packet.data = const_cast<uint8*>(buffer->GetData());
  packet.size = buffer->GetDataSize();

  // Let FFmpeg handle presentation timestamp reordering.
  codec_context_->reordered_opaque = buffer->GetTimestamp().InMicroseconds();

  // This is for codecs not using get_buffer to initialize
  // |av_frame_->reordered_opaque|
  av_frame_->reordered_opaque = codec_context_->reordered_opaque;

  int frame_decoded = 0;
  int result = avcodec_decode_video2(codec_context_,
                                     av_frame_,
                                     &frame_decoded,
                                     &packet);
  // Log the problem if we can't decode a video frame and exit early.
  if (result < 0) {
    LOG(ERROR) << "Error decoding a video frame with timestamp: "
               << buffer->GetTimestamp().InMicroseconds() << " us, duration: "
               << buffer->GetDuration().InMicroseconds() << " us, packet size: "
               << buffer->GetDataSize() << " bytes";
    *video_frame = NULL;
    return false;
  }

  // If no frame was produced then signal that more data is required to
  // produce more frames. This can happen under two circumstances:
  //   1) Decoder was recently initialized/flushed
  //   2) End of stream was reached and all internal frames have been output
  if (frame_decoded == 0) {
    *video_frame = NULL;
    return true;
  }

  // TODO(fbarchard): Work around for FFmpeg http://crbug.com/27675
  // The decoder is in a bad state and not decoding correctly.
  // Checking for NULL avoids a crash in CopyPlane().
  if (!av_frame_->data[VideoFrame::kYPlane] ||
      !av_frame_->data[VideoFrame::kUPlane] ||
      !av_frame_->data[VideoFrame::kVPlane]) {
    LOG(ERROR) << "Video frame was produced yet has invalid frame data.";
    *video_frame = NULL;
    return false;
  }

  // We've got a frame! Make sure we have a place to store it.
  *video_frame = AllocateVideoFrame();
  if (!(*video_frame)) {
    LOG(ERROR) << "Failed to allocate video frame";
    return false;
  }

  // Determine timestamp and calculate the duration based on the repeat picture
  // count.  According to FFmpeg docs, the total duration can be calculated as
  // follows:
  //   fps = 1 / time_base
  //
  //   duration = (1 / fps) + (repeat_pict) / (2 * fps)
  //            = (2 + repeat_pict) / (2 * fps)
  //            = (2 + repeat_pict) / (2 * (1 / time_base))
  DCHECK_LE(av_frame_->repeat_pict, 2);  // Sanity check.
  AVRational doubled_time_base;
  doubled_time_base.num = frame_rate_denominator_;
  doubled_time_base.den = frame_rate_numerator_ * 2;

  (*video_frame)->SetTimestamp(
      base::TimeDelta::FromMicroseconds(av_frame_->reordered_opaque));
  (*video_frame)->SetDuration(
      ConvertFromTimeBase(doubled_time_base, 2 + av_frame_->repeat_pict));

  // Copy the frame data since FFmpeg reuses internal buffers for AVFrame
  // output, meaning the data is only valid until the next
  // avcodec_decode_video() call.
  int y_rows = codec_context_->height;
  int uv_rows = codec_context_->height;
  if (codec_context_->pix_fmt == PIX_FMT_YUV420P) {
    uv_rows /= 2;
  }

  CopyYPlane(av_frame_->data[0], av_frame_->linesize[0], y_rows, *video_frame);
  CopyUPlane(av_frame_->data[1], av_frame_->linesize[1], uv_rows, *video_frame);
  CopyVPlane(av_frame_->data[2], av_frame_->linesize[2], uv_rows, *video_frame);

  return true;
}

void FFmpegVideoDecoder::DeliverFrame(
    const scoped_refptr<VideoFrame>& video_frame) {
  // Reset the callback before running to protect against reentrancy.
  ReadCB read_cb = read_cb_;
  read_cb_.Reset();
  read_cb.Run(video_frame);
}

void FFmpegVideoDecoder::ReleaseFFmpegResources() {
  if (codec_context_) {
    av_free(codec_context_->extradata);
    avcodec_close(codec_context_);
    av_free(codec_context_);
    codec_context_ = NULL;
  }
  if (av_frame_) {
    av_free(av_frame_);
    av_frame_ = NULL;
  }
}

scoped_refptr<VideoFrame> FFmpegVideoDecoder::AllocateVideoFrame() {
  VideoFrame::Format format = PixelFormatToVideoFormat(codec_context_->pix_fmt);
  size_t width = codec_context_->width;
  size_t height = codec_context_->height;

  return VideoFrame::CreateFrame(format, width, height,
                                 kNoTimestamp(), kNoTimestamp());
}

}  // namespace media
