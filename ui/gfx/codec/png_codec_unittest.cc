// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cmath>

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkUnPreMultiply.h"
#include "third_party/zlib/zlib.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/size.h"

namespace gfx {

static void MakeRGBImage(int w, int h, std::vector<unsigned char>* dat) {
  dat->resize(w * h * 3);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      unsigned char* org_px = &(*dat)[(y * w + x) * 3];
      org_px[0] = x * 3;      // r
      org_px[1] = x * 3 + 1;  // g
      org_px[2] = x * 3 + 2;  // b
    }
  }
}

// Set use_transparency to write data into the alpha channel, otherwise it will
// be filled with 0xff. With the alpha channel stripped, this should yield the
// same image as MakeRGBImage above, so the code below can make reference
// images for conversion testing.
static void MakeRGBAImage(int w, int h, bool use_transparency,
                          std::vector<unsigned char>* dat) {
  dat->resize(w * h * 4);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      unsigned char* org_px = &(*dat)[(y * w + x) * 4];
      org_px[0] = x * 3;      // r
      org_px[1] = x * 3 + 1;  // g
      org_px[2] = x * 3 + 2;  // b
      if (use_transparency)
        org_px[3] = x*3 + 3;  // a
      else
        org_px[3] = 0xFF;     // a (opaque)
    }
  }
}

// Returns true if each channel of the given two colors are "close." This is
// used for comparing colors where rounding errors may cause off-by-one.
bool ColorsClose(uint32_t a, uint32_t b) {
  return abs(static_cast<int>(SkColorGetB(a) - SkColorGetB(b))) < 2 &&
         abs(static_cast<int>(SkColorGetG(a) - SkColorGetG(b))) < 2 &&
         abs(static_cast<int>(SkColorGetR(a) - SkColorGetR(b))) < 2 &&
         abs(static_cast<int>(SkColorGetA(a) - SkColorGetA(b))) < 2;
}

// Returns true if the RGB components are "close."
bool NonAlphaColorsClose(uint32_t a, uint32_t b) {
  return abs(static_cast<int>(SkColorGetB(a) - SkColorGetB(b))) < 2 &&
         abs(static_cast<int>(SkColorGetG(a) - SkColorGetG(b))) < 2 &&
         abs(static_cast<int>(SkColorGetR(a) - SkColorGetR(b))) < 2;
}

void MakeTestSkBitmap(int w, int h, SkBitmap* bmp) {
  bmp->setConfig(SkBitmap::kARGB_8888_Config, w, h);
  bmp->allocPixels();

  uint32_t* src_data = bmp->getAddr32(0, 0);
  for (int i = 0; i < w * h; i++) {
    src_data[i] = SkPreMultiplyARGB(i % 255, i % 250, i % 245, i % 240);
  }
}

TEST(PNGCodec, EncodeDecodeRGB) {
  const int w = 20, h = 20;

  // create an image with known values
  std::vector<unsigned char> original;
  MakeRGBImage(w, h, &original);

  // encode
  std::vector<unsigned char> encoded;
  EXPECT_TRUE(PNGCodec::Encode(&original[0], PNGCodec::FORMAT_RGB,
                               Size(w, h), w * 3, false,
                               std::vector<PNGCodec::Comment>(),
                               &encoded));

  // decode, it should have the same size as the original
  std::vector<unsigned char> decoded;
  int outw, outh;
  EXPECT_TRUE(PNGCodec::Decode(&encoded[0], encoded.size(),
                               PNGCodec::FORMAT_RGB, &decoded,
                               &outw, &outh));
  ASSERT_EQ(w, outw);
  ASSERT_EQ(h, outh);
  ASSERT_EQ(original.size(), decoded.size());

  // Images must be equal
  ASSERT_TRUE(original == decoded);
}

