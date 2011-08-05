// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/page_info_bubble_view.h"

#include "base/utf_string_conversions.h"
#include "chrome/browser/certificate_viewer.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/views/bubble/bubble.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar_view.h"
#include "chrome/common/url_constants.h"
#include "content/browser/cert_store.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/image.h"
#include "views/controls/image_view.h"
#include "views/controls/label.h"
#include "views/controls/link.h"
#include "views/controls/separator.h"
#include "views/layout/grid_layout.h"
#include "views/widget/widget.h"
#include "views/window/window.h"

namespace {

// Layout constants.
const int kHGapToBorder = 11;
const int kVGapToImage = 10;
const int kVGapToHeadline = 7;
const int kHGapImageToDescription = 6;
const int kTextPaddingRight = 10;
const int kPaddingBelowSeparator = 4;
const int kPaddingAboveSeparator = 13;
const int kIconHorizontalOffset = 27;
const int kIconVerticalOffset = -7;

// The duration of the animation that resizes the bubble once the async
// information is provided through the ModelChanged event.
const int kPageInfoSlideDuration = 1450;

// A section contains an image that shows a status (good or bad), a title, an
// optional head-line (in bold) and a description.
class Section : public views::View,
                public views::LinkListener {
 public:
  Section(PageInfoBubbleView* owner,
          const PageInfoModel::SectionInfo& section_info,
          const SkBitmap* status_icon,
          bool show_cert);
  virtual ~Section();

  // Notify the section how far along in the animation we are. This is used
  // to draw the section opaquely onto the canvas, to animate the section into
  // view.
  void SetAnimationStage(double animation_stage);

  // views::View methods:
  virtual int GetHeightForWidth(int w);
  virtual void Layout();
  virtual void Paint(gfx::Canvas* canvas);

  // views::LinkListener methods:
  virtual void LinkClicked(views::Link* source, int event_flags) OVERRIDE;

 private:
  // Calculate the animation value to use for setting the opacity.
  double OpacityAnimationValue();

  // Calculate the layout if |compute_bounds_only|, otherwise does Layout also.
  gfx::Size LayoutItems(bool compute_bounds_only, int width);

  // The view that owns this Section object.
  PageInfoBubbleView* owner_;

  // The information this view represents.
  PageInfoModel::SectionInfo info_;

  views::ImageView* status_image_;
  views::Label* headline_label_;
  views::Label* description_label_;
  views::Link* link_;

  // The level of animation we are currently at.
  double animation_value_;

  DISALLOW_COPY_AND_ASSIGN(Section);
};

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// PageInfoBubbleView

Bubble* PageInfoBubbleView::bubble_ = NULL;

PageInfoBubbleView::PageInfoBubbleView(gfx::NativeWindow parent_window,
                                       Profile* profile,
                                       const GURL& url,
                                       const NavigationEntry::SSLStatus& ssl,
                                       bool show_history)
    : ALLOW_THIS_IN_INITIALIZER_LIST(model_(profile, url, ssl,
                                            show_history, this)),
      parent_window_(parent_window),
      cert_id_(ssl.cert_id()),
      help_center_link_(NULL),
      ALLOW_THIS_IN_INITIALIZER_LIST(resize_animation_(this)),
      animation_start_height_(0) {
  if (bubble_)
    bubble_->Close();
  if (cert_id_ > 0) {
    scoped_refptr<net::X509Certificate> cert;
    CertStore::GetInstance()->RetrieveCert(cert_id_, &cert);
    // When running with fake certificate (Chrome Frame), we have no os
    // certificate, so there is no cert to show. Don't bother showing the cert
    // info link in that case.
    if (!cert.get() || !cert->os_cert_handle())
      cert_id_ = 0;
  }
  LayoutSections();
}

PageInfoBubbleView::~PageInfoBubbleView() {
}

void PageInfoBubbleView::ShowCertDialog() {
  ShowCertificateViewerByID(parent_window_, cert_id_);
}

gfx::Size PageInfoBubbleView::GetSeparatorSize() {
  // Calculate how much space the separators take up (with padding).
  views::Separator separator;
  gfx::Size separator_size = separator.GetPreferredSize();
  gfx::Size separator_plus_padding(0, separator_size.height() +
                                      kPaddingAboveSeparator +
                                      kPaddingBelowSeparator);
  return separator_plus_padding;
}

double PageInfoBubbleView::HeightAnimationValue() {
  // We use the first half of the animation to get to fully expanded mode.
  // Towards the end, we also animate the section into view, as determined
  // by OpacityAnimationValue().
  return std::min(1.0, 2.0 * resize_animation_.GetCurrentValue());
}

double Section::OpacityAnimationValue() {
  // We use the tail end of the animation to get to fully visible.
  // The first half of the animation is devoted to expanding the size of the
  // bubble, as determined by HeightAnimationValue().
  return std::max(0.0, std::min(1.0, 1.7 * animation_value_ - 1.0));
}

void PageInfoBubbleView::LayoutSections() {
  // Remove all the existing sections.
  RemoveAllChildViews(true);

  views::GridLayout* layout = new views::GridLayout(this);
  SetLayoutManager(layout);
  views::ColumnSet* columns = layout->AddColumnSet(0);
  columns->AddColumn(views::GridLayout::FILL,  // Horizontal resize.
                     views::GridLayout::FILL,  // Vertical resize.
                     1,   // Resize weight.
                     views::GridLayout::USE_PREF,  // Size type.
                     0,   // Ignored for USE_PREF.
                     0);  // Minimum size.
  // Add a column set for aligning the text when it has no icons (such as the
  // help center link).
  columns = layout->AddColumnSet(1);
  columns->AddPaddingColumn(
      0, kHGapToBorder + kIconHorizontalOffset + kHGapImageToDescription);
  columns->AddColumn(views::GridLayout::LEADING,  // Horizontal resize.
                     views::GridLayout::FILL,     // Vertical resize.
                     1,   // Resize weight.
                     views::GridLayout::USE_PREF,  // Size type.
                     0,   // Ignored for USE_PREF.
                     0);  // Minimum size.

  int count = model_.GetSectionCount();
  for (int i = 0; i < count; ++i) {
    PageInfoModel::SectionInfo info = model_.GetSectionInfo(i);
    layout->StartRow(0, 0);
    const SkBitmap* icon = *model_.GetIconImage(info.icon_id);
    Section* section = new Section(this, info, icon, cert_id_ > 0);
    if (info.type == PageInfoModel::SECTION_INFO_FIRST_VISIT) {
      // This section is animated into view, so we need to set the height of it
      // according to the animation stage, and let it know how transparent it
      // should draw itself.
      section->SetAnimationStage(resize_animation_.GetCurrentValue());
      gfx::Size sz(views::Window::GetLocalizedContentsSize(
          IDS_PAGEINFOBUBBLE_WIDTH_CHARS, IDS_PAGEINFOBUBBLE_HEIGHT_LINES));
      layout->AddView(section,
                      1, 1,  // Colspan & Rowspan.
                      views::GridLayout::LEADING, views::GridLayout::LEADING,
                      sz.width(),
                      static_cast<int>(HeightAnimationValue() *
                                       section->GetHeightForWidth(sz.width())));
    } else {
      layout->AddView(section);
    }

    // Add separator after all sections.
    layout->AddPaddingRow(0, kPaddingAboveSeparator);
    layout->StartRow(0, 0);
    layout->AddView(new views::Separator());
    layout->AddPaddingRow(0, kPaddingBelowSeparator);
  }

  // Then add the help center link at the bottom.
  layout->StartRow(0, 1);
  help_center_link_ = new views::Link(
      UTF16ToWide(l10n_util::GetStringUTF16(IDS_PAGE_INFO_HELP_CENTER_LINK)));
  help_center_link_->set_listener(this);
  layout->AddView(help_center_link_);

  layout->Layout(this);
}

gfx::Size PageInfoBubbleView::GetPreferredSize() {
  gfx::Size size(views::Window::GetLocalizedContentsSize(
      IDS_PAGEINFOBUBBLE_WIDTH_CHARS, IDS_PAGEINFOBUBBLE_HEIGHT_LINES));
  size.set_height(0);

  int count = model_.GetSectionCount();
  for (int i = 0; i < count; ++i) {
    PageInfoModel::SectionInfo info = model_.GetSectionInfo(i);
    const SkBitmap* icon = *model_.GetIconImage(info.icon_id);
    Section section(this, info, icon, cert_id_ > 0);
    size.Enlarge(0, section.GetHeightForWidth(size.width()));
  }

  static int separator_plus_padding = GetSeparatorSize().height();

  // Account for the separators and padding within sections.
  size.Enlarge(0, (count - 1) * separator_plus_padding);

  // Account for the Help Center link and the separator above it.
  gfx::Size link_size = help_center_link_->GetPreferredSize();
  size.Enlarge(0, separator_plus_padding +
                  link_size.height());

  if (!resize_animation_.is_animating())
    return size;

  // We are animating from animation_start_height_ to size.
  int target_height = animation_start_height_ + static_cast<int>(
      (size.height() - animation_start_height_) * HeightAnimationValue());
  size.set_height(target_height);
  return size;
}

void PageInfoBubbleView::ModelChanged() {
  // The start height must take into account that when we start animating,
  // a separator plus padding is immediately added before the view is animated
  // into existence.
  animation_start_height_ = bounds().height() + GetSeparatorSize().height();
  LayoutSections();
  resize_animation_.SetSlideDuration(kPageInfoSlideDuration);
  resize_animation_.Show();
}

void PageInfoBubbleView::BubbleClosing(Bubble* bubble, bool closed_by_escape) {
  resize_animation_.Reset();
  bubble_ = NULL;
}

bool PageInfoBubbleView::CloseOnEscape() {
  return true;
}

bool PageInfoBubbleView::FadeInOnShow() {
  return false;
}

std::wstring PageInfoBubbleView::accessible_name() {
  return L"PageInfoBubble";
}

void PageInfoBubbleView::LinkClicked(views::Link* source, int event_flags) {
  // We want to make sure the info bubble closes once the link is activated.  So
  // we close it explicitly rather than relying on a side-effect of opening a
  // new tab (see http://crosbug.com/10186).
  bubble_->Close();

  GURL url = google_util::AppendGoogleLocaleParam(
      GURL(chrome::kPageInfoHelpCenterURL));
  Browser* browser = BrowserList::GetLastActive();
  browser->OpenURL(url, GURL(), NEW_FOREGROUND_TAB, PageTransition::LINK);
}

void PageInfoBubbleView::AnimationEnded(const ui::Animation* animation) {
  LayoutSections();
  bubble_->SizeToContents();
}

void PageInfoBubbleView::AnimationProgressed(const ui::Animation* animation) {
  LayoutSections();
  bubble_->SizeToContents();
}

////////////////////////////////////////////////////////////////////////////////
// Section

Section::Section(PageInfoBubbleView* owner,
                 const PageInfoModel::SectionInfo& section_info,
                 const SkBitmap* state_icon,
                 bool show_cert)
    : owner_(owner),
      info_(section_info),
      status_image_(NULL),
      link_(NULL) {
  if (state_icon) {
    status_image_ = new views::ImageView();
    status_image_->SetImage(*state_icon);
    AddChildView(status_image_);
  }

  headline_label_ = new views::Label(UTF16ToWideHack(info_.headline));
  headline_label_->SetFont(
      headline_label_->font().DeriveFont(0, gfx::Font::BOLD));
  headline_label_->set_background(
      views::Background::CreateSolidBackground(SK_ColorWHITE));
  headline_label_->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
  AddChildView(headline_label_);

  description_label_ = new views::Label(UTF16ToWideHack(info_.description));
  description_label_->set_background(
      views::Background::CreateSolidBackground(SK_ColorWHITE));
  description_label_->SetMultiLine(true);
  description_label_->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
  // Allow linebreaking in the middle of words if necessary, so that extremely
  // long hostnames (longer than one line) will still be completely shown.
  description_label_->SetAllowCharacterBreak(true);
  AddChildView(description_label_);

  if (info_.type == PageInfoModel::SECTION_INFO_IDENTITY && show_cert) {
    link_ = new views::Link(
        UTF16ToWide(l10n_util::GetStringUTF16(IDS_PAGEINFO_CERT_INFO_BUTTON)));
    link_->set_listener(this);
    AddChildView(link_);
  }
}

Section::~Section() {
}

void Section::SetAnimationStage(double animation_stage) {
  animation_value_ = animation_stage;
  SchedulePaint();
}

int Section::GetHeightForWidth(int width) {
  return LayoutItems(true, width).height();
}

void Section::Layout() {
  LayoutItems(false, width());
}

void Section::Paint(gfx::Canvas* canvas) {
  if (info_.type == PageInfoModel::SECTION_INFO_FIRST_VISIT) {
    // This section needs to be animated into view.
    canvas->SaveLayerAlpha(static_cast<int>(255.0 * OpacityAnimationValue()),
                           bounds());
  }

  views::View::Paint(canvas);

  if (info_.type == PageInfoModel::SECTION_INFO_FIRST_VISIT)
    canvas->Restore();
}

void Section::LinkClicked(views::Link* source, int event_flags) {
  owner_->ShowCertDialog();
}

gfx::Size Section::LayoutItems(bool compute_bounds_only, int width) {
  int x = kHGapToBorder;
  int y = kVGapToImage;

  // Layout the image, head-line and description.
  gfx::Size size;
  if (status_image_) {
      size = status_image_->GetPreferredSize();
    if (!compute_bounds_only)
      status_image_->SetBounds(x, y, size.width(), size.height());
  }
  int image_height = y + size.height();
  x += size.width() + kHGapImageToDescription;
  int w = width - x - kTextPaddingRight;
  y = kVGapToHeadline;
  if (!headline_label_->GetText().empty()) {
    size = headline_label_->GetPreferredSize();
    if (!compute_bounds_only)
      headline_label_->SetBounds(x, y, w > 0 ? w : 0, size.height());
    y += size.height();
  } else {
    if (!compute_bounds_only)
      headline_label_->SetBounds(x, y, 0, 0);
  }
  if (w > 0) {
    int height = description_label_->GetHeightForWidth(w);
    if (!compute_bounds_only)
      description_label_->SetBounds(x, y, w, height);
    y += height;
  } else {
    if (!compute_bounds_only)
      description_label_->SetBounds(x, y, 0, 0);
  }
  if (info_.type == PageInfoModel::SECTION_INFO_IDENTITY && link_) {
    size = link_->GetPreferredSize();
    if (!compute_bounds_only)
      link_->SetBounds(x, y, size.width(), size.height());
    y += size.height();
  }

  // Make sure the image is not truncated if the text doesn't contain much.
  y = std::max(y, image_height);
  return gfx::Size(width, y);
}

namespace browser {

void ShowPageInfoBubble(gfx::NativeWindow parent,
                        Profile* profile,
                        const GURL& url,
                        const NavigationEntry::SSLStatus& ssl,
                        bool show_history) {
  // Find where to point the bubble at.
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForNativeWindow(parent);
  gfx::Point point;
  if (base::i18n::IsRTL()) {
    int width = browser_view->toolbar()->location_bar()->width();
    point = gfx::Point(width - kIconHorizontalOffset, 0);
  }
  point.Offset(0, kIconVerticalOffset);
  views::View::ConvertPointToScreen(browser_view->toolbar()->location_bar(),
                                    &point);
  gfx::Rect bounds = browser_view->toolbar()->location_bar()->bounds();
  bounds.set_origin(point);
  bounds.set_width(kIconHorizontalOffset);

  // Show the bubble. If the bubble already exist - it will be closed first.
  PageInfoBubbleView* page_info_bubble =
      new PageInfoBubbleView(parent, profile, url, ssl, show_history);
  Bubble* bubble =
      Bubble::Show(browser_view->GetWidget(), bounds,
                   BubbleBorder::TOP_LEFT,
                   page_info_bubble, page_info_bubble);
  page_info_bubble->set_bubble(bubble);
}

}
