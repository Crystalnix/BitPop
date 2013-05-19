// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_MYBUB_SEARCH_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_MYBUB_SEARCH_VIEW_H_

#pragma once

#include <list>
#include <string>

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "ui/gfx/size.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/view.h"

class Browser;
class MybubButton;
class OmniboxView;

class MybubSearchView : public views::View,
                        public views::ButtonListener {
 public:
  MybubSearchView(OmniboxView *omnibox_view, Browser *browser);
  virtual ~MybubSearchView();

  // views::View overrides
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual gfx::Size GetMinimumSize() OVERRIDE;
  virtual void Layout() OVERRIDE;

  // views::ButtonListener overrides
  virtual void ButtonPressed(views::Button* button, const ui::Event& event) OVERRIDE;

 private:
  MybubButton* CreateMybubButton(int normal_image_id, int hot_image_id,
                                 int pushed_image_id, int tooltip_msg_id, int view_id);

  std::list<MybubButton*> buttons_;
  gfx::Size containerSize_;

  OmniboxView* omnibox_view_;
  Browser* browser_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_MYBUB_SEARCH_VIEW_H_