TEST(PNGCodec, EncodeDecodeRGBA) {
  const int w = 20, h = 20;

  // create an image with known values, a must be opaque because it will be
  // lost during encoding
  std::vector<unsigned char> original;
  MakeRGBAImage(w, h, true, &original);

  // encode
  std::vector<unsigned char> encoded;
  EXPECT_TRUE(PNGCodec::Encode(&original[0], PNGCodec::FORMAT_RGBA,
                               Size(w, h), w * 4, false,
                               std::vector<PNGCodec::Comment>(),
                               &encoded));

  // decode, it should have the same size as the original
  std::vector<unsigned char> decoded;
  int outw, outh;
  EXPECT_TRUE(PNGCodec::Decode(&encoded[0], encoded.size(),
                               PNGCodec::FORMAT_RGBA, &decoded,
                               &outw, &outh));
  ASSERT_EQ(w, outw);
  ASSERT_EQ(h, outh);
  ASSERT_EQ(original.size(), decoded.size());

  // Images must be exactly equal
  ASSERT_TRUE(original == decoded);
}

// Test that corrupted data decompression causes failures.
TEST(PNGCodec, DecodeCorrupted) {
  int w = 20, h = 20;

  // Make some random data (an uncompressed image).
  std::vector<unsigned char> original;
  MakeRGBImage(w, h, &original);

  // It should fail when given non-JPEG compressed data.
  std::vector<unsigned char> output;
  int outw, outh;
  EXPECT_FALSE(PNGCodec::Decode(&original[0], original.size(),
                                PNGCodec::FORMAT_RGB, &output,
                                &outw, &outh));

  // Make some compressed data.
  std::vector<unsigned char> compressed;
  EXPECT_TRUE(PNGCodec::Encode(&original[0], PNGCodec::FORMAT_RGB,
                               Size(w, h), w * 3, false,
                               std::vector<PNGCodec::Comment>(),
                               &compressed));

  // Try decompressing a truncated version.
  EXPECT_FALSE(PNGCodec::Decode(&compressed[0], compressed.size() / 2,
                                PNGCodec::FORMAT_RGB, &output,
                                &outw, &outh));

  // Corrupt it and try decompressing that.
  for (int i = 10; i < 30; i++)
    compressed[i] = i;
  EXPECT_FALSE(PNGCodec::Decode(&compressed[0], compressed.size(),
                                PNGCodec::FORMAT_RGB, &output,
                                &outw, &outh));
}

TEST(PNGCodec, EncodeDecodeBGRA) {
  const int w = 20, h = 20;

  // Create an image with known values, alpha must be opaque because it will be
  // lost during encoding.
  std::vector<unsigned char> original;
  MakeRGBAImage(w, h, true, &original);

  // Encode.
  std::vector<unsigned char> encoded;
  EXPECT_TRUE(PNGCodec::Encode(&original[0], PNGCodec::FORMAT_BGRA,
                               Size(w, h), w * 4, false,
                               std::vector<PNGCodec::Comment>(),
                               &encoded));

  // Decode, it should have the same size as the original.
  std::vector<unsigned char> decoded;
  int outw, outh;
  EXPECT_TRUE(PNGCodec::Decode(&encoded[0], encoded.size(),
                               PNGCodec::FORMAT_BGRA, &decoded,
                               &outw, &outh));
  ASSERT_EQ(w, outw);
  ASSERT_EQ(h, outh);
  ASSERT_EQ(original.size(), decoded.size());

  // Images must be exactly equal.
  ASSERT_TRUE(original == decoded);
}

