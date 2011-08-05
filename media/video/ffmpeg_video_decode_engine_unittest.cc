// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "media/base/data_buffer.h"
#include "media/base/mock_ffmpeg.h"
#include "media/base/mock_task.h"
#include "media/base/pipeline.h"
#include "media/video/ffmpeg_video_decode_engine.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::ReturnNull;
using ::testing::SetArgumentPointee;
using ::testing::StrictMock;

namespace media {

static const int kWidth = 320;
static const int kHeight = 240;
static const AVRational kFrameRate = { 100, 1 };

static void InitializeFrame(uint8_t* data, int width, AVFrame* frame) {
  frame->data[0] = data;
  frame->data[1] = data;
  frame->data[2] = data;
  frame->linesize[0] = width;
  frame->linesize[1] = width / 2;
  frame->linesize[2] = width / 2;
}

ACTION_P(DecodeComplete, decoder) {
  decoder->set_video_frame(arg0);
}

ACTION_P2(DemuxComplete, engine, buffer) {
  engine->ConsumeVideoSample(buffer);
}

ACTION_P(SaveInitializeResult, engine) {
  engine->set_video_codec_info(arg0);
}

class FFmpegVideoDecodeEngineTest : public testing::Test,
                                    public VideoDecodeEngine::EventHandler {
 public:
  FFmpegVideoDecodeEngineTest()
      : config_(kCodecH264, kWidth, kHeight,
                kFrameRate.num, kFrameRate.den, NULL, 0) {

    // Setup FFmpeg structures.
    frame_buffer_.reset(new uint8[kWidth * kHeight]);
    memset(&yuv_frame_, 0, sizeof(yuv_frame_));
    InitializeFrame(frame_buffer_.get(), kWidth, &yuv_frame_);

    memset(&codec_context_, 0, sizeof(codec_context_));
    memset(&codec_, 0, sizeof(codec_));

    buffer_ = new DataBuffer(1);

    test_engine_.reset(new FFmpegVideoDecodeEngine());

    VideoFrame::CreateFrame(VideoFrame::YV12,
                            kWidth,
                            kHeight,
                            kNoTimestamp,
                            kNoTimestamp,
                            &video_frame_);
  }

  ~FFmpegVideoDecodeEngineTest() {
    test_engine_.reset();
  }

  void Initialize() {
    EXPECT_CALL(mock_ffmpeg_, AVCodecAllocContext())
        .WillOnce(Return(&codec_context_));
    EXPECT_CALL(mock_ffmpeg_, AVCodecFindDecoder(CODEC_ID_H264))
        .WillOnce(Return(&codec_));
    EXPECT_CALL(mock_ffmpeg_, AVCodecAllocFrame())
        .WillOnce(Return(&yuv_frame_));
    EXPECT_CALL(mock_ffmpeg_, AVCodecThreadInit(&codec_context_, 2))
        .WillOnce(Return(0));
    EXPECT_CALL(mock_ffmpeg_, AVCodecOpen(&codec_context_, &codec_))
        .WillOnce(Return(0));
    EXPECT_CALL(mock_ffmpeg_, AVCodecClose(&codec_context_))
        .WillOnce(Return(0));
    EXPECT_CALL(mock_ffmpeg_, AVFree(&yuv_frame_))
        .Times(1);
    EXPECT_CALL(mock_ffmpeg_, AVFree(&codec_context_))
        .Times(1);

    EXPECT_CALL(*this, OnInitializeComplete(_))
       .WillOnce(SaveInitializeResult(this));
    test_engine_->Initialize(MessageLoop::current(), this, NULL, config_);
    EXPECT_TRUE(info_.success);
  }

  void Decode() {
    EXPECT_CALL(mock_ffmpeg_, AVInitPacket(_));
    EXPECT_CALL(mock_ffmpeg_,
                AVCodecDecodeVideo2(&codec_context_, &yuv_frame_, _, _))
        .WillOnce(DoAll(SetArgumentPointee<2>(1),  // Simulate 1 byte frame.
                        Return(0)));

    EXPECT_CALL(*this, ProduceVideoSample(_))
        .WillOnce(DemuxComplete(test_engine_.get(), buffer_));
    EXPECT_CALL(*this, ConsumeVideoFrame(_, _))
        .WillOnce(DecodeComplete(this));
    test_engine_->ProduceVideoFrame(video_frame_);
  }

  void ChangeDimensions(int width, int height) {
    frame_buffer_.reset(new uint8[width * height]);
    InitializeFrame(frame_buffer_.get(), width, &yuv_frame_);
    codec_context_.width = width;
    codec_context_.height = height;
  }

  // VideoDecodeEngine::EventHandler implementation.
  MOCK_METHOD2(ConsumeVideoFrame,
               void(scoped_refptr<VideoFrame> video_frame,
                    const PipelineStatistics& statistics));
  MOCK_METHOD1(ProduceVideoSample,
               void(scoped_refptr<Buffer> buffer));
  MOCK_METHOD1(OnInitializeComplete,
               void(const VideoCodecInfo& info));
  MOCK_METHOD0(OnUninitializeComplete, void());
  MOCK_METHOD0(OnFlushComplete, void());
  MOCK_METHOD0(OnSeekComplete, void());
  MOCK_METHOD0(OnError, void());
  MOCK_METHOD1(OnFormatChange, void(VideoStreamInfo stream_info));

  // Used by gmock actions.
  void set_video_frame(scoped_refptr<VideoFrame> video_frame) {
    video_frame_ = video_frame;
  }

  void set_video_codec_info(const VideoCodecInfo& info) {
    info_ = info;
  }

 protected:
  VideoDecoderConfig config_;
  VideoCodecInfo info_;
  scoped_refptr<VideoFrame> video_frame_;
  scoped_ptr<FFmpegVideoDecodeEngine> test_engine_;
  scoped_array<uint8_t> frame_buffer_;
  StrictMock<MockFFmpeg> mock_ffmpeg_;

  AVFrame yuv_frame_;
  AVCodecContext codec_context_;
  AVCodec codec_;
  scoped_refptr<DataBuffer> buffer_;

 private:
  DISALLOW_COPY_AND_ASSIGN(FFmpegVideoDecodeEngineTest);
};

TEST_F(FFmpegVideoDecodeEngineTest, Initialize_Normal) {
  Initialize();
}

TEST_F(FFmpegVideoDecodeEngineTest, Initialize_FindDecoderFails) {
  // Test avcodec_find_decoder() returning NULL.
  EXPECT_CALL(mock_ffmpeg_, AVCodecAllocContext())
      .WillOnce(Return(&codec_context_));
  EXPECT_CALL(mock_ffmpeg_, AVCodecFindDecoder(CODEC_ID_H264))
      .WillOnce(ReturnNull());
  EXPECT_CALL(mock_ffmpeg_, AVCodecAllocFrame())
      .WillOnce(Return(&yuv_frame_));
  EXPECT_CALL(mock_ffmpeg_, AVCodecClose(&codec_context_))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_ffmpeg_, AVFree(&yuv_frame_))
      .Times(1);
  EXPECT_CALL(mock_ffmpeg_, AVFree(&codec_context_))
      .Times(1);

