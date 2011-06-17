// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/WebKit/Source/WebKit/chromium/public/WebImageDecoder.h"
#include "webkit/tools/test_shell/image_decoder_unittest.h"

class BMPImageDecoderTest : public ImageDecoderTest {
 public:
  BMPImageDecoderTest() : ImageDecoderTest("bmp") { }

 protected:
  virtual WebKit::WebImageDecoder* CreateWebKitImageDecoder() const {
    return new WebKit::WebImageDecoder(WebKit::WebImageDecoder::TypeBMP);
  }

  // The BMPImageDecoderTest tests are really slow under Valgrind.
  // Thus it is split into fast and slow versions. The threshold is
  // set to 10KB because the fast test can finish under Valgrind in
  // less than 30 seconds.
  static const int64 kThresholdSize = 10240;
};

TEST_F(BMPImageDecoderTest, DecodingFast) {
  TestDecoding(TEST_SMALLER, kThresholdSize);
}

TEST_F(BMPImageDecoderTest, DecodingSlow) {
  TestDecoding(TEST_BIGGER, kThresholdSize);
}
