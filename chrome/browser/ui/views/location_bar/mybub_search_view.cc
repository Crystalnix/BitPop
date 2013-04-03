// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/mybub_search_view.h"

#include "base/stl_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_model.h"
#include "chrome/browser/ui/omnibox/omnibox_view.h"
#include "chrome/browser/ui/view_ids.h"
#include "content/public/browser/web_contents.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#include "net/base/escape.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/point.h"
#include "ui/views/controls/button/image_button.h"

using content::OpenURLParams;
using content::Referrer;
using content::WebContents;

static const int kMybubButtonsSpacing = 3;

class MybubButton : public views::ImageButton {
 public:
  enum Kind {
    MYBUB_NONE,

    MYBUB_WIKIPEDIA,
    MYBUB_YOUTUBE,
    MYBUB_REVIEWS,
    MYBUB_NEWS
  };

  MybubButton(views::ButtonListener* observer, Kind kind = MYBUB_NONE) :
    views::ImageButton(observer), kind_(kind) {}
  virtual ~MybubButton() {}

  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE {
    string16 tooltipText;
    if (GetTooltipText(gfx::Point(), &tooltipText)) {
      state->role = ui::AccessibilityTypes::ROLE_PUSHBUTTON;
      state->name = tooltipText;
    }
  }

  void SetKind(Kind kind) { kind_ = kind; }
  Kind kind() const { return kind_; }

 private:
  Kind kind_;
};

MybubSearchView::MybubSearchView(OmniboxView* omnibox_view, Browser* browser)
  : omnibox_view_(omnibox_view), browser_(browser) {
  int buttonOriginX = 0;

  MybubButton* wiki = CreateMybubButton(IDR_MYBUB_WIKIPEDIA, IDR_MYBUB_WIKIPEDIA_H,
      IDR_MYBUB_WIKIPEDIA, IDS_TOOLTIP_MYBUB_WIKIPEDIA,
      VIEW_ID_MYBUB_WIKIPEDIA);
  wiki->SetKind(MybubButton::MYBUB_WIKIPEDIA);
  wiki->SetPosition(gfx::Point(buttonOriginX, 0));

  buttonOriginX += wiki->width() + kMybubButtonsSpacing;

  MybubButton* yt = CreateMybubButton(IDR_MYBUB_YOUTUBE, IDR_MYBUB_YOUTUBE_H,
      IDR_MYBUB_YOUTUBE, IDS_TOOLTIP_MYBUB_YOUTUBE,
      VIEW_ID_MYBUB_YOUTUBE);
  yt->SetKind(MybubButton::MYBUB_YOUTUBE);
  yt->SetPosition(gfx::Point(buttonOriginX, 0));

  buttonOriginX += yt->width() + kMybubButtonsSpacing;

  MybubButton* reviews = CreateMybubButton(IDR_MYBUB_REVIEWS, IDR_MYBUB_REVIEWS_H,
      IDR_MYBUB_REVIEWS, IDS_TOOLTIP_MYBUB_REVIEWS,
      VIEW_ID_MYBUB_REVIEWS);
  reviews->SetKind(MybubButton::MYBUB_REVIEWS);
  reviews->SetPosition(gfx::Point(buttonOriginX, 0));

  buttonOriginX += reviews->width() + kMybubButtonsSpacing;

  MybubButton* news = CreateMybubButton(IDR_MYBUB_NEWS, IDR_MYBUB_NEWS_H,
      IDR_MYBUB_NEWS, IDS_TOOLTIP_MYBUB_NEWS,
      VIEW_ID_MYBUB_NEWS);
  news->SetKind(MybubButton::MYBUB_NEWS);
  news->SetPosition(gfx::Point(buttonOriginX, 0));

  buttons_.push_back(wiki);
  buttons_.push_back(yt);
  buttons_.push_back(reviews);
  buttons_.push_back(news);

  // height should be the same for each beforementioned buttons
  containerSize_ = gfx::Size(buttonOriginX + news->width(), news->height());

  this->SetSize(containerSize_);
  std::list<MybubButton*>::iterator it = buttons_.begin();
  for (; it != buttons_.end(); it++) {
    this->AddChildView(*it);
    (*it)->SetVisible(true);
  }
}

MybubSearchView::~MybubSearchView() {
  STLDeleteElements(&buttons_);
}

gfx::Size MybubSearchView::GetPreferredSize() {
  return containerSize_;
}

gfx::Size MybubSearchView::GetMinimumSize() {
  return containerSize_;
}

void MybubSearchView::Layout() {
}

void MybubSearchView::ButtonPressed(views::Button* button, const views::Event& event) {
  if (!omnibox_view_)
    return;

  MybubButton* but = static_cast<MybubButton*>(button);

  std::string uriSuffix = "knowledge";
  switch (but->kind()) {
    case MybubButton::MYBUB_WIKIPEDIA:
      // already assigned
      break;
    case MybubButton::MYBUB_YOUTUBE:
      uriSuffix = "visual";
      break;
    case MybubButton::MYBUB_REVIEWS:
      uriSuffix = "reviews";
      break;
    case MybubButton::MYBUB_NEWS:
      uriSuffix = "news";
      break;
    default:
      NOTREACHED();
      return;
  }

  if (!omnibox_view_->model()->CurrentTextIsURL()) {
    const string16 userText = omnibox_view_->GetText();
    std::string encodedTerms = net::EscapeQueryParamValue(UTF16ToUTF8(userText), true);
    std::string mybubURL = std::string("http://mybub.com/mod/") +
        uriSuffix + std::string("/");
    std::string finalURL = mybubURL + encodedTerms;

    GURL url(finalURL);
    OpenURLParams params(
        url, Referrer(), CURRENT_TAB, content::PAGE_TRANSITION_TYPED, false);
    browser_->OpenURL(params);
  }

}

MybubButton* MybubSearchView::CreateMybubButton(int normal_image_id, int hot_image_id,
                                        int pushed_image_id, int tooltip_msg_id,
                                        int view_id) {
  MybubButton* button = new MybubButton(this);

  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  gfx::ImageSkia *normalImage = rb.GetImageSkiaNamed(normal_image_id);
  button->SetImage(views::CustomButton::BS_NORMAL, normalImage);
  button->SetImage(views::CustomButton::BS_HOT, rb.GetImageSkiaNamed(hot_image_id));
  button->SetImage(views::CustomButton::BS_PUSHED, rb.GetImageSkiaNamed(pushed_image_id));

  button->SetSize(gfx::Size(normalImage->width(), normalImage->height()));

  button->SetTooltipText(l10n_util::GetStringUTF16(tooltip_msg_id));

  button->set_focusable(true);

  button->set_id(view_id);

  return button;
}