  EXPECT_CALL(*this, OnInitializeComplete(_))
     .WillOnce(SaveInitializeResult(this));
  test_engine_->Initialize(MessageLoop::current(), this, NULL, config_);
  EXPECT_FALSE(info_.success);
}

// Note There are 2 threads for FFmpeg-mt.
TEST_F(FFmpegVideoDecodeEngineTest, Initialize_InitThreadFails) {
  // Test avcodec_thread_init() failing.
  EXPECT_CALL(mock_ffmpeg_, AVCodecAllocContext())
      .WillOnce(Return(&codec_context_));
  EXPECT_CALL(mock_ffmpeg_, AVCodecFindDecoder(CODEC_ID_H264))
      .WillOnce(Return(&codec_));
  EXPECT_CALL(mock_ffmpeg_, AVCodecAllocFrame())
      .WillOnce(Return(&yuv_frame_));
  EXPECT_CALL(mock_ffmpeg_, AVCodecThreadInit(&codec_context_, 2))
      .WillOnce(Return(-1));
  EXPECT_CALL(mock_ffmpeg_, AVCodecClose(&codec_context_))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_ffmpeg_, AVFree(&yuv_frame_))
      .Times(1);
  EXPECT_CALL(mock_ffmpeg_, AVFree(&codec_context_))
      .Times(1);

  EXPECT_CALL(*this, OnInitializeComplete(_))
     .WillOnce(SaveInitializeResult(this));
  test_engine_->Initialize(MessageLoop::current(), this, NULL, config_);
  EXPECT_FALSE(info_.success);
}

TEST_F(FFmpegVideoDecodeEngineTest, Initialize_OpenDecoderFails) {
  // Test avcodec_open() failing.
  EXPECT_CALL(mock_ffmpeg_, AVCodecAllocContext())
      .WillOnce(Return(&codec_context_));
  EXPECT_CALL(mock_ffmpeg_, AVCodecFindDecoder(CODEC_ID_H264))
      .WillOnce(Return(&codec_));
  EXPECT_CALL(mock_ffmpeg_, AVCodecAllocFrame())
      .WillOnce(Return(&yuv_frame_));
  EXPECT_CALL(mock_ffmpeg_, AVCodecThreadInit(&codec_context_, 2))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_ffmpeg_, AVCodecOpen(&codec_context_, &codec_))
      .WillOnce(Return(-1));
  EXPECT_CALL(mock_ffmpeg_, AVCodecClose(&codec_context_))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_ffmpeg_, AVFree(&yuv_frame_))
      .Times(1);
  EXPECT_CALL(mock_ffmpeg_, AVFree(&codec_context_))
      .Times(1);

  EXPECT_CALL(*this, OnInitializeComplete(_))
     .WillOnce(SaveInitializeResult(this));
  test_engine_->Initialize(MessageLoop::current(), this, NULL, config_);
  EXPECT_FALSE(info_.success);
}

