// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_BASE_CODEC_TEST_H_
#define REMOTING_BASE_CODEC_TEST_H_

#include "base/memory/ref_counted.h"
#include "media/base/video_frame.h"
#include "remoting/base/capture_data.h"

namespace remoting {

class Decoder;
class Encoder;

// Generate test data and test the encoder for a regular encoding sequence.
// This will test encoder test and the sequence of messages sent.
//
// If |strict| is set to true then this routine will make sure the updated
// rects match dirty rects.
void TestEncoder(Encoder* encoder, bool strict);

// Generate test data and test the encoder and decoder pair.
//
// If |strict| is set to true, this routine will make sure the updated rects
// are correct.
void TestEncoderDecoder(Encoder* encoder, Decoder* decoder, bool strict);

// Generate a frame containing a gradient, and test the encoder and decoder
// pair.
void TestEncoderDecoderGradient(Encoder* encoder, Decoder* decoder,
                                const SkISize& screen_size,
                                const SkISize& view_size,
                                double max_error_limit,
                                double mean_error_limit);

}  // namespace remoting

#endif  // REMOTING_BASE_CODEC_TEST_H_
