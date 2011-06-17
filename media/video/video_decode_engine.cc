// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/video/video_decode_engine.h"

#include "base/logging.h"

namespace media {

VideoCodecConfig::VideoCodecConfig(VideoCodec codec,
                                   int width,
                                   int height,
                                   int frame_rate_numerator,
                                   int frame_rate_denominator,
                                   uint8* extra_data,
                                   size_t extra_data_size)
    : codec_(codec),
      width_(width),
      height_(height),
      frame_rate_numerator_(frame_rate_numerator),
      frame_rate_denominator_(frame_rate_denominator),
      extra_data_size_(extra_data_size) {
  CHECK(extra_data_size_ == 0 || extra_data);
  if (extra_data_size_ > 0) {
    extra_data_.reset(new uint8[extra_data_size_]);
    memcpy(extra_data_.get(), extra_data, extra_data_size_);
  }
}

VideoCodecConfig::~VideoCodecConfig() {}

VideoCodec VideoCodecConfig::codec() const {
  return codec_;
}

int VideoCodecConfig::width() const {
  return width_;
}

int VideoCodecConfig::height() const {
  return height_;
}

int VideoCodecConfig::frame_rate_numerator() const {
  return frame_rate_numerator_;
}

int VideoCodecConfig::frame_rate_denominator() const {
  return frame_rate_denominator_;
}

uint8* VideoCodecConfig::extra_data() const {
  return extra_data_.get();
}

size_t VideoCodecConfig::extra_data_size() const {
  return extra_data_size_;
}

}  // namespace media
