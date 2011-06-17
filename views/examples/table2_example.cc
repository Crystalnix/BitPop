// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/examples/table2_example.h"

#include "base/utf_string_conversions.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "views/controls/button/checkbox.h"
#include "views/controls/table/table_view2.h"
#include "views/layout/grid_layout.h"

namespace examples {

Table2Example::Table2Example(ExamplesMain* main)
    : ExampleBase(main) {
}

Table2Example::~Table2Example() {
}

std::wstring Table2Example::GetExampleTitle() {
  return L"Table2";
}

void Table2Example::CreateExampleView(views::View* container) {
  column1_visible_checkbox_ = new views::Checkbox(L"Fruit column visible");
  column1_visible_checkbox_->SetChecked(true);
  column1_visible_checkbox_->set_listener(this);
  column2_visible_checkbox_ = new views::Checkbox(L"Color column visible");
  column2_visible_checkbox_->SetChecked(true);
  column2_visible_checkbox_->set_listener(this);
  column3_visible_checkbox_ = new views::Checkbox(L"Origin column visible");
  column3_visible_checkbox_->SetChecked(true);
  column3_visible_checkbox_->set_listener(this);
  column4_visible_checkbox_ = new views::Checkbox(L"Price column visible");
  column4_visible_checkbox_->SetChecked(true);
  column4_visible_checkbox_->set_listener(this);

  views::GridLayout* layout = new views::GridLayout(container);
  container->SetLayoutManager(layout);

  std::vector<TableColumn> columns;
  columns.push_back(
      TableColumn(0, ASCIIToUTF16("Fruit"), TableColumn::LEFT, 100));
  columns.push_back(
      TableColumn(1, ASCIIToUTF16("Color"), TableColumn::LEFT, 100));
  columns.push_back(
      TableColumn(2, ASCIIToUTF16("Origin"), TableColumn::LEFT, 100));
  columns.push_back(
      TableColumn(3, ASCIIToUTF16("Price"), TableColumn::LEFT, 100));
  const int options = (views::TableView2::SINGLE_SELECTION |
                       views::TableView2::RESIZABLE_COLUMNS |
                       views::TableView2::AUTOSIZE_COLUMNS |
                       views::TableView2::HORIZONTAL_LINES |
                       views::TableView2::VERTICAL_LINES);
  table_ = new views::TableView2(this, columns, views::ICON_AND_TEXT, options);
  table_->SetObserver(this);
  icon1_.setConfig(SkBitmap::kARGB_8888_Config, 16, 16);
  icon1_.allocPixels();
  SkCanvas canvas1(icon1_);
  canvas1.drawColor(SK_ColorRED);

  icon2_.setConfig(SkBitmap::kARGB_8888_Config, 16, 16);
  icon2_.allocPixels();
  SkCanvas canvas2(icon2_);
  canvas2.drawColor(SK_ColorBLUE);

  views::ColumnSet* column_set = layout->AddColumnSet(0);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL, 1,
                        views::GridLayout::USE_PREF, 0, 0);
  layout->StartRow(1 /* expand */, 0);
  layout->AddView(table_);

  column_set = layout->AddColumnSet(1);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL,
                        0.5f, views::GridLayout::USE_PREF, 0, 0);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL,
                        0.5f, views::GridLayout::USE_PREF, 0, 0);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL,
                        0.5f, views::GridLayout::USE_PREF, 0, 0);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL,
                        0.5f, views::GridLayout::USE_PREF, 0, 0);

  layout->StartRow(0 /* no expand */, 1);

  layout->AddView(column1_visible_checkbox_);
  layout->AddView(column2_visible_checkbox_);
  layout->AddView(column3_visible_checkbox_);
  layout->AddView(column4_visible_checkbox_);
}

int Table2Example::RowCount() {
  return 10;
}

string16 Table2Example::GetText(int row, int column_id) {
  const char* const cells[5][4] = {
    { "Orange", "Orange", "South america", "$5" },
    { "Apple", "Green", "Canada", "$3" },
    { "Blue berries", "Blue", "Mexico", "$10.3" },
    { "Strawberries", "Red", "California", "$7" },
    { "Cantaloupe", "Orange", "South america", "$5" }
  };
  return ASCIIToUTF16(cells[row % 5][column_id]);
}

SkBitmap Table2Example::GetIcon(int row) {
  return row % 2 ? icon1_ : icon2_;
}

void Table2Example::SetObserver(TableModelObserver* observer) {
}

void Table2Example::ButtonPressed(views::Button* sender,
                                  const views::Event& event) {
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
}

void Table2Example::OnSelectionChanged() {
  PrintStatus(L"Selection changed: %d", table_->GetFirstSelectedRow());
}

void Table2Example::OnDoubleClick() {
}

void Table2Example::OnMiddleClick() {
}

void Table2Example::OnKeyDown(ui::KeyboardCode virtual_keycode) {
}

void Table2Example::OnTableViewDelete(views::TableView* table_view) {
}

void Table2Example::OnTableView2Delete(views::TableView2* table_view) {
}

}  // namespace examples
