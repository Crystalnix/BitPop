// Copyright (c) 2013 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FB_BUTTON_BUBBLE_H_
#define CHROME_BROWSER_UI_VIEWS_FB_BUTTON_BUBBLE_H_

#include "ui/views/bubble/bubble_delegate.h"
#include "ui/views/controls/link_listener.h"

class Browser;

class FbButtonBubble : public views::BubbleDelegateView {
 public:
  // |browser| is the opening browser and is NULL in unittests.
  static FbButtonBubble* ShowBubble(Browser* browser, views::View* anchor_view);

 protected:
  // views::BubbleDelegateView overrides:
  virtual void Init() OVERRIDE;

 private:
  FbButtonBubble(Browser* browser, views::View* anchor_view);
  virtual ~FbButtonBubble();

  Browser* browser_;

  DISALLOW_COPY_AND_ASSIGN(FbButtonBubble);
};

#endif  // CHROME_BROWSER_UI_VIEWS_FB_BUTTON_BUBBLE_H_
