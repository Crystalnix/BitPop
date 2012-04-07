// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/speech/speech_input_bubble.h"

#include <algorithm>

#include "base/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_icon_view.h"
#include "chrome/browser/ui/views/toolbar_view.h"
#include "chrome/browser/ui/views/window.h"
#include "content/browser/resource_context.h"
#include "content/browser/speech/speech_input_manager.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/bubble/bubble_delegate.h"
#include "ui/views/controls/button/text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/link_listener.h"
#include "ui/views/layout/layout_constants.h"

using content::WebContents;

namespace {

const int kBubbleHorizMargin = 6;
const int kBubbleVertMargin = 4;
const int kBubbleHeadingVertMargin = 6;
const int kIconHorizontalOffset = 27;
const int kIconVerticalOffset = -7;

// This is the SpeechInputBubble content and views bubble delegate.
class SpeechInputBubbleView
    : public views::BubbleDelegateView,
      public views::ButtonListener,
      public views::LinkListener {
 public:
  SpeechInputBubbleView(SpeechInputBubbleDelegate* delegate,
                        views::View* anchor_view,
                        const gfx::Rect& element_rect,
                        WebContents* web_contents);

  void UpdateLayout(SpeechInputBubbleBase::DisplayMode mode,
                    const string16& message_text,
                    const SkBitmap& image);
  void SetImage(const SkBitmap& image);

  // views::BubbleDelegateView methods.
  virtual void OnWidgetActivationChanged(views::Widget* widget,
                                         bool active) OVERRIDE;
  virtual gfx::Rect GetAnchorRect() OVERRIDE;
  virtual void Init() OVERRIDE;

  // views::ButtonListener methods.
  virtual void ButtonPressed(views::Button* source,
                             const views::Event& event) OVERRIDE;

  // views::LinkListener methods.
  virtual void LinkClicked(views::Link* source, int event_flags) OVERRIDE;

  // views::View overrides.
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual void Layout() OVERRIDE;

  void set_notify_delegate_on_activation_change(bool notify) {
    notify_delegate_on_activation_change_ = notify;
  }

 private:
  SpeechInputBubbleDelegate* delegate_;
  gfx::Rect element_rect_;
  WebContents* web_contents_;
  bool notify_delegate_on_activation_change_;
  views::ImageView* icon_;
  views::Label* heading_;
  views::Label* message_;
  views::TextButton* try_again_;
  views::TextButton* cancel_;
  views::Link* mic_settings_;
  SpeechInputBubbleBase::DisplayMode display_mode_;
  const int kIconLayoutMinWidth;

  DISALLOW_COPY_AND_ASSIGN(SpeechInputBubbleView);
};

SpeechInputBubbleView::SpeechInputBubbleView(
    SpeechInputBubbleDelegate* delegate,
    views::View* anchor_view,
    const gfx::Rect& element_rect,
    WebContents* web_contents)
    : BubbleDelegateView(anchor_view, views::BubbleBorder::TOP_LEFT),
      delegate_(delegate),
      element_rect_(element_rect),
      web_contents_(web_contents),
      notify_delegate_on_activation_change_(true),
      icon_(NULL),
      heading_(NULL),
      message_(NULL),
      try_again_(NULL),
      cancel_(NULL),
      mic_settings_(NULL),
      display_mode_(SpeechInputBubbleBase::DISPLAY_MODE_WARM_UP),
      kIconLayoutMinWidth(ResourceBundle::GetSharedInstance().GetBitmapNamed(
                          IDR_SPEECH_INPUT_MIC_EMPTY)->width()) {
  // The bubble lifetime is managed by its controller; closing on escape or
  // explicitly closing on deactivation will cause unexpected behavior.
  set_close_on_esc(false);
  set_close_on_deactivate(false);
}

void SpeechInputBubbleView::OnWidgetActivationChanged(views::Widget* widget,
                                                      bool active) {
  if (widget == GetWidget() && !active && notify_delegate_on_activation_change_)
    delegate_->InfoBubbleFocusChanged();
  BubbleDelegateView::OnWidgetActivationChanged(widget, active);
}

gfx::Rect SpeechInputBubbleView::GetAnchorRect() {
  gfx::Rect container_rect;
  web_contents_->GetContainerBounds(&container_rect);
  gfx::Rect anchor(element_rect_);
  anchor.Offset(container_rect.origin());
  if (!container_rect.Intersects(anchor))
    return BubbleDelegateView::GetAnchorRect();
  return anchor;
}

void SpeechInputBubbleView::Init() {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  const gfx::Font& font = rb.GetFont(ResourceBundle::MediumFont);

  heading_ = new views::Label(
      l10n_util::GetStringUTF16(IDS_SPEECH_INPUT_BUBBLE_HEADING));
  heading_->set_border(views::Border::CreateEmptyBorder(
      kBubbleHeadingVertMargin, 0, kBubbleHeadingVertMargin, 0));
  heading_->SetFont(font);
  heading_->SetHorizontalAlignment(views::Label::ALIGN_CENTER);
  heading_->SetText(
      l10n_util::GetStringUTF16(IDS_SPEECH_INPUT_BUBBLE_HEADING));
  AddChildView(heading_);

  message_ = new views::Label();
  message_->SetFont(font);
  message_->SetHorizontalAlignment(views::Label::ALIGN_CENTER);
  message_->SetMultiLine(true);
  AddChildView(message_);

  icon_ = new views::ImageView();
  icon_->SetHorizontalAlignment(views::ImageView::CENTER);
  AddChildView(icon_);

  cancel_ = new views::NativeTextButton(
      this, l10n_util::GetStringUTF16(IDS_CANCEL));
  AddChildView(cancel_);

  try_again_ = new views::NativeTextButton(
      this, l10n_util::GetStringUTF16(IDS_SPEECH_INPUT_TRY_AGAIN));
  AddChildView(try_again_);

  mic_settings_ = new views::Link(
      l10n_util::GetStringUTF16(IDS_SPEECH_INPUT_MIC_SETTINGS));
  mic_settings_->set_listener(this);
  AddChildView(mic_settings_);
}

void SpeechInputBubbleView::UpdateLayout(
    SpeechInputBubbleBase::DisplayMode mode,
    const string16& message_text,
    const SkBitmap& image) {
  display_mode_ = mode;
  bool is_message = (mode == SpeechInputBubbleBase::DISPLAY_MODE_MESSAGE);
  icon_->SetVisible(!is_message);
  message_->SetVisible(is_message);
  mic_settings_->SetVisible(is_message);
  try_again_->SetVisible(is_message);
  cancel_->SetVisible(mode != SpeechInputBubbleBase::DISPLAY_MODE_WARM_UP);
  heading_->SetVisible(mode == SpeechInputBubbleBase::DISPLAY_MODE_RECORDING);

  if (is_message) {
    message_->SetText(message_text);
  } else {
    SetImage(image);
  }

  if (icon_->visible())
    icon_->ResetImageSize();

  // When moving from warming up to recording state, the size of the content
  // stays the same. So we wouldn't get a resize/layout call from the view
  // system and we do it ourselves.
  if (GetPreferredSize() == size())  // |size()| here is the current size.
    Layout();

  SizeToContents();
}

void SpeechInputBubbleView::SetImage(const SkBitmap& image) {
  icon_->SetImage(image);
}

void SpeechInputBubbleView::ButtonPressed(views::Button* source,
                                          const views::Event& event) {
  if (source == cancel_) {
    delegate_->InfoBubbleButtonClicked(SpeechInputBubble::BUTTON_CANCEL);
  } else if (source == try_again_) {
    delegate_->InfoBubbleButtonClicked(SpeechInputBubble::BUTTON_TRY_AGAIN);
  } else {
    NOTREACHED() << "Unknown button";
  }
}

void SpeechInputBubbleView::LinkClicked(views::Link* source, int event_flags) {
  DCHECK_EQ(source, mic_settings_);
  speech_input::SpeechInputManager::ShowAudioInputSettingsFromUI(
      &web_contents_->GetBrowserContext()->GetResourceContext());
}

gfx::Size SpeechInputBubbleView::GetPreferredSize() {
  int width = heading_->GetPreferredSize().width();
  int control_width = cancel_->GetPreferredSize().width();
  if (try_again_->visible()) {
    control_width += try_again_->GetPreferredSize().width() +
                     views::kRelatedButtonHSpacing;
  }
  width = std::max(width, control_width);
  control_width = std::max(icon_->GetPreferredSize().width(),
                           kIconLayoutMinWidth);
  width = std::max(width, control_width);
  if (mic_settings_->visible()) {
    control_width = mic_settings_->GetPreferredSize().width();
    width = std::max(width, control_width);
  }

  int height = cancel_->GetPreferredSize().height();
  if (message_->visible()) {
    height += message_->GetHeightForWidth(width) +
              views::kLabelToControlVerticalSpacing;
  }
  if (heading_->visible())
    height += heading_->GetPreferredSize().height();
  if (icon_->visible())
    height += icon_->GetImage().height();
  if (mic_settings_->visible())
    height += mic_settings_->GetPreferredSize().height();
  width += kBubbleHorizMargin * 2;
  height += kBubbleVertMargin * 2;

  return gfx::Size(width, height);
}

void SpeechInputBubbleView::Layout() {
  int x = kBubbleHorizMargin;
  int y = kBubbleVertMargin;
  int available_width = width() - kBubbleHorizMargin * 2;
  int available_height = height() - kBubbleVertMargin * 2;

  if (message_->visible()) {
    DCHECK(try_again_->visible());

    int control_height = try_again_->GetPreferredSize().height();
    int try_again_width = try_again_->GetPreferredSize().width();
    int cancel_width = cancel_->GetPreferredSize().width();
    y += available_height - control_height;
    x += (available_width - cancel_width - try_again_width -
          views::kRelatedButtonHSpacing) / 2;
    try_again_->SetBounds(x, y, try_again_width, control_height);
    cancel_->SetBounds(x + try_again_width + views::kRelatedButtonHSpacing, y,
                       cancel_width, control_height);

    control_height = message_->GetHeightForWidth(available_width);
    message_->SetBounds(kBubbleHorizMargin, kBubbleVertMargin,
                        available_width, control_height);
    y = kBubbleVertMargin + control_height;

    control_height = mic_settings_->GetPreferredSize().height();
    mic_settings_->SetBounds(kBubbleHorizMargin, y, available_width,
                             control_height);
  } else {
    DCHECK(icon_->visible());

    int control_height = icon_->GetImage().height();
    if (display_mode_ == SpeechInputBubbleBase::DISPLAY_MODE_WARM_UP)
      y = (available_height - control_height) / 2;
    icon_->SetBounds(x, y, available_width, control_height);
    y += control_height;

    if (heading_->visible()) {
      control_height = heading_->GetPreferredSize().height();
      heading_->SetBounds(x, y, available_width, control_height);
      y += control_height;
    }

    if (cancel_->visible()) {
      control_height = cancel_->GetPreferredSize().height();
      int width = cancel_->GetPreferredSize().width();
      cancel_->SetBounds(x + (available_width - width) / 2, y, width,
                         control_height);
    }
  }
}

// Implementation of SpeechInputBubble.
class SpeechInputBubbleImpl : public SpeechInputBubbleBase {
 public:
  SpeechInputBubbleImpl(WebContents* web_contents,
                        Delegate* delegate,
                        const gfx::Rect& element_rect);
  virtual ~SpeechInputBubbleImpl();

