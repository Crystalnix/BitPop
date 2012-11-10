// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/examples/table_example.h"

#include <vector>

#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/layout/grid_layout.h"

namespace views {
namespace examples {

TableExample::TableExample() : ExampleBase("Table") , table_(NULL) {
}

TableExample::~TableExample() {
  // Delete the view before the model.
  delete table_;
  table_ = NULL;
}

void TableExample::CreateExampleView(View* container) {
#if defined(OS_WIN) && !defined(USE_AURA)
  column1_visible_checkbox_ = new Checkbox(
      ASCIIToUTF16("Fruit column visible"));
  column1_visible_checkbox_->SetChecked(true);
  column1_visible_checkbox_->set_listener(this);
  column2_visible_checkbox_ = new Checkbox(
      ASCIIToUTF16("Color column visible"));
  column2_visible_checkbox_->SetChecked(true);
  column2_visible_checkbox_->set_listener(this);
  column3_visible_checkbox_ = new Checkbox(
      ASCIIToUTF16("Origin column visible"));
  column3_visible_checkbox_->SetChecked(true);
  column3_visible_checkbox_->set_listener(this);
  column4_visible_checkbox_ = new Checkbox(
      ASCIIToUTF16("Price column visible"));
  column4_visible_checkbox_->SetChecked(true);
  column4_visible_checkbox_->set_listener(this);
#endif

  GridLayout* layout = new GridLayout(container);
  container->SetLayoutManager(layout);

  std::vector<ui::TableColumn> columns;
  columns.push_back(ui::TableColumn(0, ASCIIToUTF16("Fruit"),
                                    ui::TableColumn::LEFT, 100));
#if defined(OS_WIN) && !defined(USE_AURA)
  columns.push_back(ui::TableColumn(1, ASCIIToUTF16("Color"),
                                    ui::TableColumn::LEFT, 100));
  columns.push_back(ui::TableColumn(2, ASCIIToUTF16("Origin"),
                                    ui::TableColumn::LEFT, 100));
  columns.push_back(ui::TableColumn(3, ASCIIToUTF16("Price"),
                                    ui::TableColumn::LEFT, 100));
#endif
  table_ = new TableView(this, columns, ICON_AND_TEXT, true, true, true);
  table_->SetObserver(this);
  icon1_.setConfig(SkBitmap::kARGB_8888_Config, 16, 16);
  icon1_.allocPixels();
  SkCanvas canvas1(icon1_);
  canvas1.drawColor(SK_ColorRED);

  icon2_.setConfig(SkBitmap::kARGB_8888_Config, 16, 16);
  icon2_.allocPixels();
  SkCanvas canvas2(icon2_);
  canvas2.drawColor(SK_ColorBLUE);

  ColumnSet* column_set = layout->AddColumnSet(0);
  column_set->AddColumn(GridLayout::FILL, GridLayout::FILL, 1,
                        GridLayout::USE_PREF, 0, 0);
  layout->StartRow(1 /* expand */, 0);
  layout->AddView(table_->CreateParentIfNecessary());

  column_set = layout->AddColumnSet(1);
  column_set->AddColumn(GridLayout::FILL, GridLayout::FILL,
                        0.5f, GridLayout::USE_PREF, 0, 0);
  column_set->AddColumn(GridLayout::FILL, GridLayout::FILL,
                        0.5f, GridLayout::USE_PREF, 0, 0);
  column_set->AddColumn(GridLayout::FILL, GridLayout::FILL,
                        0.5f, GridLayout::USE_PREF, 0, 0);
  column_set->AddColumn(GridLayout::FILL, GridLayout::FILL,
                        0.5f, GridLayout::USE_PREF, 0, 0);

  layout->StartRow(0 /* no expand */, 1);

#if defined(OS_WIN) && !defined(USE_AURA)
  layout->AddView(column1_visible_checkbox_);
  layout->AddView(column2_visible_checkbox_);
  layout->AddView(column3_visible_checkbox_);
  layout->AddView(column4_visible_checkbox_);
#endif
}

int TableExample::RowCount() {
  return 10;
}

string16 TableExample::GetText(int row, int column_id) {
  const char* const cells[5][4] = {
    { "Orange", "Orange", "South america", "$5" },
    { "Apple", "Green", "Canada", "$3" },
    { "Blue berries", "Blue", "Mexico", "$10.3" },
    { "Strawberries", "Red", "California", "$7" },
    { "Cantaloupe", "Orange", "South america", "$5" },
  };
  return ASCIIToUTF16(cells[row % 5][column_id]);
}

gfx::ImageSkia TableExample::GetIcon(int row) {
  return row % 2 ? icon1_ : icon2_;
}

void TableExample::SetObserver(ui::TableModelObserver* observer) {}

void TableExample::OnSelectionChanged() {
  PrintStatus("Selected: %s",
              UTF16ToASCII(GetText(table_->FirstSelectedRow(), 0)).c_str());
}

void TableExample::OnDoubleClick() {
  PrintStatus("Double Click: %s",
              UTF16ToASCII(GetText(table_->FirstSelectedRow(), 0)).c_str());
}

void TableExample::OnMiddleClick() {}

void TableExample::OnKeyDown(ui::KeyboardCode virtual_keycode) {}

void TableExample::OnTableViewDelete(TableView* table_view) {}

void TableExample::OnTableView2Delete(TableView2* table_view) {}

void TableExample::ButtonPressed(Button* sender, const Event& event) {
#if defined(OS_WIN) && !defined(USE_AURA)
  int index = 0;
  bool show = true;
  if (sender == column1_visible_checkbox_) {
    index = 0;
    show = column1_visible_checkbox_->checked();
  } else if (sender == column2_visible_checkbox_) {
    index = 1;
    show = column2_visible_checkbox_->checked();
  } else if (sender == column3_visible_checkbox_) {
    index = 2;
    show = column3_visible_checkbox_->checked();
  } else if (sender == column4_visible_checkbox_) {
    index = 3;
    show = column4_visible_checkbox_->checked();
  }
  table_->SetColumnVisibility(index, show);
#endif
}

}  // namespace examples
}  // namespace views
