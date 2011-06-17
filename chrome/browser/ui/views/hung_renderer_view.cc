// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/browser_dialogs.h"

#include "base/i18n/rtl.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/logging_chrome.h"
#include "content/browser/renderer_host/render_process_host.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/common/result_codes.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "views/controls/button/native_button.h"
#include "views/controls/image_view.h"
#include "views/controls/label.h"
#include "views/controls/table/group_table_view.h"
#include "views/layout/grid_layout.h"
#include "views/layout/layout_constants.h"
#include "views/window/client_view.h"
#include "views/window/dialog_delegate.h"
#include "views/window/window.h"

class HungRendererDialogView;

namespace {
// We only support showing one of these at a time per app.
HungRendererDialogView* g_instance = NULL;
}

///////////////////////////////////////////////////////////////////////////////
// HungPagesTableModel

class HungPagesTableModel : public views::GroupTableModel {
 public:
  HungPagesTableModel();
  virtual ~HungPagesTableModel();

  void InitForTabContents(TabContents* hung_contents);

  // Overridden from views::GroupTableModel:
  virtual int RowCount();
  virtual string16 GetText(int row, int column_id);
  virtual SkBitmap GetIcon(int row);
  virtual void SetObserver(ui::TableModelObserver* observer);
  virtual void GetGroupRangeForItem(int item, views::GroupRange* range);

 private:
  typedef std::vector<TabContents*> TabContentsVector;
  TabContentsVector tab_contentses_;

  ui::TableModelObserver* observer_;

  DISALLOW_COPY_AND_ASSIGN(HungPagesTableModel);
};

///////////////////////////////////////////////////////////////////////////////
// HungPagesTableModel, public:

HungPagesTableModel::HungPagesTableModel() : observer_(NULL) {
}

HungPagesTableModel::~HungPagesTableModel() {
}

void HungPagesTableModel::InitForTabContents(TabContents* hung_contents) {
  tab_contentses_.clear();
  for (TabContentsIterator it; !it.done(); ++it) {
    if (it->tab_contents()->GetRenderProcessHost() ==
        hung_contents->GetRenderProcessHost())
      tab_contentses_.push_back((*it)->tab_contents());
  }
  // The world is different.
  if (observer_)
    observer_->OnModelChanged();
}

///////////////////////////////////////////////////////////////////////////////
// HungPagesTableModel, views::GroupTableModel implementation:

int HungPagesTableModel::RowCount() {
  return static_cast<int>(tab_contentses_.size());
}

string16 HungPagesTableModel::GetText(int row, int column_id) {
  DCHECK(row >= 0 && row < RowCount());
  string16 title = tab_contentses_[row]->GetTitle();
  if (title.empty())
    title = TabContentsWrapper::GetDefaultTitle();
  // TODO(xji): Consider adding a special case if the title text is a URL,
  // since those should always have LTR directionality. Please refer to
  // http://crbug.com/6726 for more information.
  base::i18n::AdjustStringForLocaleDirection(&title);
  return title;
}

SkBitmap HungPagesTableModel::GetIcon(int row) {
  DCHECK(row >= 0 && row < RowCount());
  return tab_contentses_.at(row)->GetFavicon();
}

void HungPagesTableModel::SetObserver(ui::TableModelObserver* observer) {
  observer_ = observer;
}

void HungPagesTableModel::GetGroupRangeForItem(int item,
                                               views::GroupRange* range) {
  DCHECK(range);
  range->start = 0;
  range->length = RowCount();
}

///////////////////////////////////////////////////////////////////////////////
// HungRendererDialogView

