// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ffmpeg_unittests verify that the parts of the FFmpeg API that Chromium uses
// function as advertised for each media format that Chromium supports.  This
// mostly includes stuff like reporting proper timestamps, seeking to
// keyframes, and supporting certain features like reordered_opaque.
//
// Known failures as of r54591:
//   http://crbug.com/47761
//     crbug47761_ogg/FFmpegTest.Loop_Audio/0
//     crbug47761_ogg/FFmpegTest.Seek_Audio/0
//     crbug47761_ogg/FFmpegTest.Decode_Audio/0
//
//   http://crbug.com/49709
//     sync1_ogg/FFmpegTest.Seek_Audio/0
//     sync1_ogv/FFmpegTest.Seek_Audio/0
//     sync2_ogg/FFmpegTest.Seek_Audio/0
//     sync2_ogv/FFmpegTest.Seek_Audio/0
//
//   http://crbug.com/50457
//     sync0_webm/FFmpegTest.Decode_Video/0
//     sync0_webm/FFmpegTest.Duration/0
//     sync1_webm/FFmpegTest.Decode_Video/0
//     sync2_webm/FFmpegTest.Decode_Video/0

#include <limits>
#include <queue>

#include "base/base_paths.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/perftimer.h"
#include "base/string_util.h"
#include "base/test/perf_test_suite.h"
#include "media/base/media.h"
#include "media/ffmpeg/ffmpeg_common.h"
#include "media/ffmpeg/file_protocol.h"
#include "testing/gtest/include/gtest/gtest.h"

int main(int argc, char** argv) {
  return base::PerfTestSuite(argc, argv).Run();
}

namespace media {

class AVPacketQueue {
 public:
  AVPacketQueue() {
  }

  ~AVPacketQueue() {
    flush();
  }

  bool empty() {
    return packets_.empty();
  }

  AVPacket* peek() {
    return packets_.front();
  }

  void pop() {
    AVPacket* packet = packets_.front();
    packets_.pop();
    av_free_packet(packet);
    delete packet;
  }

  void push(AVPacket* packet) {
    av_dup_packet(packet);
    packets_.push(packet);
  }

  void flush() {
    while (!empty()) {
      pop();
    }
  }

 private:
  std::queue<AVPacket*> packets_;

  DISALLOW_COPY_AND_ASSIGN(AVPacketQueue);
};

class FFmpegTest : public testing::TestWithParam<const char*> {
 protected:
  FFmpegTest()
      : av_format_context_(NULL),
        audio_stream_index_(-1),
        video_stream_index_(-1),
        audio_buffer_(NULL),
        video_buffer_(NULL),
        decoded_audio_time_(AV_NOPTS_VALUE),
        decoded_audio_duration_(AV_NOPTS_VALUE),
        decoded_video_time_(AV_NOPTS_VALUE),
        decoded_video_duration_(AV_NOPTS_VALUE),
        duration_(AV_NOPTS_VALUE) {
    InitializeFFmpeg();

    audio_buffer_.reset(
        reinterpret_cast<int16*>(av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE)));
    video_buffer_.reset(avcodec_alloc_frame());
  }

  virtual ~FFmpegTest() {
  }

  void OpenAndReadFile(const std::string& name) {
    OpenFile(name);
    OpenCodecs();
    ReadRemainingFile();
  }

  void OpenFile(const std::string& name) {
    FilePath path;
    PathService::Get(base::DIR_SOURCE_ROOT, &path);
    path = path.AppendASCII("media")
        .AppendASCII("test")
        .AppendASCII("data")
        .AppendASCII("content")
        .AppendASCII(name.c_str());
    FilePath::StringType raw_path = path.value();
    EXPECT_TRUE(file_util::PathExists(path));

#if defined(OS_WIN)
    std::string ascii_path = WideToASCII(path.value());
#else
    std::string ascii_path = path.value();
#endif

    EXPECT_EQ(0, av_open_input_file(&av_format_context_,
                                    ascii_path.c_str(),
                                    NULL, 0, NULL))
        << "Could not open " << path.value();
    EXPECT_LE(0, av_find_stream_info(av_format_context_))
        << "Could not find stream information for " << path.value();

    // Determine duration by picking max stream duration.
    for (unsigned int i = 0; i < av_format_context_->nb_streams; ++i) {
      AVStream* av_stream = av_format_context_->streams[i];
      int64 duration = ConvertFromTimeBase(av_stream->time_base,
                                        av_stream->duration).InMicroseconds();
      duration_ = std::max(duration_, duration);
    }

    // Final check to see if the container itself specifies a duration.
    AVRational av_time_base = {1, AV_TIME_BASE};
    int64 duration =
        ConvertFromTimeBase(av_time_base,
                         av_format_context_->duration).InMicroseconds();
    duration_ = std::max(duration_, duration);
  }

