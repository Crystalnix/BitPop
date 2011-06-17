// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_WM_OVERVIEW_FAVICON_H_
#define CHROME_BROWSER_CHROMEOS_WM_OVERVIEW_FAVICON_H_
#pragma once

#include "views/widget/widget_gtk.h"

class SkBitmap;

namespace views {
class ImageView;
}

namespace chromeos {

class WmOverviewSnapshot;

// A single favicon displayed by WmOverviewController.
class WmOverviewFavicon : public views::WidgetGtk {
 public:
  static const int kIconSize;

  WmOverviewFavicon();

  // Initializes the favicon to 0x0 size.
  void Init(WmOverviewSnapshot* snapshot);

  // Setting the favicon sets the bounds to the size of the given
  // image.
  void SetFavicon(const SkBitmap& image);

 private:
  // This control is the contents view for this widget.
  views::ImageView* favicon_view_;

  DISALLOW_COPY_AND_ASSIGN(WmOverviewFavicon);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_WM_OVERVIEW_FAVICON_H_
