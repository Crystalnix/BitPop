// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/infobars/before_translate_infobar.h"

#include "base/utf_string_conversions.h"
#include "chrome/browser/translate/translate_infobar_delegate.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "views/controls/button/menu_button.h"
#include "views/controls/label.h"
#include "views/controls/menu/menu_2.h"

BeforeTranslateInfoBar::BeforeTranslateInfoBar(
    TranslateInfoBarDelegate* delegate)
    : TranslateInfoBarBase(delegate),
      label_1_(NULL),
      label_2_(NULL),
      language_menu_button_(NULL),
      accept_button_(NULL),
      deny_button_(NULL),
      never_translate_button_(NULL),
      always_translate_button_(NULL),
      options_menu_button_(NULL),
      languages_menu_model_(delegate, LanguagesMenuModel::ORIGINAL),
      languages_menu_(new views::Menu2(&languages_menu_model_)),
      options_menu_model_(delegate),
      options_menu_(new views::Menu2(&options_menu_model_)) {
}

BeforeTranslateInfoBar::~BeforeTranslateInfoBar() {
}

void BeforeTranslateInfoBar::Layout() {
  TranslateInfoBarBase::Layout();

  int available_width = std::max(0, EndX() - StartX() - ContentMinimumWidth());
  gfx::Size label_1_size = label_1_->GetPreferredSize();
  label_1_->SetBounds(StartX(), OffsetY(label_1_size),
      std::min(label_1_size.width(), available_width), label_1_size.height());
  available_width = std::max(0, available_width - label_1_size.width());

  gfx::Size language_button_size = language_menu_button_->GetPreferredSize();
  language_menu_button_->SetBounds(
      label_1_->bounds().right() + kButtonInLabelSpacing,
      OffsetY(language_button_size), language_button_size.width(),
      language_button_size.height());

  gfx::Size label_2_size = label_2_->GetPreferredSize();
  label_2_->SetBounds(
      language_menu_button_->bounds().right() + kButtonInLabelSpacing,
      OffsetY(label_2_size), std::min(label_2_size.width(), available_width),
      label_2_size.height());

  gfx::Size accept_button_size = accept_button_->GetPreferredSize();
  accept_button_->SetBounds(label_2_->bounds().right() + kEndOfLabelSpacing,
      OffsetY(accept_button_size), accept_button_size.width(),
      accept_button_size.height());

  gfx::Size deny_button_size = deny_button_->GetPreferredSize();
  deny_button_->SetBounds(
        accept_button_->bounds().right() + kButtonButtonSpacing,
        OffsetY(deny_button_size), deny_button_size.width(),
        deny_button_size.height());

  if (never_translate_button_) {
    gfx::Size never_button_size = never_translate_button_->GetPreferredSize();
    never_translate_button_->SetBounds(
          deny_button_->bounds().right() + kButtonButtonSpacing,
          OffsetY(never_button_size), never_button_size.width(),
          never_button_size.height());
  }

  if (always_translate_button_) {
    gfx::Size always_button_size = always_translate_button_->GetPreferredSize();
    always_translate_button_->SetBounds(
          deny_button_->bounds().right() + kButtonButtonSpacing,
          OffsetY(always_button_size), always_button_size.width(),
          always_button_size.height());
  }

  gfx::Size options_size = options_menu_button_->GetPreferredSize();
  options_menu_button_->SetBounds(EndX() - options_size.width(),
      OffsetY(options_size), options_size.width(), options_size.height());
}

