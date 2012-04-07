// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_unittest_util.h"

#if defined(TOOLKIT_USES_GTK)
#include <gtk/gtk.h>
#include "ui/gfx/gtk_util.h"
#elif defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#include "skia/ext/skia_utils_mac.h"
#endif

namespace {

#if defined(TOOLKIT_VIEWS)
const bool kUsesSkiaNatively = true;
#else
const bool kUsesSkiaNatively = false;
#endif

class ImageTest : public testing::Test {
};

namespace gt = gfx::test;

TEST_F(ImageTest, SkiaToSkia) {
  gfx::Image image(gt::CreateBitmap(25, 25));
  const SkBitmap* bitmap = image.ToSkBitmap();
  EXPECT_TRUE(bitmap);
  EXPECT_FALSE(bitmap->isNull());
  EXPECT_EQ(1U, image.RepresentationCount());

  // Make sure double conversion doesn't happen.
  bitmap = image.ToSkBitmap();
  EXPECT_TRUE(bitmap);
  EXPECT_FALSE(bitmap->isNull());
  EXPECT_EQ(1U, image.RepresentationCount());

  EXPECT_TRUE(image.HasRepresentation(gfx::Image::kImageRepSkia));
  if (!kUsesSkiaNatively)
    EXPECT_FALSE(image.HasRepresentation(gt::GetPlatformRepresentationType()));
}

TEST_F(ImageTest, SkiaToSkiaRef) {
  gfx::Image image(gt::CreateBitmap(25, 25));

  const SkBitmap& bitmap = static_cast<const SkBitmap&>(image);
  EXPECT_FALSE(bitmap.isNull());
  EXPECT_EQ(1U, image.RepresentationCount());

  const SkBitmap* bitmap1 = image.ToSkBitmap();
  EXPECT_FALSE(bitmap1->isNull());
  EXPECT_EQ(1U, image.RepresentationCount());

  EXPECT_TRUE(image.HasRepresentation(gfx::Image::kImageRepSkia));
  if (!kUsesSkiaNatively)
    EXPECT_FALSE(image.HasRepresentation(gt::GetPlatformRepresentationType()));
}

TEST_F(ImageTest, SkiaToPlatform) {
  gfx::Image image(gt::CreateBitmap(25, 25));
  const size_t kRepCount = kUsesSkiaNatively ? 1U : 2U;

  EXPECT_TRUE(image.HasRepresentation(gfx::Image::kImageRepSkia));
  if (!kUsesSkiaNatively)
    EXPECT_FALSE(image.HasRepresentation(gt::GetPlatformRepresentationType()));

  EXPECT_TRUE(gt::ToPlatformType(image));
  EXPECT_EQ(kRepCount, image.RepresentationCount());

  const SkBitmap& bitmap = static_cast<const SkBitmap&>(image);
  EXPECT_FALSE(bitmap.isNull());
  EXPECT_EQ(kRepCount, image.RepresentationCount());

  EXPECT_TRUE(image.HasRepresentation(gfx::Image::kImageRepSkia));
  EXPECT_TRUE(image.HasRepresentation(gt::GetPlatformRepresentationType()));
}

TEST_F(ImageTest, PlatformToSkia) {
  gfx::Image image(gt::CreatePlatformImage());
  const size_t kRepCount = kUsesSkiaNatively ? 1U : 2U;

  EXPECT_TRUE(image.HasRepresentation(gt::GetPlatformRepresentationType()));
  if (!kUsesSkiaNatively)
    EXPECT_FALSE(image.HasRepresentation(gfx::Image::kImageRepSkia));

  const SkBitmap* bitmap = image.ToSkBitmap();
  EXPECT_TRUE(bitmap);
  EXPECT_FALSE(bitmap->isNull());
  EXPECT_EQ(kRepCount, image.RepresentationCount());

  EXPECT_TRUE(gt::ToPlatformType(image));
  EXPECT_EQ(kRepCount, image.RepresentationCount());

  EXPECT_TRUE(image.HasRepresentation(gfx::Image::kImageRepSkia));
}

TEST_F(ImageTest, PlatformToPlatform) {
  gfx::Image image(gt::CreatePlatformImage());
  EXPECT_TRUE(gt::ToPlatformType(image));
  EXPECT_EQ(1U, image.RepresentationCount());

  // Make sure double conversion doesn't happen.
  EXPECT_TRUE(gt::ToPlatformType(image));
  EXPECT_EQ(1U, image.RepresentationCount());

  EXPECT_TRUE(image.HasRepresentation(gt::GetPlatformRepresentationType()));
  if (!kUsesSkiaNatively)
    EXPECT_FALSE(image.HasRepresentation(gfx::Image::kImageRepSkia));
}

TEST_F(ImageTest, PlatformToSkiaToCopy) {
  const SkBitmap* bitmap;

  {
    gfx::Image image(gt::CreatePlatformImage());
    bitmap = image.CopySkBitmap();
  }

  EXPECT_TRUE(bitmap);
  EXPECT_FALSE(bitmap->isNull());

  delete bitmap;
}

#if defined(TOOLKIT_USES_GTK)
TEST_F(ImageTest, SkiaToGdkCopy) {
  GdkPixbuf* pixbuf;

  {
    gfx::Image image(gt::CreateBitmap(25, 25));
    pixbuf = image.CopyGdkPixbuf();
  }

  EXPECT_TRUE(pixbuf);
  g_object_unref(pixbuf);
}

TEST_F(ImageTest, SkiaToCairoCreatesGdk) {
  gfx::Image image(gt::CreateBitmap(25, 25));
  EXPECT_FALSE(image.HasRepresentation(gfx::Image::kImageRepGdk));
  EXPECT_TRUE(image.ToCairo());
  EXPECT_TRUE(image.HasRepresentation(gfx::Image::kImageRepGdk));
}
#endif

#if defined(OS_MACOSX)
TEST_F(ImageTest, SkiaToCocoaCopy) {
  NSImage* ns_image;

  {
    gfx::Image image(gt::CreateBitmap(25, 25));
    ns_image = image.CopyNSImage();
  }

  EXPECT_TRUE(ns_image);
  base::mac::NSObjectRelease(ns_image);
}
#endif

TEST_F(ImageTest, CheckSkiaColor) {
  gfx::Image image(gt::CreatePlatformImage());
  const SkBitmap& bitmap(image);

  SkAutoLockPixels auto_lock(bitmap);
  uint32_t* pixel = bitmap.getAddr32(10, 10);
  EXPECT_EQ(SK_ColorRED, *pixel);
}

TEST_F(ImageTest, SwapRepresentations) {
  const size_t kRepCount = kUsesSkiaNatively ? 1U : 2U;

  gfx::Image image1(gt::CreateBitmap(25, 25));
  const SkBitmap* bitmap1 = image1;
  EXPECT_EQ(1U, image1.RepresentationCount());

  gfx::Image image2(gt::CreatePlatformImage());
  const SkBitmap* bitmap2 = image2;
  gt::PlatformImage platform_image = gt::ToPlatformType(image2);
  EXPECT_EQ(kRepCount, image2.RepresentationCount());

  image1.SwapRepresentations(&image2);

  EXPECT_EQ(bitmap2, image1.ToSkBitmap());
  EXPECT_EQ(platform_image, gt::ToPlatformType(image1));
  EXPECT_EQ(bitmap1, image2.ToSkBitmap());
  EXPECT_EQ(kRepCount, image1.RepresentationCount());
  EXPECT_EQ(1U, image2.RepresentationCount());
}

TEST_F(ImageTest, Copy) {
  const size_t kRepCount = kUsesSkiaNatively ? 1U : 2U;

  gfx::Image image1(gt::CreateBitmap(25, 25));
  gfx::Image image2(image1);

  EXPECT_EQ(1U, image1.RepresentationCount());
  EXPECT_EQ(1U, image2.RepresentationCount());
  EXPECT_EQ(image1.ToSkBitmap(), image2.ToSkBitmap());

  EXPECT_TRUE(gt::ToPlatformType(image2));
  EXPECT_EQ(kRepCount, image2.RepresentationCount());
  EXPECT_EQ(kRepCount, image1.RepresentationCount());
}

TEST_F(ImageTest, Assign) {
  gfx::Image image1(gt::CreatePlatformImage());
  gfx::Image image2 = image1;

  EXPECT_EQ(1U, image1.RepresentationCount());
  EXPECT_EQ(1U, image2.RepresentationCount());
  EXPECT_EQ(image1.ToSkBitmap(), image2.ToSkBitmap());
}

TEST_F(ImageTest, MultiResolutionSkBitmap) {
  const int width1 = 10;
  const int height1 = 12;
  const int width2 = 20;
  const int height2 = 24;

  std::vector<const SkBitmap*> bitmaps;
  bitmaps.push_back(gt::CreateBitmap(width1, height1));
  bitmaps.push_back(gt::CreateBitmap(width2, height2));
  gfx::Image image(bitmaps);

  EXPECT_EQ(1u, image.RepresentationCount());
  EXPECT_EQ(2u, image.GetNumberOfSkBitmaps());

  const SkBitmap* bitmap1 = image.GetSkBitmapAtIndex(0);
  EXPECT_TRUE(bitmap1);
  const SkBitmap* bitmap2 = image.GetSkBitmapAtIndex(1);
  EXPECT_TRUE(bitmap2);

  if (bitmap1->width() == width1) {
    EXPECT_EQ(bitmap1->height(), height1);
    EXPECT_EQ(bitmap2->width(), width2);
    EXPECT_EQ(bitmap2->height(), height2);
  } else {
    EXPECT_EQ(bitmap1->width(), width2);
    EXPECT_EQ(bitmap1->height(), height2);
    EXPECT_EQ(bitmap2->width(), width1);
    EXPECT_EQ(bitmap2->height(), height1);
  }

  // Sanity check.
  EXPECT_EQ(1u, image.RepresentationCount());
  EXPECT_EQ(2u, image.GetNumberOfSkBitmaps());
}

// Integration tests with UI toolkit frameworks require linking against the
// Views library and cannot be here (gfx_unittests doesn't include it). They
// instead live in /chrome/browser/ui/tests/ui_gfx_image_unittest.cc.

}  // namespace