  void CloseFile() {
    av_close_input_file(av_format_context_);
  }

  void OpenCodecs() {
    for (unsigned int i = 0; i < av_format_context_->nb_streams; ++i) {
      AVStream* av_stream = av_format_context_->streams[i];
      AVCodecContext* av_codec_context = av_stream->codec;
      AVCodec* av_codec = avcodec_find_decoder(av_codec_context->codec_id);

      EXPECT_TRUE(av_codec)
          << "Could not find AVCodec with CodecID "
          << av_codec_context->codec_id;
      EXPECT_EQ(0, avcodec_open(av_codec_context, av_codec))
          << "Could not open AVCodecContext with CodecID "
          << av_codec_context->codec_id;

      if (av_codec->type == AVMEDIA_TYPE_AUDIO) {
        EXPECT_EQ(-1, audio_stream_index_) << "Found multiple audio streams.";
        audio_stream_index_ = static_cast<int>(i);
      } else if (av_codec->type == AVMEDIA_TYPE_VIDEO) {
        EXPECT_EQ(-1, video_stream_index_) << "Found multiple video streams.";
        video_stream_index_ = static_cast<int>(i);
      } else {
        ADD_FAILURE() << "Found unknown stream type.";
      }
    }
  }

  void CloseCodecs() {
    for (unsigned int i = 0; i < av_format_context_->nb_streams; ++i) {
      AVStream* av_stream = av_format_context_->streams[i];
      av_stream->discard = AVDISCARD_ALL;
      avcodec_close(av_stream->codec);
    }
  }

  void Flush() {
    if (has_audio()) {
      audio_packets_.flush();
      avcodec_flush_buffers(av_audio_context());
    }
    if (has_video()) {
      video_packets_.flush();
      avcodec_flush_buffers(av_video_context());
    }
  }

  void ReadUntil(int64 time) {
    while (true) {
      scoped_ptr<AVPacket> packet(new AVPacket());
      if (av_read_frame(av_format_context_, packet.get()) < 0) {
        break;
      }

      int stream_index = static_cast<int>(packet->stream_index);
      int64 packet_time = AV_NOPTS_VALUE;
      if (stream_index == audio_stream_index_) {
        packet_time =
            ConvertFromTimeBase(av_audio_stream()->time_base, packet->pts)
                .InMicroseconds();
        audio_packets_.push(packet.release());
      } else if (stream_index == video_stream_index_) {
        packet_time =
            ConvertFromTimeBase(av_video_stream()->time_base, packet->pts)
                .InMicroseconds();
        video_packets_.push(packet.release());
      } else {
        ADD_FAILURE() << "Found packet that belongs to unknown stream.";
      }

      if (packet_time > time) {
        break;
      }
    }
  }

  void ReadRemainingFile() {
    ReadUntil(std::numeric_limits<int64>::max());
  }

