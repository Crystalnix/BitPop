// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <queue>

#include "base/bind.h"
#include "base/message_loop.h"
#include "base/stringprintf.h"
#include "media/base/mock_callback.h"
#include "media/base/mock_filters.h"
#include "media/base/test_data_util.h"
#include "media/ffmpeg/ffmpeg_common.h"
#include "media/filters/ffmpeg_audio_decoder.h"
#include "media/filters/ffmpeg_glue.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::ReturnRef;
using ::testing::StrictMock;

namespace media {

ACTION_P(InvokeReadPacket, test) {
  test->ReadPacket(arg0);
}

class FFmpegAudioDecoderTest : public testing::Test {
 public:
  FFmpegAudioDecoderTest()
      : decoder_(new FFmpegAudioDecoder(&message_loop_)),
        demuxer_(new StrictMock<MockDemuxerStream>()) {
    CHECK(FFmpegGlue::GetInstance());

    ReadTestDataFile("vorbis-extradata",
                     &vorbis_extradata_,
                     &vorbis_extradata_size_);

    // Refer to media/test/data/README for details on vorbis test data.
    for (int i = 0; i < 4; ++i) {
      scoped_refptr<Buffer> buffer;
      ReadTestDataFile(base::StringPrintf("vorbis-packet-%d", i), &buffer);

      if (i < 3) {
        buffer->SetTimestamp(base::TimeDelta());
      } else {
        buffer->SetTimestamp(base::TimeDelta::FromMicroseconds(2902));
      }

      buffer->SetDuration(base::TimeDelta());
      encoded_audio_.push_back(buffer);
    }

    // Push in an EOS buffer.
    encoded_audio_.push_back(new DataBuffer(0));

    config_.Initialize(kCodecVorbis,
                       16,
                       CHANNEL_LAYOUT_STEREO,
                       44100,
                       vorbis_extradata_.get(),
                       vorbis_extradata_size_,
                       true);
  }

  virtual ~FFmpegAudioDecoderTest() {}

  void Initialize() {
    EXPECT_CALL(*demuxer_, audio_decoder_config())
        .WillOnce(ReturnRef(config_));

    decoder_->Initialize(demuxer_,
                         NewExpectedClosure(),
                         base::Bind(&MockStatisticsCallback::OnStatistics,
                                    base::Unretained(&statistics_callback_)));

    message_loop_.RunAllPending();
  }

  void Stop() {
    decoder_->Stop(NewExpectedClosure());
    message_loop_.RunAllPending();
  }

  void ReadPacket(const DemuxerStream::ReadCallback& read_callback) {
    CHECK(!encoded_audio_.empty()) << "ReadPacket() called too many times";

    scoped_refptr<Buffer> buffer(encoded_audio_.front());
    encoded_audio_.pop_front();
    read_callback.Run(buffer);
  }

  void Read() {
    decoder_->Read(base::Bind(
        &FFmpegAudioDecoderTest::DecodeFinished, base::Unretained(this)));
    message_loop_.RunAllPending();
  }

  void DecodeFinished(scoped_refptr<Buffer> buffer) {
    decoded_audio_.push_back(buffer);
  }

  void ExpectDecodedAudio(size_t i, int64 timestamp, int64 duration) {
    EXPECT_LT(i, decoded_audio_.size());
    EXPECT_EQ(timestamp, decoded_audio_[i]->GetTimestamp().InMicroseconds());
    EXPECT_EQ(duration, decoded_audio_[i]->GetDuration().InMicroseconds());
    EXPECT_FALSE(decoded_audio_[i]->IsEndOfStream());
  }

  void ExpectEndOfStream(size_t i) {
    EXPECT_LT(i, decoded_audio_.size());
    EXPECT_EQ(0, decoded_audio_[i]->GetTimestamp().InMicroseconds());
    EXPECT_EQ(0, decoded_audio_[i]->GetDuration().InMicroseconds());
    EXPECT_TRUE(decoded_audio_[i]->IsEndOfStream());
  }

  MessageLoop message_loop_;
  scoped_refptr<FFmpegAudioDecoder> decoder_;
  scoped_refptr<StrictMock<MockDemuxerStream> > demuxer_;
  MockStatisticsCallback statistics_callback_;

  scoped_array<uint8> vorbis_extradata_;
  int vorbis_extradata_size_;

  std::deque<scoped_refptr<Buffer> > encoded_audio_;
  std::deque<scoped_refptr<Buffer> > decoded_audio_;

  AudioDecoderConfig config_;
};

TEST_F(FFmpegAudioDecoderTest, Initialize) {
  Initialize();

  EXPECT_EQ(16, decoder_->bits_per_channel());
  EXPECT_EQ(CHANNEL_LAYOUT_STEREO, decoder_->channel_layout());
  EXPECT_EQ(44100, decoder_->samples_per_second());

  Stop();
}

TEST_F(FFmpegAudioDecoderTest, Flush) {
  Initialize();

  decoder_->Flush(NewExpectedClosure());
  message_loop_.RunAllPending();

  Stop();
}

TEST_F(FFmpegAudioDecoderTest, ProduceAudioSamples) {
  Initialize();

  // Vorbis requires N+1 packets to produce audio data for N packets.
  //
  // This will should result in the demuxer receiving three reads for two
  // requests to produce audio samples.
  EXPECT_CALL(*demuxer_, Read(_))
      .Times(5)
      .WillRepeatedly(InvokeReadPacket(this));
  EXPECT_CALL(statistics_callback_, OnStatistics(_))
      .Times(5);

  Read();
  Read();
  Read();

  // We should have three decoded audio buffers.
  //
  // TODO(scherkus): timestamps are off by one packet due to decoder delay.
  ASSERT_EQ(3u, decoded_audio_.size());
  ExpectDecodedAudio(0, 0, 2902);
  ExpectDecodedAudio(1, 0, 13061);
  ExpectDecodedAudio(2, 2902, 23219);

  // Call one more time to trigger EOS.
  Read();
  ASSERT_EQ(4u, decoded_audio_.size());
  ExpectEndOfStream(3);

  Stop();
}

TEST_F(FFmpegAudioDecoderTest, ReadAbort) {
  Initialize();

  encoded_audio_.clear();
  encoded_audio_.push_back(NULL);

  EXPECT_CALL(*demuxer_, Read(_))
      .WillOnce(InvokeReadPacket(this));
  Read();

  EXPECT_EQ(decoded_audio_.size(), 1u);
  EXPECT_TRUE(decoded_audio_[0].get() ==  NULL);

  Stop();
}

}  // namespace media