TEST(PNGCodec, StripAddAlpha) {
  const int w = 20, h = 20;

  // These should be the same except one has a 0xff alpha channel.
  std::vector<unsigned char> original_rgb;
  MakeRGBImage(w, h, &original_rgb);
  std::vector<unsigned char> original_rgba;
  MakeRGBAImage(w, h, false, &original_rgba);

  // Encode RGBA data as RGB.
  std::vector<unsigned char> encoded;
  EXPECT_TRUE(PNGCodec::Encode(&original_rgba[0], PNGCodec::FORMAT_RGBA,
                               Size(w, h), w * 4, true,
                               std::vector<PNGCodec::Comment>(),
                               &encoded));

  // Decode the RGB to RGBA.
  std::vector<unsigned char> decoded;
  int outw, outh;
  EXPECT_TRUE(PNGCodec::Decode(&encoded[0], encoded.size(),
                               PNGCodec::FORMAT_RGBA, &decoded,
                               &outw, &outh));

  // Decoded and reference should be the same (opaque alpha).
  ASSERT_EQ(w, outw);
  ASSERT_EQ(h, outh);
  ASSERT_EQ(original_rgba.size(), decoded.size());
  ASSERT_TRUE(original_rgba == decoded);

  // Encode RGBA to RGBA.
  EXPECT_TRUE(PNGCodec::Encode(&original_rgba[0], PNGCodec::FORMAT_RGBA,
                               Size(w, h), w * 4, false,
                               std::vector<PNGCodec::Comment>(),
                               &encoded));

  // Decode the RGBA to RGB.
  EXPECT_TRUE(PNGCodec::Decode(&encoded[0], encoded.size(),
                               PNGCodec::FORMAT_RGB, &decoded,
                               &outw, &outh));

  // It should be the same as our non-alpha-channel reference.
  ASSERT_EQ(w, outw);
  ASSERT_EQ(h, outh);
  ASSERT_EQ(original_rgb.size(), decoded.size());
  ASSERT_TRUE(original_rgb == decoded);
}

TEST(PNGCodec, EncodeBGRASkBitmap) {
  const int w = 20, h = 20;

  SkBitmap original_bitmap;
  MakeTestSkBitmap(w, h, &original_bitmap);

  // Encode the bitmap.
  std::vector<unsigned char> encoded;
  PNGCodec::EncodeBGRASkBitmap(original_bitmap, false, &encoded);

  // Decode the encoded string.
  SkBitmap decoded_bitmap;
  EXPECT_TRUE(PNGCodec::Decode(&encoded.front(), encoded.size(),
                               &decoded_bitmap));

  // Compare the original bitmap and the output bitmap. We use ColorsClose
  // as SkBitmaps are considered to be pre-multiplied, the unpremultiplication
  // (in Encode) and repremultiplication (in Decode) can be lossy.
  for (int x = 0; x < w; x++) {
    for (int y = 0; y < h; y++) {
      uint32_t original_pixel = original_bitmap.getAddr32(0, y)[x];
      uint32_t decoded_pixel = decoded_bitmap.getAddr32(0, y)[x];
      EXPECT_TRUE(ColorsClose(original_pixel, decoded_pixel));
    }
  }
}

TEST(PNGCodec, EncodeBGRASkBitmapDiscardTransparency) {
  const int w = 20, h = 20;

  SkBitmap original_bitmap;
  MakeTestSkBitmap(w, h, &original_bitmap);

  // Encode the bitmap.
  std::vector<unsigned char> encoded;
  PNGCodec::EncodeBGRASkBitmap(original_bitmap, true, &encoded);

  // Decode the encoded string.
  SkBitmap decoded_bitmap;
  EXPECT_TRUE(PNGCodec::Decode(&encoded.front(), encoded.size(),
                               &decoded_bitmap));

  // Compare the original bitmap and the output bitmap. We need to
  // unpremultiply original_pixel, as the decoded bitmap doesn't have an alpha
  // channel.
  for (int x = 0; x < w; x++) {
    for (int y = 0; y < h; y++) {
      uint32_t original_pixel = original_bitmap.getAddr32(0, y)[x];
      uint32_t unpremultiplied =
          SkUnPreMultiply::PMColorToColor(original_pixel);
      uint32_t decoded_pixel = decoded_bitmap.getAddr32(0, y)[x];
      EXPECT_TRUE(NonAlphaColorsClose(unpremultiplied, decoded_pixel))
          << "Original_pixel: ("
          << SkColorGetR(unpremultiplied) << ", "
          << SkColorGetG(unpremultiplied) << ", "
          << SkColorGetB(unpremultiplied) << "), "
          << "Decoded pixel: ("
          << SkColorGetR(decoded_pixel) << ", "
          << SkColorGetG(decoded_pixel) << ", "
          << SkColorGetB(decoded_pixel) << ")";
    }
  }
}