  bool StepDecodeAudio() {
    EXPECT_TRUE(has_audio());
    if (!has_audio() || audio_packets_.empty()) {
      return false;
    }

    // Decode until output is produced, end of stream, or error.
    while (true) {
      int result = 0;
      int size_out = AVCODEC_MAX_AUDIO_FRAME_SIZE;
      bool end_of_stream = false;

      AVPacket packet;
      if (audio_packets_.empty()) {
        av_init_packet(&packet);
        end_of_stream = true;
      } else {
        memcpy(&packet, audio_packets_.peek(), sizeof(packet));
      }

      result = avcodec_decode_audio3(av_audio_context(), audio_buffer_.get(),
                                     &size_out, audio_packets_.peek());
      if (!audio_packets_.empty()) {
        audio_packets_.pop();
      }

      EXPECT_GE(result, 0) << "Audio decode error.";
      if (result < 0 || (size_out == 0 && end_of_stream)) {
        return false;
      }

      if (result > 0) {
        // TODO(scherkus): move this to ffmpeg_common.h and dedup.
        int64 denominator = av_audio_context()->channels *
            av_get_bits_per_sample_fmt(av_audio_context()->sample_fmt) / 8 *
            av_audio_context()->sample_rate;
        double microseconds = size_out /
            (denominator /
             static_cast<double>(base::Time::kMicrosecondsPerSecond));
        decoded_audio_duration_ = static_cast<int64>(microseconds);

        if (packet.pts == static_cast<int64>(AV_NOPTS_VALUE)) {
          EXPECT_NE(decoded_audio_time_, static_cast<int64>(AV_NOPTS_VALUE))
              << "We never received an initial timestamped audio packet! "
              << "Looks like there's a seeking/parsing bug in FFmpeg.";
          decoded_audio_time_ += decoded_audio_duration_;
        } else {
          decoded_audio_time_ =
              ConvertFromTimeBase(av_audio_stream()->time_base, packet.pts)
                  .InMicroseconds();
        }
        return true;
      }
    }
    return true;
  }

  bool StepDecodeVideo() {
    EXPECT_TRUE(has_video());
    if (!has_video() || video_packets_.empty()) {
      return false;
    }

    // Decode until output is produced, end of stream, or error.
    while (true) {
      int result = 0;
      int got_picture = 0;
      bool end_of_stream = false;

      AVPacket packet;
      if (video_packets_.empty()) {
        av_init_packet(&packet);
        end_of_stream = true;
      } else {
        memcpy(&packet, video_packets_.peek(), sizeof(packet));
      }

      av_video_context()->reordered_opaque = packet.pts;
      result = avcodec_decode_video2(av_video_context(), video_buffer_.get(),
                                     &got_picture, &packet);
      if (!video_packets_.empty()) {
        video_packets_.pop();
      }

      EXPECT_GE(result, 0) << "Video decode error.";
      if (result < 0 || (got_picture == 0 && end_of_stream)) {
        return false;
      }

      if (got_picture) {
        AVRational doubled_time_base;
        doubled_time_base.den = av_video_stream()->r_frame_rate.num;
        doubled_time_base.num = av_video_stream()->r_frame_rate.den;
        doubled_time_base.den *= 2;

        decoded_video_time_ =
            ConvertFromTimeBase(av_video_stream()->time_base,
                             video_buffer_->reordered_opaque)
                .InMicroseconds();
        decoded_video_duration_ =
            ConvertFromTimeBase(doubled_time_base,
                             2 + video_buffer_->repeat_pict)
                .InMicroseconds();
        return true;
      }
    }
  }

  void DecodeRemainingAudio() {
    while (StepDecodeAudio()) {}
  }

  void DecodeRemainingVideo() {
    while (StepDecodeVideo()) {}
  }

  void SeekTo(double position) {
    int64 seek_time =
        static_cast<int64>(position * base::Time::kMicrosecondsPerSecond);
    int flags = AVSEEK_FLAG_BACKWARD;

    // Passing -1 as our stream index lets FFmpeg pick a default stream.
    // FFmpeg will attempt to use the lowest-index video stream, if present,
    // followed by the lowest-index audio stream.
    EXPECT_GE(0, av_seek_frame(av_format_context_, -1, seek_time, flags))
        << "Failed to seek to position " << position;
    Flush();
  }

  bool has_audio() { return audio_stream_index_ >= 0; }
  bool has_video() { return video_stream_index_ >= 0; }
  int64 decoded_audio_time() { return decoded_audio_time_; }
  int64 decoded_audio_duration() { return decoded_audio_duration_; }
  int64 decoded_video_time() { return decoded_video_time_; }
  int64 decoded_video_duration() { return decoded_video_duration_; }
  int64 duration() { return duration_; }

