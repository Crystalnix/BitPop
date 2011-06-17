// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "remoting/base/codec_test.h"
#include "remoting/base/encoder_vp8.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const int kIntMax = std::numeric_limits<int>::max();

}  // namespace

namespace remoting {

TEST(EncoderVp8Test, TestEncoder) {
  EncoderVp8 encoder;
  TestEncoder(&encoder, false);
}

TEST(EncoderVp8Test, AlignAndClipRect) {
  // Simple test case (no clipping).
  gfx::Rect r1(100, 200, 300, 400);
  EXPECT_EQ(EncoderVp8::AlignAndClipRect(r1, kIntMax, kIntMax), r1);

  // Should expand outward to r1.
  gfx::Rect r2(101, 201, 298, 398);
  EXPECT_EQ(EncoderVp8::AlignAndClipRect(r2, kIntMax, kIntMax), r1);

  // Test clipping to screen size.
  EXPECT_EQ(EncoderVp8::AlignAndClipRect(r1, 110, 220),
            gfx::Rect(100, 200, 10, 20));

  // Rectangle completely off-screen.
  EXPECT_TRUE(EncoderVp8::AlignAndClipRect(r1, 50, 50).IsEmpty());

  // Clipping to odd-sized screen.  An unlikely case, and we might not deal
  // with it cleanly in the encoder (we possibly lose 1px at right & bottom
  // of screen).
  EXPECT_EQ(EncoderVp8::AlignAndClipRect(r1, 199, 299),
            gfx::Rect(100, 200, 98, 98));
}

}  // namespace remoting
