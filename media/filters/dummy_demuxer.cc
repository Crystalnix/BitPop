// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/filters/dummy_demuxer.h"

#include "base/logging.h"

namespace media {

DummyDemuxerStream::DummyDemuxerStream(Type type)
    : type_(type) {
}

DummyDemuxerStream::~DummyDemuxerStream() {}

DemuxerStream::Type DummyDemuxerStream::type() {
  return type_;
}

const AudioDecoderConfig& DummyDemuxerStream::audio_decoder_config() {
  CHECK_EQ(type_, AUDIO);
  return audio_config_;
}

const VideoDecoderConfig& DummyDemuxerStream::video_decoder_config() {
  CHECK_EQ(type_, VIDEO);
  return video_config_;
}

void DummyDemuxerStream::Read(const ReadCB& read_cb) {}

void DummyDemuxerStream::EnableBitstreamConverter() {}

DummyDemuxer::DummyDemuxer(bool has_video, bool has_audio) {
  streams_.resize(DemuxerStream::NUM_TYPES);
  if (has_audio)
    streams_[DemuxerStream::AUDIO] =
        new DummyDemuxerStream(DemuxerStream::AUDIO);
  if (has_video)
    streams_[DemuxerStream::VIDEO] =
        new DummyDemuxerStream(DemuxerStream::VIDEO);
}

void DummyDemuxer::Initialize(DemuxerHost* host,
                              const PipelineStatusCB& status_cb) {
  host->SetDuration(media::kInfiniteDuration());
  status_cb.Run(PIPELINE_OK);
}

scoped_refptr<DemuxerStream> DummyDemuxer::GetStream(DemuxerStream::Type type) {
  return streams_[type];
}

base::TimeDelta DummyDemuxer::GetStartTime() const {
  return base::TimeDelta();
}

DummyDemuxer::~DummyDemuxer() {}

}  // namespace media
