// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_BUBBLE_BUBBLE_FRAME_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_BUBBLE_BUBBLE_FRAME_VIEW_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "chrome/browser/ui/views/facebook_chat/bubble/bubble_border.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/insets.h"
#include "ui/views/window/non_client_view.h"

namespace views {

// This is a NonClientFrameView used to render the BitpopBubbleBorder.
class BitpopBubbleFrameView : public NonClientFrameView {
 public:
  // Sets the border to |border|, taking ownership. Important: do not call
  // set_border() directly to change the border, use SetBubbleBorder() instead.
  BitpopBubbleFrameView(const gfx::Insets& margins, BitpopBubbleBorder* border);
  virtual ~BitpopBubbleFrameView();

  // NonClientFrameView overrides:
  virtual gfx::Rect GetBoundsForClientView() const OVERRIDE;
  virtual gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const OVERRIDE;
  virtual int NonClientHitTest(const gfx::Point& point) OVERRIDE;
  virtual void GetWindowMask(const gfx::Size& size,
                             gfx::Path* window_mask) OVERRIDE {}
  virtual void ResetWindowControls() OVERRIDE {}
  virtual void UpdateWindowIcon() OVERRIDE {}
  virtual void UpdateWindowTitle() OVERRIDE {}

  // View overrides:
  virtual gfx::Size GetPreferredSize() OVERRIDE;

  BitpopBubbleBorder* bubble_border() const { return bubble_border_; }

  gfx::Insets content_margins() const { return content_margins_; }

  // Given the size of the contents and the rect to point at, returns the bounds
  // of the bubble window. The bubble's arrow location may change if the bubble
  // does not fit on the monitor and |try_mirroring_arrow| is true.
  gfx::Rect GetUpdatedWindowBounds(const gfx::Rect& anchor_rect,
                                   gfx::Size client_size,
                                   bool try_mirroring_arrow);

  void SetBubbleBorder(BitpopBubbleBorder* border);

 protected:
  // Returns the bounds for the monitor showing the specified |rect|.
  // This function is virtual to support testing environments.
  virtual gfx::Rect GetMonitorBounds(const gfx::Rect& rect);

 private:
  // Mirrors the bubble's arrow location on the |vertical| or horizontal axis,
  // if the generated window bounds don't fit in the monitor bounds.
  void MirrorArrowIfOffScreen(bool vertical,
                              const gfx::Rect& anchor_rect,
                              const gfx::Size& client_size);

  // The bubble border.
  BitpopBubbleBorder* bubble_border_;

  // Margins between the content and the inside of the border, in pixels.
  gfx::Insets content_margins_;

  DISALLOW_COPY_AND_ASSIGN(BitpopBubbleFrameView);
};

}  // namespace views

#endif  // CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_BUBBLE_BUBBLE_FRAME_VIEW_H_