class HungRendererDialogView : public views::View,
                               public views::DialogDelegate,
                               public views::ButtonListener {
 public:
  HungRendererDialogView();
  ~HungRendererDialogView();

  void ShowForTabContents(TabContents* contents);
  void EndForTabContents(TabContents* contents);

  // views::WindowDelegate overrides:
  virtual std::wstring GetWindowTitle() const;
  virtual void WindowClosing();
  virtual int GetDialogButtons() const;
  virtual std::wstring GetDialogButtonLabel(
      MessageBoxFlags::DialogButton button) const;
  virtual views::View* GetExtraView();
  virtual bool Accept(bool window_closing);
  virtual views::View* GetContentsView();

  // views::ButtonListener overrides:
  virtual void ButtonPressed(views::Button* sender, const views::Event& event);

 protected:
  // views::View overrides:
  virtual void ViewHierarchyChanged(bool is_add,
                                    views::View* parent,
                                    views::View* child);

 private:
  // Initialize the controls in this dialog.
  void Init();
  void CreateKillButtonView();

  // Returns the bounds the dialog should be displayed at to be meaningfully
  // associated with the specified TabContents.
  gfx::Rect GetDisplayBounds(TabContents* contents);

  static void InitClass();

  // Controls within the dialog box.
  views::ImageView* frozen_icon_view_;
  views::Label* info_label_;
  views::GroupTableView* hung_pages_table_;

  // The button we insert into the ClientView to kill the errant process. This
  // is parented to a container view that uses a grid layout to align it
  // properly.
  views::NativeButton* kill_button_;
  class ButtonContainer : public views::View {
   public:
    ButtonContainer() {}
    virtual ~ButtonContainer() {}
   private:
    DISALLOW_COPY_AND_ASSIGN(ButtonContainer);
  };
  ButtonContainer* kill_button_container_;

  // The model that provides the contents of the table that shows a list of
  // pages affected by the hang.
  scoped_ptr<HungPagesTableModel> hung_pages_table_model_;

  // The TabContents that we detected had hung in the first place resulting in
  // the display of this view.
  TabContents* contents_;

  // Whether or not we've created controls for ourself.
  bool initialized_;

  // An amusing icon image.
  static SkBitmap* frozen_icon_;

  DISALLOW_COPY_AND_ASSIGN(HungRendererDialogView);
};

// static
SkBitmap* HungRendererDialogView::frozen_icon_ = NULL;

// The distance in pixels from the top of the relevant contents to place the
// warning window.
static const int kOverlayContentsOffsetY = 50;

// The dimensions of the hung pages list table view, in pixels.
static const int kTableViewWidth = 300;
static const int kTableViewHeight = 100;

///////////////////////////////////////////////////////////////////////////////
// HungRendererDialogView, public:

HungRendererDialogView::HungRendererDialogView()
    : frozen_icon_view_(NULL),
      info_label_(NULL),
      hung_pages_table_(NULL),
      kill_button_(NULL),
      kill_button_container_(NULL),
      contents_(NULL),
      initialized_(false) {
  InitClass();
}

HungRendererDialogView::~HungRendererDialogView() {
  hung_pages_table_->SetModel(NULL);
}

void HungRendererDialogView::ShowForTabContents(TabContents* contents) {
  DCHECK(contents && window());
  contents_ = contents;

  // Don't show the warning unless the foreground window is the frame, or this
  // window (but still invisible). If the user has another window or
  // application selected, activating ourselves is rude.
  HWND frame_hwnd = GetAncestor(contents->GetNativeView(), GA_ROOT);
  HWND foreground_window = GetForegroundWindow();
  if (foreground_window != frame_hwnd &&
      foreground_window != window()->GetNativeWindow()) {
    return;
  }

  if (!window()->IsActive()) {
    volatile TabContents* passed_c = contents;
    volatile TabContents* this_contents = contents_;

    gfx::Rect bounds = GetDisplayBounds(contents);
    window()->SetWindowBounds(bounds, frame_hwnd);

    // We only do this if the window isn't active (i.e. hasn't been shown yet,
    // or is currently shown but deactivated for another TabContents). This is
    // because this window is a singleton, and it's possible another active
    // renderer may hang while this one is showing, and we don't want to reset
    // the list of hung pages for a potentially unrelated renderer while this
    // one is showing.
    hung_pages_table_model_->InitForTabContents(contents);
    window()->Show();
  }
}