  AVFormatContext* av_format_context() {
    return av_format_context_;
  }
  AVStream* av_audio_stream() {
    return av_format_context_->streams[audio_stream_index_];
  }
  AVStream* av_video_stream() {
    return av_format_context_->streams[video_stream_index_];
  }
  AVCodecContext* av_audio_context() {
    return av_audio_stream()->codec;
  }
  AVCodecContext* av_video_context() {
    return av_video_stream()->codec;
  }

 private:
  void InitializeFFmpeg() {
    static bool initialized = false;
    if (initialized) {
      return;
    }

    FilePath path;
    PathService::Get(base::DIR_MODULE, &path);
    EXPECT_TRUE(InitializeMediaLibrary(path))
        << "Could not initialize media library.";

    avcodec_init();
    av_log_set_level(AV_LOG_FATAL);
    av_register_all();
    av_register_protocol2(&kFFmpegFileProtocol, sizeof(kFFmpegFileProtocol));
    initialized = true;
  }

  AVFormatContext* av_format_context_;
  int audio_stream_index_;
  int video_stream_index_;
  AVPacketQueue audio_packets_;
  AVPacketQueue video_packets_;

  scoped_ptr_malloc<int16, media::ScopedPtrAVFree> audio_buffer_;
  scoped_ptr_malloc<AVFrame, media::ScopedPtrAVFree> video_buffer_;

  int64 decoded_audio_time_;
  int64 decoded_audio_duration_;
  int64 decoded_video_time_;
  int64 decoded_video_duration_;
  int64 duration_;

  DISALLOW_COPY_AND_ASSIGN(FFmpegTest);
};

