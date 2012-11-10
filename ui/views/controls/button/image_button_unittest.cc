// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/layout.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/test/views_test_base.h"

namespace {

gfx::ImageSkia CreateTestImage(int width, int height) {
  SkBitmap bitmap;
  bitmap.setConfig(SkBitmap::kARGB_8888_Config, width, height);
  bitmap.allocPixels();
  return gfx::ImageSkia(bitmap);
}

}  // namespace

namespace views {

typedef ViewsTestBase ImageButtonTest;

TEST_F(ImageButtonTest, Basics) {
  ImageButton button(NULL);

  // Our image to paint starts empty.
  EXPECT_TRUE(button.GetImageToPaint().empty());

  // Without a theme, buttons are 16x14 by default.
  EXPECT_EQ("16x14", button.GetPreferredSize().ToString());

  // We can set a preferred size when we have no image.
  button.SetPreferredSize(gfx::Size(5, 15));
  EXPECT_EQ("5x15", button.GetPreferredSize().ToString());

  // Set a normal image.
  gfx::ImageSkia normal_image = CreateTestImage(10, 20);
  button.SetImage(CustomButton::BS_NORMAL, &normal_image);

  // Image uses normal image for painting.
  EXPECT_FALSE(button.GetImageToPaint().empty());
  EXPECT_EQ(10, button.GetImageToPaint().width());
  EXPECT_EQ(20, button.GetImageToPaint().height());

  // Preferred size is the normal button size.
  EXPECT_EQ("10x20", button.GetPreferredSize().ToString());

  // Set a pushed image.
  gfx::ImageSkia pushed_image = CreateTestImage(11, 21);
  button.SetImage(CustomButton::BS_PUSHED, &pushed_image);

  // By convention, preferred size doesn't change, even though pushed image
  // is bigger.
  EXPECT_EQ("10x20", button.GetPreferredSize().ToString());

  // We're still painting the normal image.
  EXPECT_FALSE(button.GetImageToPaint().empty());
  EXPECT_EQ(10, button.GetImageToPaint().width());
  EXPECT_EQ(20, button.GetImageToPaint().height());

  // Set an overlay image.
  gfx::ImageSkia overlay_image = CreateTestImage(12, 22);
  button.SetOverlayImage(&overlay_image);
  EXPECT_EQ(12, button.overlay_image_.width());
  EXPECT_EQ(22, button.overlay_image_.height());

  // By convention, preferred size doesn't change, even though pushed image
  // is bigger.
  EXPECT_EQ("10x20", button.GetPreferredSize().ToString());

  // We're still painting the normal image.
  EXPECT_FALSE(button.GetImageToPaint().empty());
  EXPECT_EQ(10, button.GetImageToPaint().width());
  EXPECT_EQ(20, button.GetImageToPaint().height());

  // Reset the overlay image.
  button.SetOverlayImage(NULL);
  EXPECT_TRUE(button.overlay_image_.empty());
}

}  // namespace views