TEST(PNGCodec, EncodeWithComment) {
  const int w = 10, h = 10;

  std::vector<unsigned char> original;
  MakeRGBImage(w, h, &original);

  std::vector<unsigned char> encoded;
  std::vector<PNGCodec::Comment> comments;
  comments.push_back(PNGCodec::Comment("key", "text"));
  comments.push_back(PNGCodec::Comment("test", "something"));
  comments.push_back(PNGCodec::Comment("have some", "spaces in both"));
  EXPECT_TRUE(PNGCodec::Encode(&original[0], PNGCodec::FORMAT_RGB,
                               Size(w, h), w * 3, false, comments, &encoded));

  // Each chunk is of the form length (4 bytes), chunk type (tEXt), data,
  // checksum (4 bytes).  Make sure we find all of them in the encoded
  // results.
  const unsigned char kExpected1[] =
      "\x00\x00\x00\x08tEXtkey\x00text\x9e\xe7\x66\x51";
  const unsigned char kExpected2[] =
      "\x00\x00\x00\x0etEXttest\x00something\x29\xba\xef\xac";
  const unsigned char kExpected3[] =
      "\x00\x00\x00\x18tEXthave some\x00spaces in both\x8d\x69\x34\x2d";

  EXPECT_NE(std::search(encoded.begin(), encoded.end(), kExpected1,
                        kExpected1 + arraysize(kExpected1)),
            encoded.end());
  EXPECT_NE(std::search(encoded.begin(), encoded.end(), kExpected2,
                        kExpected2 + arraysize(kExpected2)),
            encoded.end());
  EXPECT_NE(std::search(encoded.begin(), encoded.end(), kExpected3,
                        kExpected3 + arraysize(kExpected3)),
            encoded.end());
}

TEST(PNGCodec, EncodeDecodeWithVaryingCompressionLevels) {
  const int w = 20, h = 20;

  // create an image with known values, a must be opaque because it will be
  // lost during encoding
  std::vector<unsigned char> original;
  MakeRGBAImage(w, h, true, &original);

  // encode
  std::vector<unsigned char> encoded_fast;
  EXPECT_TRUE(PNGCodec::EncodeWithCompressionLevel(
        &original[0], PNGCodec::FORMAT_RGBA, Size(w, h), w * 4, false,
        std::vector<PNGCodec::Comment>(), Z_BEST_SPEED, &encoded_fast));

  std::vector<unsigned char> encoded_best;
  EXPECT_TRUE(PNGCodec::EncodeWithCompressionLevel(
        &original[0], PNGCodec::FORMAT_RGBA, Size(w, h), w * 4, false,
        std::vector<PNGCodec::Comment>(), Z_BEST_COMPRESSION, &encoded_best));

  // Make sure the different compression settings actually do something; the
  // sizes should be different.
  EXPECT_NE(encoded_fast.size(), encoded_best.size());

  // decode, it should have the same size as the original
  std::vector<unsigned char> decoded;
  int outw, outh;
  EXPECT_TRUE(PNGCodec::Decode(&encoded_fast[0], encoded_fast.size(),
                               PNGCodec::FORMAT_RGBA, &decoded,
                               &outw, &outh));
  ASSERT_EQ(w, outw);
  ASSERT_EQ(h, outh);
  ASSERT_EQ(original.size(), decoded.size());

  EXPECT_TRUE(PNGCodec::Decode(&encoded_best[0], encoded_best.size(),
                               PNGCodec::FORMAT_RGBA, &decoded,
                               &outw, &outh));
  ASSERT_EQ(w, outw);
  ASSERT_EQ(h, outh);
  ASSERT_EQ(original.size(), decoded.size());

  // Images must be exactly equal
  ASSERT_TRUE(original == decoded);
}


}  // namespace gfx
