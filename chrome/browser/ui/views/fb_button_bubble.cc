// Copyright (c) 2013 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/fb_button_bubble.h"

#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/search_engines/util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/widget/widget.h"

namespace {
const int kAnchorVerticalInset = 5;
const int kTopInset = 1;
const int kLeftInset = 2;
const int kBottomInset = 7;
const int kRightInset = 2;
}  // namespace

// static
FbButtonBubble* FbButtonBubble::ShowBubble(Browser* browser,
                                           views::View* anchor_view) {
  FbButtonBubble* delegate = new FbButtonBubble(browser, anchor_view);
  delegate->set_arrow_location(views::BubbleBorder::TOP_RIGHT);
  views::BubbleDelegateView::CreateBubble(delegate);
  delegate->StartFade(true);
  return delegate;
}

void FbButtonBubble::Init() {
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  const gfx::Font& original_font = rb.GetFont(ui::ResourceBundle::MediumFont);

  views::Label* title = new views::Label(
      l10n_util::GetStringUTF16(IDS_FBB_BUBBLE_TITLE));
  title->SetFont(original_font.DeriveFont(2, gfx::Font::BOLD));

  views::Label* subtext =
      new views::Label(l10n_util::GetStringUTF16(IDS_FBB_BUBBLE_SUBTEXT));
  subtext->SetFont(original_font);
  subtext->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  subtext->SetMultiLine(true);

  views::GridLayout* layout = views::GridLayout::CreatePanel(this);
  SetLayoutManager(layout);
  layout->SetInsets(kTopInset, kLeftInset, kBottomInset, kRightInset);

  views::ColumnSet* columns = layout->AddColumnSet(0);
  columns->AddColumn(views::GridLayout::LEADING, views::GridLayout::LEADING, 0,
                     views::GridLayout::FIXED, 350, 0);

  layout->StartRow(0, 0);
  layout->AddView(title);
  layout->StartRowWithPadding(0, 0, 0,
      views::kRelatedControlSmallVerticalSpacing);
  layout->AddView(subtext);
}

FbButtonBubble::FbButtonBubble(Browser* browser, views::View* anchor_view)
    : views::BubbleDelegateView(anchor_view, views::BubbleBorder::TOP_LEFT),
      browser_(browser) {
  // Compensate for built-in vertical padding in the anchor view's image.
  set_anchor_insets(
      gfx::Insets(kAnchorVerticalInset, 0, kAnchorVerticalInset, 0));
}

FbButtonBubble::~FbButtonBubble() {
}