void BeforeTranslateInfoBar::ViewHierarchyChanged(bool is_add,
                                                  View* parent,
                                                  View* child) {
  if (!is_add || (child != this) || (label_1_ != NULL)) {
    TranslateInfoBarBase::ViewHierarchyChanged(is_add, parent, child);
    return;
  }

  size_t offset = 0;
  string16 text(l10n_util::GetStringFUTF16(IDS_TRANSLATE_INFOBAR_BEFORE_MESSAGE,
                                           string16(), &offset));

  label_1_ = CreateLabel(text.substr(0, offset));
  AddChildView(label_1_);

  language_menu_button_ = CreateMenuButton(string16(), true, this);
  AddChildView(language_menu_button_);

  label_2_ = CreateLabel(text.substr(offset));
  AddChildView(label_2_);

  accept_button_ = CreateTextButton(this,
      l10n_util::GetStringUTF16(IDS_TRANSLATE_INFOBAR_ACCEPT), false);
  AddChildView(accept_button_);

  deny_button_ = CreateTextButton(this,
      l10n_util::GetStringUTF16(IDS_TRANSLATE_INFOBAR_DENY), false);
  AddChildView(deny_button_);

  TranslateInfoBarDelegate* delegate = GetDelegate();
  const string16& language(delegate->GetLanguageDisplayableNameAt(
      delegate->original_language_index()));
  if (delegate->ShouldShowNeverTranslateButton()) {
    DCHECK(!delegate->ShouldShowAlwaysTranslateButton());
    never_translate_button_ = CreateTextButton(this,
        l10n_util::GetStringFUTF16(IDS_TRANSLATE_INFOBAR_NEVER_TRANSLATE,
                                   language), false);
    AddChildView(never_translate_button_);
  } else if (delegate->ShouldShowAlwaysTranslateButton()) {
    always_translate_button_ = CreateTextButton(this,
        l10n_util::GetStringFUTF16(IDS_TRANSLATE_INFOBAR_ALWAYS_TRANSLATE,
                                   language), false);
    AddChildView(always_translate_button_);
  }

  options_menu_button_ = CreateMenuButton(
      l10n_util::GetStringUTF16(IDS_TRANSLATE_INFOBAR_OPTIONS), false, this);
  AddChildView(options_menu_button_);

  // This must happen after adding all other children so InfoBarView can ensure
  // the close button is the last child.
  TranslateInfoBarBase::ViewHierarchyChanged(is_add, parent, child);

  // This must happen after adding all children because it triggers layout,
  // which assumes that particular children (e.g. the close button) have already
  // been added.
  OriginalLanguageChanged();
}

int BeforeTranslateInfoBar::ContentMinimumWidth() const {
  return
      (kButtonInLabelSpacing +
          language_menu_button_->GetPreferredSize().width() +
          kButtonInLabelSpacing) +
      (kEndOfLabelSpacing + accept_button_->GetPreferredSize().width()) +
      (kButtonButtonSpacing + deny_button_->GetPreferredSize().width()) +
      ((never_translate_button_ == NULL) ? 0 :
          (kButtonButtonSpacing +
              never_translate_button_->GetPreferredSize().width())) +
      ((always_translate_button_ == NULL) ? 0 :
          (kButtonButtonSpacing +
              always_translate_button_->GetPreferredSize().width())) +
      (kEndOfLabelSpacing + options_menu_button_->GetPreferredSize().width());
}

void BeforeTranslateInfoBar::ButtonPressed(views::Button* sender,
                                           const views::Event& event) {
  TranslateInfoBarDelegate* delegate = GetDelegate();
  if (sender == accept_button_) {
    delegate->Translate();
  } else if (sender == deny_button_) {
    delegate->TranslationDeclined();
    RemoveInfoBar();
  } else if (sender == never_translate_button_) {
    delegate->NeverTranslatePageLanguage();
  } else if (sender == always_translate_button_) {
    delegate->AlwaysTranslatePageLanguage();
  } else {
    TranslateInfoBarBase::ButtonPressed(sender, event);
  }
}

void BeforeTranslateInfoBar::OriginalLanguageChanged() {
  UpdateLanguageButtonText(language_menu_button_, LanguagesMenuModel::ORIGINAL);
}

void BeforeTranslateInfoBar::RunMenu(View* source, const gfx::Point& pt) {
  if (source == language_menu_button_) {
    languages_menu_->RunMenuAt(pt, views::Menu2::ALIGN_TOPRIGHT);
  } else {
    DCHECK_EQ(options_menu_button_, source);
    options_menu_->RunMenuAt(pt, views::Menu2::ALIGN_TOPRIGHT);
  }
}