#define FFMPEG_TEST_CASE(name, extension) \
    INSTANTIATE_TEST_CASE_P(name##_##extension, FFmpegTest, \
                            testing::Values(#name "." #extension));

// Covers all our basic formats.
FFMPEG_TEST_CASE(sync0, mp4);
FFMPEG_TEST_CASE(sync0, ogv);
FFMPEG_TEST_CASE(sync0, webm);
FFMPEG_TEST_CASE(sync1, m4a);
FFMPEG_TEST_CASE(sync1, mp3);
FFMPEG_TEST_CASE(sync1, mp4);
FFMPEG_TEST_CASE(sync1, ogg);
FFMPEG_TEST_CASE(sync1, ogv);
FFMPEG_TEST_CASE(sync1, webm);
FFMPEG_TEST_CASE(sync2, m4a);
FFMPEG_TEST_CASE(sync2, mp3);
FFMPEG_TEST_CASE(sync2, mp4);
FFMPEG_TEST_CASE(sync2, ogg);
FFMPEG_TEST_CASE(sync2, ogv);
FFMPEG_TEST_CASE(sync2, webm);

// Covers our LayoutTest file.
FFMPEG_TEST_CASE(counting, ogv);

// The following are bugs reported by users.
FFMPEG_TEST_CASE(crbug47761, ogg);
FFMPEG_TEST_CASE(crbug50045, mp4);
FFMPEG_TEST_CASE(crbug62127, webm);

TEST_P(FFmpegTest, Perf) {
  {
    PerfTimeLogger timer("Opening file");
    OpenFile(GetParam());
  }
  {
    PerfTimeLogger timer("Opening codecs");
    OpenCodecs();
  }
  {
    PerfTimeLogger timer("Reading file");
    ReadRemainingFile();
  }
  if (has_audio()) {
    PerfTimeLogger timer("Decoding audio");
    DecodeRemainingAudio();
  }
  if (has_video()) {
    PerfTimeLogger timer("Decoding video");
    DecodeRemainingVideo();
  }
  {
    PerfTimeLogger timer("Seeking to zero");
    SeekTo(0);
  }
  {
    PerfTimeLogger timer("Closing codecs");
    CloseCodecs();
  }
  {
    PerfTimeLogger timer("Closing file");
    CloseFile();
  }
}

TEST_P(FFmpegTest, Loop_Audio) {
  OpenAndReadFile(GetParam());
  if (!has_audio()) {
    return;
  }

  const int kSteps = 4;
  std::vector<int64> expected_timestamps_;
  for (int i = 0; i < kSteps; ++i) {
    EXPECT_TRUE(StepDecodeAudio());
    expected_timestamps_.push_back(decoded_audio_time());
  }

  SeekTo(0);
  ReadRemainingFile();

  for (int i = 0; i < kSteps; ++i) {
    EXPECT_TRUE(StepDecodeAudio());
    EXPECT_EQ(expected_timestamps_[i], decoded_audio_time())
        << "Frame " << i << " had a mismatched timestamp.";
  }

  CloseCodecs();
  CloseFile();
}

TEST_P(FFmpegTest, Loop_Video) {
  OpenAndReadFile(GetParam());
  if (!has_video()) {
    return;
  }

  const int kSteps = 4;
  std::vector<int64> expected_timestamps_;
  for (int i = 0; i < kSteps; ++i) {
    EXPECT_TRUE(StepDecodeVideo());
    expected_timestamps_.push_back(decoded_video_time());
  }

  SeekTo(0);
  ReadRemainingFile();

  for (int i = 0; i < kSteps; ++i) {
    EXPECT_TRUE(StepDecodeVideo());
    EXPECT_EQ(expected_timestamps_[i], decoded_video_time())
        << "Frame " << i << " had a mismatched timestamp.";
  }

  CloseCodecs();
  CloseFile();
}

TEST_P(FFmpegTest, Seek_Audio) {
  OpenAndReadFile(GetParam());
  if (!has_audio() && duration() >= 0.5) {
    return;
  }

  SeekTo(duration() - 0.5);
  ReadRemainingFile();

  EXPECT_TRUE(StepDecodeAudio());
  EXPECT_NE(static_cast<int64>(AV_NOPTS_VALUE), decoded_audio_time());

  CloseCodecs();
  CloseFile();
}

TEST_P(FFmpegTest, Seek_Video) {
  OpenAndReadFile(GetParam());
  if (!has_video() && duration() >= 0.5) {
    return;
  }

  SeekTo(duration() - 0.5);
  ReadRemainingFile();

  EXPECT_TRUE(StepDecodeVideo());
  EXPECT_NE(static_cast<int64>(AV_NOPTS_VALUE), decoded_video_time());

  CloseCodecs();
  CloseFile();
}

TEST_P(FFmpegTest, Decode_Audio) {
  OpenAndReadFile(GetParam());
  if (!has_audio()) {
    return;
  }

  int64 last_audio_time = AV_NOPTS_VALUE;
  while (StepDecodeAudio()) {
    ASSERT_GT(decoded_audio_time(), last_audio_time);
    last_audio_time = decoded_audio_time();
  }

  CloseCodecs();
  CloseFile();
}

TEST_P(FFmpegTest, Decode_Video) {
  OpenAndReadFile(GetParam());
  if (!has_video()) {
    return;
  }

  int64 last_video_time = AV_NOPTS_VALUE;
  while (StepDecodeVideo()) {
    ASSERT_GT(decoded_video_time(), last_video_time);
    last_video_time = decoded_video_time();
  }

  CloseCodecs();
  CloseFile();
}

TEST_P(FFmpegTest, Duration) {
  OpenAndReadFile(GetParam());

  if (has_audio()) {
    DecodeRemainingAudio();
  }

  if (has_video()) {
    DecodeRemainingVideo();
  }

  double expected = static_cast<double>(duration());
  double actual = static_cast<double>(
      std::max(decoded_audio_time() + decoded_audio_duration(),
               decoded_video_time() + decoded_video_duration()));
  EXPECT_NEAR(expected, actual, 500000)
      << "Duration is off by more than 0.5 seconds.";

  CloseCodecs();
  CloseFile();
}

TEST_F(FFmpegTest, VideoPlayedCollapse) {
  OpenFile("test.ogv");
  OpenCodecs();

  SeekTo(0.5);
  ReadRemainingFile();
  EXPECT_TRUE(StepDecodeVideo());
  VLOG(1) << decoded_video_time();

  SeekTo(2.83);
  ReadRemainingFile();
  EXPECT_TRUE(StepDecodeVideo());
  VLOG(1) << decoded_video_time();

  SeekTo(0.4);
  ReadRemainingFile();
  EXPECT_TRUE(StepDecodeVideo());
  VLOG(1) << decoded_video_time();

  CloseCodecs();
  CloseFile();
}

}  // namespace media