TEST_F(FFmpegVideoDecodeEngineTest, DecodeFrame_Normal) {
  Initialize();

  // We rely on FFmpeg for timestamp and duration reporting.  The one tricky
  // bit is calculating the duration when |repeat_pict| > 0.
  const base::TimeDelta kTimestamp = base::TimeDelta::FromMicroseconds(123);
  const base::TimeDelta kDuration = base::TimeDelta::FromMicroseconds(15000);
  yuv_frame_.repeat_pict = 1;
  yuv_frame_.reordered_opaque = kTimestamp.InMicroseconds();

  // Simulate decoding a single frame.
  Decode();

  // |video_frame_| timestamp is 0 because we set the timestamp based off
  // the buffer timestamp.
  EXPECT_EQ(0, video_frame_->GetTimestamp().ToInternalValue());
  EXPECT_EQ(kDuration.ToInternalValue(),
            video_frame_->GetDuration().ToInternalValue());
}

TEST_F(FFmpegVideoDecodeEngineTest, DecodeFrame_0ByteFrame) {
  Initialize();

  // Expect a bunch of avcodec calls.
  EXPECT_CALL(mock_ffmpeg_, AVInitPacket(_))
      .Times(2);
  EXPECT_CALL(mock_ffmpeg_,
              AVCodecDecodeVideo2(&codec_context_, &yuv_frame_, _, _))
      .WillOnce(DoAll(SetArgumentPointee<2>(0),  // Simulate 0 byte frame.
                      Return(0)))
      .WillOnce(DoAll(SetArgumentPointee<2>(1),  // Simulate 1 byte frame.
                      Return(0)));

  EXPECT_CALL(*this, ProduceVideoSample(_))
      .WillOnce(DemuxComplete(test_engine_.get(), buffer_))
      .WillOnce(DemuxComplete(test_engine_.get(), buffer_));
  EXPECT_CALL(*this, ConsumeVideoFrame(_, _))
      .WillOnce(DecodeComplete(this));
  test_engine_->ProduceVideoFrame(video_frame_);

  EXPECT_TRUE(video_frame_.get());
}

TEST_F(FFmpegVideoDecodeEngineTest, DecodeFrame_DecodeError) {
  Initialize();

  // Expect a bunch of avcodec calls.
  EXPECT_CALL(mock_ffmpeg_, AVInitPacket(_));
  EXPECT_CALL(mock_ffmpeg_,
              AVCodecDecodeVideo2(&codec_context_, &yuv_frame_, _, _))
      .WillOnce(Return(-1));

  EXPECT_CALL(*this, ProduceVideoSample(_))
      .WillOnce(DemuxComplete(test_engine_.get(), buffer_));
  EXPECT_CALL(*this, OnError());

  test_engine_->ProduceVideoFrame(video_frame_);
}

TEST_F(FFmpegVideoDecodeEngineTest, DecodeFrame_LargerWidth) {
  Initialize();
  ChangeDimensions(kWidth * 2, kHeight);
  Decode();
}

TEST_F(FFmpegVideoDecodeEngineTest, DecodeFrame_SmallerWidth) {
  Initialize();
  ChangeDimensions(kWidth / 2, kHeight);
  Decode();
}

TEST_F(FFmpegVideoDecodeEngineTest, DecodeFrame_LargerHeight) {
  Initialize();
  ChangeDimensions(kWidth, kHeight * 2);
  Decode();
}

TEST_F(FFmpegVideoDecodeEngineTest, DecodeFrame_SmallerHeight) {
  Initialize();
  ChangeDimensions(kWidth, kHeight / 2);
  Decode();
}

TEST_F(FFmpegVideoDecodeEngineTest, GetSurfaceFormat) {
  Initialize();

  // YV12 formats.
  codec_context_.pix_fmt = PIX_FMT_YUV420P;
  EXPECT_EQ(VideoFrame::YV12, test_engine_->GetSurfaceFormat());
  codec_context_.pix_fmt = PIX_FMT_YUVJ420P;
  EXPECT_EQ(VideoFrame::YV12, test_engine_->GetSurfaceFormat());

  // YV16 formats.
  codec_context_.pix_fmt = PIX_FMT_YUV422P;
  EXPECT_EQ(VideoFrame::YV16, test_engine_->GetSurfaceFormat());
  codec_context_.pix_fmt = PIX_FMT_YUVJ422P;
  EXPECT_EQ(VideoFrame::YV16, test_engine_->GetSurfaceFormat());

  // Invalid value.
  codec_context_.pix_fmt = PIX_FMT_NONE;
  EXPECT_EQ(VideoFrame::INVALID, test_engine_->GetSurfaceFormat());
}

}  // namespace media
