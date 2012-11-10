// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/network_profile_bubble_view.h"

#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/network_profile_bubble.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar_view.h"
#include "chrome/common/pref_names.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_constants.h"

namespace {

// Bubble layout constants.
const int kAnchorVerticalInset = 5;
const int kInset = 2;
const int kNotificationBubbleWidth = 250;

}  // namespace

// static
void NetworkProfileBubble::ShowNotification(Browser* browser) {
  views::View* anchor = NULL;
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (browser_view && browser_view->GetToolbarView())
    anchor = browser_view->GetToolbarView()->app_menu();
  NetworkProfileBubbleView* bubble =
      new NetworkProfileBubbleView(anchor, browser, browser->profile());
  views::BubbleDelegateView::CreateBubble(bubble);
  bubble->Show();
  NetworkProfileBubble::SetNotificationShown(true);

  // Mark the time of the last bubble and reduce the number of warnings left
  // before the next silence period starts.
  PrefService* prefs = browser->profile()->GetPrefs();
  prefs->SetInt64(prefs::kNetworkProfileLastWarningTime,
                  base::Time::Now().ToTimeT());
  int left_warnings = prefs->GetInteger(prefs::kNetworkProfileWarningsLeft);
  if (left_warnings > 0)
    prefs->SetInteger(prefs::kNetworkProfileWarningsLeft, --left_warnings);
}

////////////////////////////////////////////////////////////////////////////////
// NetworkProfileBubbleView, public:

NetworkProfileBubbleView::NetworkProfileBubbleView(
    views::View* anchor,
    content::PageNavigator* navigator,
    Profile* profile)
    : BubbleDelegateView(anchor, views::BubbleBorder::TOP_RIGHT),
      navigator_(navigator),
      profile_(profile) {
}

////////////////////////////////////////////////////////////////////////////////
// NetworkProfileBubbleView, private:

NetworkProfileBubbleView::~NetworkProfileBubbleView() {
}

void NetworkProfileBubbleView::Init() {
  views::GridLayout* layout = views::GridLayout::CreatePanel(this);
  layout->SetInsets(0, kInset, kInset, kInset);
  SetLayoutManager(layout);

  views::ColumnSet* columns = layout->AddColumnSet(0);
  columns->AddColumn(views::GridLayout::LEADING, views::GridLayout::LEADING, 0,
                     views::GridLayout::USE_PREF, 0, 0);

  layout->StartRow(0, 0);

  views::Label* title = new views::Label(
      l10n_util::GetStringFUTF16(IDS_PROFILE_ON_NETWORK_WARNING,
          l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
  title->SetMultiLine(true);
  title->SizeToFit(kNotificationBubbleWidth);
  title->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
  layout->AddView(title);

  views::ColumnSet* bottom_columns = layout->AddColumnSet(1);
  bottom_columns->AddColumn(views::GridLayout::CENTER,
      views::GridLayout::CENTER, 0, views::GridLayout::USE_PREF, 0, 0);
  bottom_columns->AddPaddingColumn(1, 0);
  bottom_columns->AddColumn(views::GridLayout::CENTER,
      views::GridLayout::CENTER, 0, views::GridLayout::USE_PREF, 0, 0);
  layout->StartRowWithPadding(0, 1, 0,
                              views::kRelatedControlSmallVerticalSpacing);

  views::Link* learn_more =
      new views::Link(l10n_util::GetStringUTF16(IDS_LEARN_MORE));
  learn_more->set_listener(this);
  layout->AddView(learn_more);

  views::NativeTextButton* ok_button = new views::NativeTextButton(
      this, l10n_util::GetStringUTF16(IDS_OK));
  ok_button->SetIsDefault(true);
  layout->AddView(ok_button);
}

gfx::Rect NetworkProfileBubbleView::GetAnchorRect() {
  // Compensate for padding in anchor.
  gfx::Rect rect(BubbleDelegateView::GetAnchorRect());
  rect.Inset(0, anchor_view() ? kAnchorVerticalInset : 0);
  return rect;
}

void NetworkProfileBubbleView::ButtonPressed(views::Button* sender,
                                             const views::Event& event) {
  NetworkProfileBubble::RecordUmaEvent(
      NetworkProfileBubble::METRIC_ACKNOWLEDGED);

  GetWidget()->Close();
}

void NetworkProfileBubbleView::LinkClicked(views::Link* source,
                                           int event_flags) {
  NetworkProfileBubble::RecordUmaEvent(
      NetworkProfileBubble::METRIC_LEARN_MORE_CLICKED);
  WindowOpenDisposition disposition =
      chrome::DispositionFromEventFlags(event_flags);
  content::OpenURLParams params(
      GURL("https://sites.google.com/a/chromium.org/dev/administrators/"
            "common-problems-and-solutions#network_profile"),
      content::Referrer(),
      disposition == CURRENT_TAB ? NEW_FOREGROUND_TAB : disposition,
      content::PAGE_TRANSITION_LINK, false);
  navigator_->OpenURL(params);

  // If the user interacted with the bubble we don't reduce the number of
  // warnings left.
  PrefService* prefs = profile_->GetPrefs();
  int left_warnings = prefs->GetInteger(prefs::kNetworkProfileWarningsLeft);
  prefs->SetInteger(prefs::kNetworkProfileWarningsLeft, ++left_warnings);
  GetWidget()->Close();
}