void HungRendererDialogView::EndForTabContents(TabContents* contents) {
  DCHECK(contents);
  if (contents_ && contents_->GetRenderProcessHost() ==
      contents->GetRenderProcessHost()) {
    window()->CloseWindow();
    // Since we're closing, we no longer need this TabContents.
    contents_ = NULL;
  }
}

///////////////////////////////////////////////////////////////////////////////
// HungRendererDialogView, views::DialogDelegate implementation:

std::wstring HungRendererDialogView::GetWindowTitle() const {
  return UTF16ToWide(
      l10n_util::GetStringUTF16(IDS_BROWSER_HANGMONITOR_RENDERER_TITLE));
}

void HungRendererDialogView::WindowClosing() {
  // We are going to be deleted soon, so make sure our instance is destroyed.
  g_instance = NULL;
}

int HungRendererDialogView::GetDialogButtons() const {
  // We specifically don't want a CANCEL button here because that code path is
  // also called when the window is closed by the user clicking the X button in
  // the window's titlebar, and also if we call Window::Close. Rather, we want
  // the OK button to wait for responsiveness (and close the dialog) and our
  // additional button (which we create) to kill the process (which will result
  // in the dialog being destroyed).
  return MessageBoxFlags::DIALOGBUTTON_OK;
}

std::wstring HungRendererDialogView::GetDialogButtonLabel(
    MessageBoxFlags::DialogButton button) const {
  if (button == MessageBoxFlags::DIALOGBUTTON_OK)
    return UTF16ToWide(
        l10n_util::GetStringUTF16(IDS_BROWSER_HANGMONITOR_RENDERER_WAIT));
  return std::wstring();
}

views::View* HungRendererDialogView::GetExtraView() {
  return kill_button_container_;
}

bool HungRendererDialogView::Accept(bool window_closing) {
  // Don't do anything if we're being called only because the dialog is being
  // destroyed and we don't supply a Cancel function...
  if (window_closing)
    return true;

  // Start waiting again for responsiveness.
  if (contents_ && contents_->render_view_host())
    contents_->render_view_host()->RestartHangMonitorTimeout();
  return true;
}

views::View* HungRendererDialogView::GetContentsView() {
  return this;
}

///////////////////////////////////////////////////////////////////////////////
// HungRendererDialogView, views::ButtonListener implementation:

