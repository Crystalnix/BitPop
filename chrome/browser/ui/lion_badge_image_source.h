// Copyright (c) 2013 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_LION_BADGE_IMAGE_SOURCE_H_
#define CHROME_BROWSER_UI_LION_BADGE_IMAGE_SOURCE_H_

#include <string>

#include "chrome/common/extensions/extension.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image_skia.h"

namespace gfx {
class Size;
}

// CanvasImageSource for creating a badge.
class LionBadgeImageSource
    : public gfx::CanvasImageSource {
 public:
  LionBadgeImageSource(const gfx::Size& icon_size,
                       const std::string& text);
  virtual ~LionBadgeImageSource();

 private:
  static int actual_width(const gfx::Size& icon_size,
                          const std::string& text);

  virtual void Draw(gfx::Canvas* canvas) OVERRIDE;

    // Text to be displayed on the badge.
  std::string text_;
  gfx::Size icon_size_;

  DISALLOW_COPY_AND_ASSIGN(LionBadgeImageSource);
};

#endif  // CHROME_BROWSER_UI_LION_BADGE_IMAGE_SOURCE_H_
