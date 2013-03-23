// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/web_ui_util.h"

#include "googleurl/src/gurl.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(WebUIUtilTest, ParsePathAndScale) {
  GURL url("chrome://some/random/username@email/and/more");
  std::string path;
  ui::ScaleFactor factor;

  web_ui_util::ParsePathAndScale(url, &path, &factor);
  EXPECT_EQ("random/username@email/and/more", path);
  EXPECT_EQ(ui::SCALE_FACTOR_100P, factor);

  GURL url2("chrome://some/random/username@email/and/more@2x");
  web_ui_util::ParsePathAndScale(url2, &path, &factor);
  EXPECT_EQ("random/username@email/and/more", path);
  EXPECT_EQ(ui::SCALE_FACTOR_200P, factor);

  GURL url3("chrome://some/random/username/and/more@2x");
  web_ui_util::ParsePathAndScale(url3, &path, &factor);
  EXPECT_EQ("random/username/and/more", path);
  EXPECT_EQ(ui::SCALE_FACTOR_200P, factor);

  GURL url4("chrome://some/random/username/and/more");
  web_ui_util::ParsePathAndScale(url4, &path, &factor);
  EXPECT_EQ("random/username/and/more", path);
  EXPECT_EQ(ui::SCALE_FACTOR_100P, factor);
}