void HungRendererDialogView::ButtonPressed(
    views::Button* sender, const views::Event& event) {
  if (sender == kill_button_) {
    if (contents_ && contents_->GetRenderProcessHost()) {
      // Kill the process.
      TerminateProcess(contents_->GetRenderProcessHost()->GetHandle(),
                       ResultCodes::HUNG);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// HungRendererDialogView, views::View overrides:

void HungRendererDialogView::ViewHierarchyChanged(bool is_add,
                                                  views::View* parent,
                                                  views::View* child) {
  if (!initialized_ && is_add && child == this && GetWidget())
    Init();
}

///////////////////////////////////////////////////////////////////////////////
// HungRendererDialogView, private:

void HungRendererDialogView::Init() {
  frozen_icon_view_ = new views::ImageView;
  frozen_icon_view_->SetImage(frozen_icon_);

  info_label_ = new views::Label(
      UTF16ToWide(l10n_util::GetStringUTF16(IDS_BROWSER_HANGMONITOR_RENDERER)));
  info_label_->SetMultiLine(true);
  info_label_->SetHorizontalAlignment(views::Label::ALIGN_LEFT);

  hung_pages_table_model_.reset(new HungPagesTableModel);
  std::vector<TableColumn> columns;
  columns.push_back(TableColumn());
  hung_pages_table_ = new views::GroupTableView(
      hung_pages_table_model_.get(), columns, views::ICON_AND_TEXT, true,
      false, true, false);
  hung_pages_table_->SetPreferredSize(
    gfx::Size(kTableViewWidth, kTableViewHeight));

  CreateKillButtonView();

  using views::GridLayout;
  using views::ColumnSet;

  GridLayout* layout = GridLayout::CreatePanel(this);
  SetLayoutManager(layout);

  const int double_column_set_id = 0;
  ColumnSet* column_set = layout->AddColumnSet(double_column_set_id);
  column_set->AddColumn(GridLayout::LEADING, GridLayout::LEADING, 0,
                        GridLayout::FIXED, frozen_icon_->width(), 0);
  column_set->AddPaddingColumn(
      0, views::kUnrelatedControlLargeHorizontalSpacing);
  column_set->AddColumn(GridLayout::FILL, GridLayout::FILL, 1,
                        GridLayout::USE_PREF, 0, 0);

  layout->StartRow(0, double_column_set_id);
  layout->AddView(frozen_icon_view_, 1, 3);
  layout->AddView(info_label_);

  layout->AddPaddingRow(0, views::kUnrelatedControlVerticalSpacing);

  layout->StartRow(0, double_column_set_id);
  layout->SkipColumns(1);
  layout->AddView(hung_pages_table_);

  initialized_ = true;
}

void HungRendererDialogView::CreateKillButtonView() {
  kill_button_ = new views::NativeButton(this, UTF16ToWide(
      l10n_util::GetStringUTF16(IDS_BROWSER_HANGMONITOR_RENDERER_END)));

  kill_button_container_ = new ButtonContainer;

  using views::GridLayout;
  using views::ColumnSet;

  GridLayout* layout = new GridLayout(kill_button_container_);
  kill_button_container_->SetLayoutManager(layout);

  const int single_column_set_id = 0;
  ColumnSet* column_set = layout->AddColumnSet(single_column_set_id);
  column_set->AddPaddingColumn(0, frozen_icon_->width() +
      views::kPanelHorizMargin + views::kUnrelatedControlHorizontalSpacing);
  column_set->AddColumn(GridLayout::LEADING, GridLayout::LEADING, 0,
                        GridLayout::USE_PREF, 0, 0);

  layout->StartRow(0, single_column_set_id);
  layout->AddView(kill_button_);
}

gfx::Rect HungRendererDialogView::GetDisplayBounds(
    TabContents* contents) {
  HWND contents_hwnd = contents->GetNativeView();
  RECT contents_bounds_rect;
  GetWindowRect(contents_hwnd, &contents_bounds_rect);
  gfx::Rect contents_bounds(contents_bounds_rect);
  gfx::Rect window_bounds = window()->GetBounds();

  int window_x = contents_bounds.x() +
      (contents_bounds.width() - window_bounds.width()) / 2;
  int window_y = contents_bounds.y() + kOverlayContentsOffsetY;
  return gfx::Rect(window_x, window_y, window_bounds.width(),
                   window_bounds.height());
}

// static
void HungRendererDialogView::InitClass() {
  static bool initialized = false;
  if (!initialized) {
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    frozen_icon_ = rb.GetBitmapNamed(IDR_FROZEN_TAB_ICON);
    initialized = true;
  }
}

static HungRendererDialogView* CreateHungRendererDialogView() {
  HungRendererDialogView* cv = new HungRendererDialogView;
  views::Window::CreateChromeWindow(NULL, gfx::Rect(), cv);
  return cv;
}

namespace browser {

void ShowHungRendererDialog(TabContents* contents) {
  if (!logging::DialogsAreSuppressed()) {
    if (!g_instance)
      g_instance = CreateHungRendererDialogView();
    g_instance->ShowForTabContents(contents);
  }
}

void HideHungRendererDialog(TabContents* contents) {
  if (!logging::DialogsAreSuppressed() && g_instance)
    g_instance->EndForTabContents(contents);
}

}  // namespace browser
