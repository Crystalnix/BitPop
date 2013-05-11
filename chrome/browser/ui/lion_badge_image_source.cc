// Copyright (c) 2013 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/lion_badge_image_source.h"

#include "chrome/common/badge_util.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/rect.h"

LionBadgeImageSource::LionBadgeImageSource(const gfx::Size& icon_size,
    const std::string& text)
        : gfx::CanvasImageSource(
            gfx::Size(actual_width(icon_size, text), icon_size.height()),
            false),
          text_(text),
          icon_size_(icon_size) {
}

LionBadgeImageSource::~LionBadgeImageSource() {}

void LionBadgeImageSource::Draw(gfx::Canvas* canvas) {
  gfx::Rect bounds(icon_size_.width(), icon_size_.height());
  canvas->Save();
  gfx::Rect badge_rect = badge_util::BadgeRect(bounds,
                                               text_,
                                               icon_size_.width());
  canvas->Translate(gfx::Vector2d(-badge_rect.x(), -(badge_rect.y()+1)));
  badge_util::PaintBadge(canvas, bounds, text_,
                         SkColor(), SkColor(), // these params are ignored
                         icon_size_.width(),
                         // next param affects only icon bottom padding
                         extensions::Extension::ActionInfo::TYPE_PAGE);
  canvas->Restore();
}

int LionBadgeImageSource::actual_width(const gfx::Size& icon_size,
                                              const std::string& text) {
  gfx::Rect bounds(icon_size.width(), icon_size.height());
  gfx::Rect badge_rect = badge_util::BadgeRect(bounds, text, icon_size.width());
  return badge_rect.width();
}