  // SpeechInputBubble methods.
  virtual void Show() OVERRIDE;
  virtual void Hide() OVERRIDE;

  // SpeechInputBubbleBase methods.
  virtual void UpdateLayout() OVERRIDE;
  virtual void UpdateImage() OVERRIDE;

 private:
  Delegate* delegate_;
  SpeechInputBubbleView* bubble_;
  gfx::Rect element_rect_;

  DISALLOW_COPY_AND_ASSIGN(SpeechInputBubbleImpl);
};

SpeechInputBubbleImpl::SpeechInputBubbleImpl(WebContents* web_contents,
                                             Delegate* delegate,
                                             const gfx::Rect& element_rect)
    : SpeechInputBubbleBase(web_contents),
      delegate_(delegate),
      bubble_(NULL),
      element_rect_(element_rect) {
}

SpeechInputBubbleImpl::~SpeechInputBubbleImpl() {
  if (bubble_) {
    bubble_->set_notify_delegate_on_activation_change(false);
    bubble_->GetWidget()->Close();
  }
}

void SpeechInputBubbleImpl::Show() {
  if (!bubble_) {
    // Anchor to the location icon view, in case |element_rect| is offscreen.
    Browser* browser = Browser::GetOrCreateTabbedBrowser(
        Profile::FromBrowserContext(web_contents()->GetBrowserContext()));
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
    views::View* icon = browser_view->GetLocationBarView() ?
        browser_view->GetLocationBarView()->location_icon_view() : NULL;
    bubble_ = new SpeechInputBubbleView(delegate_, icon, element_rect_,
                                        web_contents());
    browser::CreateViewsBubble(bubble_);
    UpdateLayout();
  }
  bubble_->Show();
}

void SpeechInputBubbleImpl::Hide() {
  if (bubble_)
    bubble_->GetWidget()->Hide();
}

void SpeechInputBubbleImpl::UpdateLayout() {
  if (bubble_)
    bubble_->UpdateLayout(display_mode(), message_text(), icon_image());
}

void SpeechInputBubbleImpl::UpdateImage() {
  if (bubble_)
    bubble_->SetImage(icon_image());
}

}  // namespace

SpeechInputBubble* SpeechInputBubble::CreateNativeBubble(
    WebContents* web_contents,
    SpeechInputBubble::Delegate* delegate,
    const gfx::Rect& element_rect) {
  return new SpeechInputBubbleImpl(web_contents, delegate, element_rect);
}
