// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/menu/menu_config.h"

#include "grit/ui_resources.h"
#include "ui/base/layout.h"
#include "ui/base/native_theme/native_theme_aura.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"

namespace views {

// static
MenuConfig* MenuConfig::Create() {
  MenuConfig* config = new MenuConfig();
  config->text_color = ui::NativeTheme::instance()->GetSystemColor(
      ui::NativeTheme::kColorId_EnabledMenuItemForegroundColor);
  config->submenu_horizontal_margin_size = 0;
  config->submenu_vertical_margin_size = 2;
  config->submenu_horizontal_inset = 1;
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  config->arrow_to_edge_padding = 20;
  config->icon_to_label_padding = 4;
  config->arrow_width = rb.GetImageNamed(IDR_MENU_ARROW).ToImageSkia()->width();
  const gfx::ImageSkia* check = rb.GetImageNamed(IDR_MENU_CHECK).ToImageSkia();
  // Add 4 to force some padding between check and label.
  config->check_width = check->width() + 4;
  config->check_height = check->height();
  if (ui::GetDisplayLayout() == ui::LAYOUT_TOUCH) {
    config->separator_height = 1;
    config->item_min_height = 40;
    config->item_left_margin = 4;
  } else {
    config->separator_height = 5;
    config->item_min_height = 30;
    config->item_left_margin = 8;
  }
  config->font = rb.GetFont(ResourceBundle::BaseFont);
  config->label_to_arrow_padding = 20;
  config->label_to_accelerator_padding = 20;
  config->always_use_icon_to_label_padding = true;
  config->align_arrow_and_shortcut = true;

  return config;
}

}  // namespace views
