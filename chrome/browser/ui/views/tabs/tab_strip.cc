// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/tab_strip.h"

#include <algorithm>
#include <iterator>
#include <vector>

#if defined(OS_WIN)
#include <windowsx.h>
#endif

#include "base/compiler_specific.h"
#include "base/stl_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/tabs/tab_strip_selection_model.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab_drag_controller.h"
#include "chrome/browser/ui/views/tabs/tab_strip_controller.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/theme_resources_standard.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/base/animation/animation_container.h"
#include "ui/base/animation/throb_animation.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/path.h"
#include "ui/gfx/skbitmap_operations.h"
#include "ui/gfx/size.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/widget/default_theme_provider.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

#if defined(OS_WIN)
#include "ui/base/win/hwnd_util.h"
#include "ui/views/widget/monitor_win.h"
#endif

using views::DropTargetEvent;

static const int kNewTabButtonHOffset = -5;
static const int kNewTabButtonVOffset = 5;
static const int kSuspendAnimationsTimeMs = 200;
static const int kTabHOffset = -16;
static const int kTabStripAnimationVSlop = 40;
// Inactive tabs in a native frame are slightly transparent.
static const int kNativeFrameInactiveTabAlpha = 200;
// If there are multiple tabs selected then make non-selected inactive tabs
// even more transparent.
static const int kNativeFrameInactiveTabAlphaMultiSelection = 150;

// Inverse ratio of the width of a tab edge to the width of the tab. When
// hovering over the left or right edge of a tab, the drop indicator will
// point between tabs.
static const int kTabEdgeRatioInverse = 4;

// Size of the drop indicator.
static int drop_indicator_width;
static int drop_indicator_height;

static inline int Round(double x) {
  // Why oh why is this not in a standard header?
  return static_cast<int>(floor(x + 0.5));
}

namespace {

// Animation delegate used when a dragged tab is released. When done sets the
// dragging state to false.
class ResetDraggingStateDelegate
    : public views::BoundsAnimator::OwnedAnimationDelegate {
 public:
  explicit ResetDraggingStateDelegate(BaseTab* tab) : tab_(tab) {
  }

  virtual void AnimationEnded(const ui::Animation* animation) {
    tab_->set_dragging(false);
  }

  virtual void AnimationCanceled(const ui::Animation* animation) {
    tab_->set_dragging(false);
  }

 private:
  BaseTab* tab_;

  DISALLOW_COPY_AND_ASSIGN(ResetDraggingStateDelegate);
};

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// NewTabButton
//
//  A subclass of button that hit-tests to the shape of the new tab button and
//  does custom drawing.

class NewTabButton : public views::ImageButton {
 public:
  NewTabButton(TabStrip* tab_strip, views::ButtonListener* listener);
  virtual ~NewTabButton();

  // Set the background offset used to match the background image to the frame
  // image.
  void set_background_offset(const gfx::Point& offset) {
    background_offset_ = offset;
  }

 protected:
  // Overridden from views::View:
  virtual bool HasHitTestMask() const OVERRIDE;
  virtual void GetHitTestMask(gfx::Path* path) const OVERRIDE;
#if defined(OS_WIN) && !defined(USE_AURA)
  void OnMouseReleased(const views::MouseEvent& event) OVERRIDE;
#endif
  void OnPaint(gfx::Canvas* canvas) OVERRIDE;

 private:
  SkBitmap GetBitmapForState(views::CustomButton::ButtonState state) const;
  SkBitmap GetBitmap() const;

  // Tab strip that contains this button.
  TabStrip* tab_strip_;

  // The offset used to paint the background image.
  gfx::Point background_offset_;

  DISALLOW_COPY_AND_ASSIGN(NewTabButton);
};

NewTabButton::NewTabButton(TabStrip* tab_strip, views::ButtonListener* listener)
    : views::ImageButton(listener),
      tab_strip_(tab_strip) {
}

NewTabButton::~NewTabButton() {
}

bool NewTabButton::HasHitTestMask() const {
  // When the button is sized to the top of the tab strip we want the user to
  // be able to click on complete bounds, and so don't return a custom hit
  // mask.
  return !tab_strip_->SizeTabButtonToTopOfTabStrip();
}

void NewTabButton::GetHitTestMask(gfx::Path* path) const {
  DCHECK(path);

  SkScalar w = SkIntToScalar(width());

  // These values are defined by the shape of the new tab bitmap. Should that
  // bitmap ever change, these values will need to be updated. They're so
  // custom it's not really worth defining constants for.
  path->moveTo(0, 1);
  path->lineTo(w - 7, 1);
  path->lineTo(w - 4, 4);
  path->lineTo(w, 16);
  path->lineTo(w - 1, 17);
  path->lineTo(7, 17);
  path->lineTo(4, 13);
  path->lineTo(0, 1);
  path->close();
}

#if defined(OS_WIN) && !defined(USE_AURA)
void NewTabButton::OnMouseReleased(const views::MouseEvent& event) {
  if (event.IsOnlyRightMouseButton()) {
    gfx::Point point(event.x(), event.y());
    views::View::ConvertPointToScreen(this, &point);
    ui::ShowSystemMenu(GetWidget()->GetNativeView(), point.x(), point.y());
    SetState(BS_NORMAL);
    return;
  }
  views::ImageButton::OnMouseReleased(event);
}
#endif

void NewTabButton::OnPaint(gfx::Canvas* canvas) {
  SkBitmap bitmap = GetBitmap();
  canvas->DrawBitmapInt(bitmap, 0, height() - bitmap.height());
}

SkBitmap NewTabButton::GetBitmapForState(
    views::CustomButton::ButtonState state) const {
  bool use_native_frame =
      GetWidget() && GetWidget()->GetTopLevelWidget()->ShouldUseNativeFrame();
  int background_id = 0;
  if (use_native_frame) {
    background_id = IDR_THEME_TAB_BACKGROUND_V;
  } else {
    background_id = tab_strip_->controller()->IsIncognito() ?
        IDR_THEME_TAB_BACKGROUND_INCOGNITO : IDR_THEME_TAB_BACKGROUND;
  }

  int overlay_id = 0;
  int alpha = 0;
  switch (state) {
    case views::CustomButton::BS_NORMAL:
      overlay_id = IDR_NEWTAB_BUTTON;
      alpha = use_native_frame ? kNativeFrameInactiveTabAlpha : 255;
      break;
    case views::CustomButton::BS_HOT:
      overlay_id = IDR_NEWTAB_BUTTON;
      alpha = use_native_frame ? kNativeFrameInactiveTabAlpha : 255;
      break;
    case views::CustomButton::BS_PUSHED:
      overlay_id = IDR_NEWTAB_BUTTON_P;
      alpha = 145;
      break;
    default:
      NOTREACHED();
      break;
  }

  ui::ThemeProvider* tp = GetThemeProvider();
  SkBitmap* background = tp->GetBitmapNamed(background_id);
  SkBitmap* overlay = tp->GetBitmapNamed(overlay_id);
  int height = overlay->height();
  int width = overlay->width();

  gfx::CanvasSkia canvas(gfx::Size(width, height), false);

  // For custom images the background starts at the top of the tab strip.
  // Otherwise the background starts at the top of the frame.
  int offset_y = GetThemeProvider()->HasCustomImage(background_id) ?
      0 : background_offset_.y();
  canvas.TileImageInt(*background, GetMirroredX() + background_offset_.x(),
                      kNewTabButtonVOffset + offset_y, 0, 0, width, height);

  if (alpha != 255) {
    SkPaint paint;
    paint.setColor(SkColorSetARGB(alpha, 255, 255, 255));
    paint.setXfermodeMode(SkXfermode::kDstIn_Mode);
    paint.setStyle(SkPaint::kFill_Style);
    canvas.DrawRect(gfx::Rect(0, 0, width, height), paint);
  }

  if (state == views::CustomButton::BS_HOT) {
    canvas.FillRect(SkColorSetARGB(64, 255, 255, 255),
                    gfx::Rect(size()));
  }

  canvas.DrawBitmapInt(*overlay, 0, 0);
  SkBitmap* mask = tp->GetBitmapNamed(IDR_NEWTAB_BUTTON_MASK);
  return SkBitmapOperations::CreateMaskedBitmap(canvas.ExtractBitmap(), *mask);
}

SkBitmap NewTabButton::GetBitmap() const {
  if (!hover_animation_->is_animating())
    return GetBitmapForState(state());
  return SkBitmapOperations::CreateBlendedBitmap(
      GetBitmapForState(views::CustomButton::BS_NORMAL),
      GetBitmapForState(views::CustomButton::BS_HOT),
      hover_animation_->GetCurrentValue());
}

///////////////////////////////////////////////////////////////////////////////
// TabStrip::RemoveTabDelegate
//
// AnimationDelegate used when removing a tab. Does the necessary cleanup when
// done.
class TabStrip::RemoveTabDelegate
    : public views::BoundsAnimator::OwnedAnimationDelegate {
 public:
  RemoveTabDelegate(TabStrip* tab_strip, BaseTab* tab);

  virtual void AnimationEnded(const ui::Animation* animation) OVERRIDE;
  virtual void AnimationCanceled(const ui::Animation* animation) OVERRIDE;

 private:
  void CompleteRemove();

  // When the animation completes, we send the Container a message to simulate
  // a mouse moved event at the current mouse position. This tickles the Tab
  // the mouse is currently over to show the "hot" state of the close button.
  void HighlightCloseButton();

  TabStrip* tabstrip_;
  BaseTab* tab_;

  DISALLOW_COPY_AND_ASSIGN(RemoveTabDelegate);
};

TabStrip::RemoveTabDelegate::RemoveTabDelegate(TabStrip* tab_strip,
                                               BaseTab* tab)
    : tabstrip_(tab_strip),
      tab_(tab) {
}

void TabStrip::RemoveTabDelegate::AnimationEnded(
    const ui::Animation* animation) {
  CompleteRemove();
}

void TabStrip::RemoveTabDelegate::AnimationCanceled(
    const ui::Animation* animation) {
  // We can be canceled for two interesting reasons:
  // . The tab we reference was dragged back into the tab strip. In this case
  //   we don't want to remove the tab (closing is false).
  // . The drag was completed before the animation completed
  //   (DestroyDraggedSourceTab). In this case we need to remove the tab
  //   (closing is true).
  if (tab_->closing())
    CompleteRemove();
}

void TabStrip::RemoveTabDelegate::CompleteRemove() {
  if (!tab_->closing()) {
    // The tab was added back yet we weren't canceled. This shouldn't happen.
    NOTREACHED();
    return;
  }
  tabstrip_->RemoveAndDeleteTab(tab_);
  HighlightCloseButton();
}

void TabStrip::RemoveTabDelegate::HighlightCloseButton() {
  if (tabstrip_->IsDragSessionActive() ||
      !tabstrip_->ShouldHighlightCloseButtonAfterRemove()) {
    // This function is not required (and indeed may crash!) for removes
    // spawned by non-mouse closes and drag-detaches.
    return;
  }

#if defined(OS_WIN) && !defined(USE_AURA)
  views::Widget* widget = tabstrip_->GetWidget();
  // This can be null during shutdown. See http://crbug.com/42737.
  if (!widget)
    return;

  widget->ResetLastMouseMoveFlag();

  // Force the close button (that slides under the mouse) to highlight by
  // saying the mouse just moved, but sending the same coordinates.
  DWORD pos = GetMessagePos();
  POINT cursor_point = {GET_X_LPARAM(pos), GET_Y_LPARAM(pos)};
  MapWindowPoints(NULL, widget->GetNativeView(), &cursor_point, 1);
  SendMessage(widget->GetNativeView(), WM_MOUSEMOVE, 0,
              MAKELPARAM(cursor_point.x, cursor_point.y));
#else
  NOTIMPLEMENTED();
#endif
}

///////////////////////////////////////////////////////////////////////////////
// TabStrip, public:

// static
const int TabStrip::kMiniToNonMiniGap = 3;

TabStrip::TabStrip(TabStripController* controller)
    : controller_(controller),
      newtab_button_(NULL),
      current_unselected_width_(Tab::GetStandardSize().width()),
      current_selected_width_(Tab::GetStandardSize().width()),
      available_width_for_tabs_(-1),
      in_tab_close_(false),
      animation_container_(new ui::AnimationContainer()),
      attaching_dragged_tab_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(bounds_animator_(this)) {
  Init();
}

TabStrip::~TabStrip() {
  // The animations may reference the tabs. Shut down the animation before we
  // delete the tabs.
  StopAnimating(false);

  DestroyDragController();

  // Make sure we unhook ourselves as a message loop observer so that we don't
  // crash in the case where the user closes the window after closing a tab
  // but before moving the mouse.
  RemoveMessageLoopObserver();

  // The children (tabs) may callback to us from their destructor. Delete them
  // so that if they call back we aren't in a weird state.
  RemoveAllChildViews(true);
}

gfx::Rect TabStrip::GetNewTabButtonBounds() {
  return newtab_button_->bounds();
}

bool TabStrip::SizeTabButtonToTopOfTabStrip() {
  return browser_defaults::kSizeTabButtonToTopOfTabStrip ||
      (GetWidget() && GetWidget()->IsMaximized());
}

void TabStrip::StartHighlight(int model_index) {
  GetTabAtModelIndex(model_index)->StartPulse();
}

void TabStrip::StopAllHighlighting() {
  for (int i = 0; i < tab_count(); ++i)
    GetTabAtTabDataIndex(i)->StopPulse();
}

void TabStrip::AddTabAt(int model_index, const TabRendererData& data) {
  BaseTab* tab = CreateTab();
  tab->SetData(data);

  TabData d = { tab, gfx::Rect() };
  tab_data_.insert(tab_data_.begin() + ModelIndexToTabIndex(model_index), d);

  AddChildView(tab);

  // Don't animate the first tab, it looks weird, and don't animate anything
  // if the containing window isn't visible yet.
  if (tab_count() > 1 && GetWidget() && GetWidget()->IsVisible())
    StartInsertTabAnimation(model_index);
  else
    DoLayout();
}

void TabStrip::MoveTab(int from_model_index, int to_model_index) {
  int from_tab_data_index = ModelIndexToTabIndex(from_model_index);
  BaseTab* tab = tab_data_[from_tab_data_index].tab;
  tab_data_.erase(tab_data_.begin() + from_tab_data_index);

  TabData data = {tab, gfx::Rect()};
  int to_tab_data_index = ModelIndexToTabIndex(to_model_index);
  tab_data_.insert(tab_data_.begin() + to_tab_data_index, data);

  StartMoveTabAnimation();
}

void TabStrip::RemoveTabAt(int model_index) {
  if (in_tab_close_ && model_index != GetModelCount())
    StartMouseInitiatedRemoveTabAnimation(model_index);
  else
    StartRemoveTabAnimation(model_index);
}

void TabStrip::SetTabData(int model_index, const TabRendererData& data) {
  BaseTab* tab = GetBaseTabAtModelIndex(model_index);
  bool mini_state_changed = tab->data().mini != data.mini;
  tab->SetData(data);

  if (mini_state_changed) {
    if (GetWidget() && GetWidget()->IsVisible())
      StartMiniTabAnimation();
    else
      DoLayout();
  }
}

void TabStrip::PrepareForCloseAt(int model_index) {
  if (!in_tab_close_ && IsAnimating()) {
    // Cancel any current animations. We do this as remove uses the current
    // ideal bounds and we need to know ideal bounds is in a good state.
    StopAnimating(true);
  }

  int model_count = GetModelCount();
  if (model_index + 1 != model_count && model_count > 1) {
    // The user is about to close a tab other than the last tab. Set
    // available_width_for_tabs_ so that if we do a layout we don't position a
    // tab past the end of the second to last tab. We do this so that as the
    // user closes tabs with the mouse a tab continues to fall under the mouse.
    Tab* last_tab = GetTabAtModelIndex(model_count - 1);
    Tab* tab_being_removed = GetTabAtModelIndex(model_index);
    available_width_for_tabs_ = last_tab->x() + last_tab->width() -
        tab_being_removed->width() - kTabHOffset;
    if (model_index == 0 && tab_being_removed->data().mini &&
        !GetTabAtModelIndex(1)->data().mini) {
      available_width_for_tabs_ -= kMiniToNonMiniGap;
    }
  }

  in_tab_close_ = true;
  AddMessageLoopObserver();
}

void TabStrip::SetSelection(const TabStripSelectionModel& old_selection,
                            const TabStripSelectionModel& new_selection) {
  // We have "tiny tabs" if the tabs are so tiny that the unselected ones are
  // a different size to the selected ones.
  bool tiny_tabs = current_unselected_width_ != current_selected_width_;
  if (!IsAnimating() && (!in_tab_close_ || tiny_tabs)) {
    DoLayout();
  } else {
    SchedulePaint();
  }

  TabStripSelectionModel::SelectedIndices no_longer_selected;
  std::insert_iterator<TabStripSelectionModel::SelectedIndices>
      it(no_longer_selected, no_longer_selected.begin());
  std::set_difference(old_selection.selected_indices().begin(),
                      old_selection.selected_indices().end(),
                      new_selection.selected_indices().begin(),
                      new_selection.selected_indices().end(),
                      it);
  for (size_t i = 0; i < no_longer_selected.size(); ++i) {
    GetTabAtTabDataIndex(ModelIndexToTabIndex(no_longer_selected[i]))->
        StopMiniTabTitleAnimation();
  }
}

void TabStrip::TabTitleChangedNotLoading(int model_index) {
  Tab* tab = GetTabAtModelIndex(model_index);
  if (tab->data().mini && !tab->IsActive())
    tab->StartMiniTabTitleAnimation();
}

BaseTab* TabStrip::GetBaseTabAtModelIndex(int model_index) const {
  return base_tab_at_tab_index(ModelIndexToTabIndex(model_index));
}

int TabStrip::GetModelIndexOfBaseTab(const BaseTab* tab) const {
  for (int i = 0, model_index = 0; i < tab_count(); ++i) {
    BaseTab* current_tab = base_tab_at_tab_index(i);
    if (!current_tab->closing()) {
      if (current_tab == tab)
        return model_index;
      model_index++;
    } else if (current_tab == tab) {
      return -1;
    }
  }
  return -1;
}

int TabStrip::GetModelCount() const {
  return controller_->GetCount();
}

bool TabStrip::IsValidModelIndex(int model_index) const {
  return controller_->IsValidIndex(model_index);
}

int TabStrip::ModelIndexToTabIndex(int model_index) const {
  int current_model_index = 0;
  for (int i = 0; i < tab_count(); ++i) {
    if (!base_tab_at_tab_index(i)->closing()) {
      if (current_model_index == model_index)
        return i;
      current_model_index++;
    }
  }
  return static_cast<int>(tab_data_.size());
}

BaseTab* TabStrip::CreateTabForDragging() {
  Tab* tab = new Tab(NULL);
  // Make sure the dragged tab shares our theme provider. We need to explicitly
  // do this as during dragging there isn't a theme provider.
  tab->set_theme_provider(GetThemeProvider());
  return tab;
}

bool TabStrip::IsDragSessionActive() const {
  return drag_controller_.get() != NULL;
}

bool TabStrip::IsActiveDropTarget() const {
  for (int i = 0; i < tab_count(); ++i) {
    BaseTab* tab = base_tab_at_tab_index(i);
    if (tab->dragging())
      return true;
  }
  return false;
}

const TabStripSelectionModel& TabStrip::GetSelectionModel() {
  return controller_->GetSelectionModel();
}

void TabStrip::SelectTab(BaseTab* tab) {
  int model_index = GetModelIndexOfBaseTab(tab);
  if (IsValidModelIndex(model_index))
    controller_->SelectTab(model_index);
}

void TabStrip::ExtendSelectionTo(BaseTab* tab) {
  int model_index = GetModelIndexOfBaseTab(tab);
  if (IsValidModelIndex(model_index))
    controller_->ExtendSelectionTo(model_index);
}

void TabStrip::ToggleSelected(BaseTab* tab) {
  int model_index = GetModelIndexOfBaseTab(tab);
  if (IsValidModelIndex(model_index))
    controller_->ToggleSelected(model_index);
}

void TabStrip::AddSelectionFromAnchorTo(BaseTab* tab) {
  int model_index = GetModelIndexOfBaseTab(tab);
  if (IsValidModelIndex(model_index))
    controller_->AddSelectionFromAnchorTo(model_index);
}

void TabStrip::CloseTab(BaseTab* tab) {
  // Find the closest model index. We do this so that the user can rapdily close
  // tabs and have the close click close the next tab.
  int model_index = 0;
  for (int i = 0; i < tab_count(); ++i) {
    BaseTab* current_tab = base_tab_at_tab_index(i);
    if (current_tab == tab)
      break;
    if (!current_tab->closing())
      model_index++;
  }

  if (IsValidModelIndex(model_index))
    controller_->CloseTab(model_index);
}

void TabStrip::ShowContextMenuForTab(BaseTab* tab, const gfx::Point& p) {
  controller_->ShowContextMenuForTab(tab, p);
}

bool TabStrip::IsActiveTab(const BaseTab* tab) const {
  int model_index = GetModelIndexOfBaseTab(tab);
  return IsValidModelIndex(model_index) &&
      controller_->IsActiveTab(model_index);
}

bool TabStrip::IsTabSelected(const BaseTab* tab) const {
  int model_index = GetModelIndexOfBaseTab(tab);
  return IsValidModelIndex(model_index) &&
      controller_->IsTabSelected(model_index);
}

bool TabStrip::IsTabPinned(const BaseTab* tab) const {
  if (tab->closing())
    return false;

  int model_index = GetModelIndexOfBaseTab(tab);
  return IsValidModelIndex(model_index) &&
      controller_->IsTabPinned(model_index);
}

bool TabStrip::IsTabCloseable(const BaseTab* tab) const {
  int model_index = GetModelIndexOfBaseTab(tab);
  return !IsValidModelIndex(model_index) ||
      controller_->IsTabCloseable(model_index);
}

void TabStrip::MaybeStartDrag(
    BaseTab* tab,
    const views::MouseEvent& event,
    const TabStripSelectionModel& original_selection) {
  // Don't accidentally start any drag operations during animations if the
  // mouse is down... during an animation tabs are being resized automatically,
  // so the View system can misinterpret this easily if the mouse is down that
  // the user is dragging.
  if (IsAnimating() || tab->closing() ||
      controller_->HasAvailableDragActions() == 0) {
    return;
  }
  int model_index = GetModelIndexOfBaseTab(tab);
  if (!IsValidModelIndex(model_index)) {
    CHECK(false);
    return;
  }
  std::vector<BaseTab*> tabs;
  int size_to_selected = 0;
  int x = tab->GetMirroredXInView(event.x());
  int y = event.y();
  // Build the set of selected tabs to drag and calculate the offset from the
  // first selected tab.
  for (int i = 0; i < tab_count(); ++i) {
    BaseTab* other_tab = base_tab_at_tab_index(i);
    if (IsTabSelected(other_tab) && !other_tab->closing()) {
      tabs.push_back(other_tab);
      if (other_tab == tab) {
        size_to_selected = GetSizeNeededForTabs(tabs);
        x = size_to_selected - tab->width() + x;
      }
    }
  }
  DCHECK(!tabs.empty());
  DCHECK(std::find(tabs.begin(), tabs.end(), tab) != tabs.end());
  TabStripSelectionModel selection_model;
  if (!original_selection.IsSelected(model_index))
    selection_model.Copy(original_selection);
  // Delete the existing DragController before creating a new one. We do this as
  // creating the DragController remembers the TabContents delegates and we need
  // to make sure the existing DragController isn't still a delegate.
  drag_controller_.reset();
  drag_controller_.reset(TabDragController::Create(
      this, tab, tabs, gfx::Point(x, y), tab->GetMirroredXInView(event.x()),
      selection_model));
}

void TabStrip::ContinueDrag(const views::MouseEvent& event) {
  if (drag_controller_.get())
    drag_controller_->Drag();
}

bool TabStrip::EndDrag(bool canceled) {
  if (!drag_controller_.get())
    return false;
  bool started_drag = drag_controller_->GetStartedDrag();
  drag_controller_->EndDrag(canceled);
  return started_drag;
}

BaseTab* TabStrip::GetTabAt(BaseTab* tab,
                            const gfx::Point& tab_in_tab_coordinates) {
  gfx::Point local_point = tab_in_tab_coordinates;
  ConvertPointToView(tab, this, &local_point);

  views::View* view = GetEventHandlerForPoint(local_point);
  if (!view)
    return NULL;  // No tab contains the point.

  // Walk up the view hierarchy until we find a tab, or the TabStrip.
  while (view && view != this && view->id() != VIEW_ID_TAB)
    view = view->parent();

  return view && view->id() == VIEW_ID_TAB ? static_cast<BaseTab*>(view) : NULL;
}

void TabStrip::ClickActiveTab(const BaseTab* tab) const {
  DCHECK(IsActiveTab(tab));
  int index = GetModelIndexOfBaseTab(tab);
  if (controller() && IsValidModelIndex(index))
    controller()->ClickActiveTab(index);
}

void TabStrip::MouseMovedOutOfView() {
  ResizeLayoutTabs();
}

////////////////////////////////////////////////////////////////////////////////
// TabStrip, AbstractTabStripView implementation:

bool TabStrip::IsTabStripEditable() const {
  return !IsDragSessionActive() && !IsActiveDropTarget();
}

bool TabStrip::IsTabStripCloseable() const {
  return !IsDragSessionActive();
}

void TabStrip::UpdateLoadingAnimations() {
  controller_->UpdateLoadingAnimations();
}

bool TabStrip::IsPositionInWindowCaption(const gfx::Point& point) {
  views::View* v = GetEventHandlerForPoint(point);

  // If there is no control at this location, claim the hit was in the title
  // bar to get a move action.
  if (v == this)
    return true;

  // Check to see if the point is within the non-button parts of the new tab
  // button. The button has a non-rectangular shape, so if it's not in the
  // visual portions of the button we treat it as a click to the caption.
  gfx::Point point_in_newtab_coords(point);
  View::ConvertPointToView(this, newtab_button_, &point_in_newtab_coords);
  if (newtab_button_->GetLocalBounds().Contains(point_in_newtab_coords) &&
      !newtab_button_->HitTest(point_in_newtab_coords)) {
    return true;
  }

  // All other regions, including the new Tab button, should be considered part
  // of the containing Window's client area so that regular events can be
  // processed for them.
  return false;
}

void TabStrip::SetBackgroundOffset(const gfx::Point& offset) {
  for (int i = 0; i < tab_count(); ++i)
    GetTabAtTabDataIndex(i)->set_background_offset(offset);
  newtab_button_->set_background_offset(offset);
}

views::View* TabStrip::GetNewTabButton() {
  return newtab_button_;
}

///////////////////////////////////////////////////////////////////////////////
// TabStrip, views::View overrides:

void TabStrip::Layout() {
  // Only do a layout if our size changed.
  if (last_layout_size_ == size())
    return;
  if (IsDragSessionActive())
    return;
  DoLayout();
}

void TabStrip::PaintChildren(gfx::Canvas* canvas) {
  // Tabs are painted in reverse order, so they stack to the left.
  Tab* active_tab = NULL;
  std::vector<Tab*> tabs_dragging;
  std::vector<Tab*> selected_tabs;
  bool is_dragging = false;

  for (int i = tab_count() - 1; i >= 0; --i) {
    // We must ask the _Tab's_ model, not ourselves, because in some situations
    // the model will be different to this object, e.g. when a Tab is being
    // removed after its TabContents has been destroyed.
    Tab* tab = GetTabAtTabDataIndex(i);
    if (tab->dragging()) {
      is_dragging = true;
      if (tab->IsActive())
        active_tab = tab;
      else
        tabs_dragging.push_back(tab);
    } else if (!tab->IsActive()) {
      if (!tab->IsSelected())
        tab->Paint(canvas);
      else
        selected_tabs.push_back(tab);
    } else {
      active_tab = tab;
    }
  }

  if (GetWidget()->ShouldUseNativeFrame()) {
    bool multiple_tabs_selected = (!selected_tabs.empty() ||
                                   tabs_dragging.size() > 1);
    // Make sure non-active tabs are somewhat transparent.
    SkPaint paint;
    // If there are multiple tabs selected, fade non-selected tabs more to make
    // the selected tabs more noticable.
    int alpha = multiple_tabs_selected ?
        kNativeFrameInactiveTabAlphaMultiSelection :
        kNativeFrameInactiveTabAlpha;
    paint.setColor(SkColorSetARGB(alpha, 255, 255, 255));
    paint.setXfermodeMode(SkXfermode::kDstIn_Mode);
    paint.setStyle(SkPaint::kFill_Style);
    // The tabstrip area overlaps the toolbar area by 2 px.
    canvas->DrawRect(gfx::Rect(0, 0, width(), height() - 2), paint);
  }

  // Now selected but not active. We don't want these dimmed if using native
  // frame, so they're painted after initial pass.
  for (size_t i = 0; i < selected_tabs.size(); ++i)
    selected_tabs[i]->Paint(canvas);

  // Next comes the active tab.
  if (active_tab && !is_dragging)
    active_tab->Paint(canvas);

  // Paint the New Tab button.
  newtab_button_->Paint(canvas);

  // And the dragged tabs.
  for (size_t i = 0; i < tabs_dragging.size(); ++i)
    tabs_dragging[i]->Paint(canvas);

  // If the active tab is being dragged, it goes last.
  if (active_tab && is_dragging)
    active_tab->Paint(canvas);
}

gfx::Size TabStrip::GetPreferredSize() {
  // Report the minimum width as the size required for a single selected tab
  // plus the new tab button. Don't base it on the actual number of tabs because
  // it's undesirable to have the minimum window size change when a new tab is
  // opened.
  int needed_width = Tab::GetMinimumSelectedSize().width();
  needed_width += kNewTabButtonHOffset - kTabHOffset;
  needed_width += newtab_button_bounds_.width();
  return gfx::Size(needed_width, Tab::GetMinimumUnselectedSize().height());
}

void TabStrip::OnDragEntered(const DropTargetEvent& event) {
  // Force animations to stop, otherwise it makes the index calculation tricky.
  StopAnimating(true);

  UpdateDropIndex(event);
}

int TabStrip::OnDragUpdated(const DropTargetEvent& event) {
  UpdateDropIndex(event);
  return GetDropEffect(event);
}

void TabStrip::OnDragExited() {
  SetDropIndex(-1, false);
}

int TabStrip::OnPerformDrop(const DropTargetEvent& event) {
  if (!drop_info_.get())
    return ui::DragDropTypes::DRAG_NONE;

  const int drop_index = drop_info_->drop_index;
  const bool drop_before = drop_info_->drop_before;

  // Hide the drop indicator.
  SetDropIndex(-1, false);

  GURL url;
  string16 title;
  if (!event.data().GetURLAndTitle(&url, &title) || !url.is_valid())
    return ui::DragDropTypes::DRAG_NONE;

  controller()->PerformDrop(drop_before, drop_index, url);

  return GetDropEffect(event);
}

void TabStrip::GetAccessibleState(ui::AccessibleViewState* state) {
  state->role = ui::AccessibilityTypes::ROLE_PAGETABLIST;
}

views::View* TabStrip::GetEventHandlerForPoint(const gfx::Point& point) {
  // Return any view that isn't a Tab or this TabStrip immediately. We don't
  // want to interfere.
  views::View* v = View::GetEventHandlerForPoint(point);
  if (v && v != this && v->GetClassName() != Tab::kViewClassName)
    return v;

  // The display order doesn't necessarily match the child list order, so we
  // walk the display list hit-testing Tabs. Since the active tab always
  // renders on top of adjacent tabs, it needs to be hit-tested before any
  // left-adjacent Tab, so we look ahead for it as we walk.
  for (int i = 0; i < tab_count(); ++i) {
    Tab* next_tab = i < (tab_count() - 1) ? GetTabAtTabDataIndex(i + 1) : NULL;
    if (next_tab && next_tab->IsActive() && IsPointInTab(next_tab, point))
      return next_tab;
    Tab* tab = GetTabAtTabDataIndex(i);
    if (IsPointInTab(tab, point))
      return tab;
  }

  // No need to do any floating view stuff, we don't use them in the TabStrip.
  return this;
}

int TabStrip::TabIndexOfTab(BaseTab* tab) const {
  for (int i = 0; i < tab_count(); ++i) {
    if (base_tab_at_tab_index(i) == tab)
      return i;
  }
  return -1;
}

Tab* TabStrip::GetTabAtTabDataIndex(int tab_data_index) const {
  return static_cast<Tab*>(base_tab_at_tab_index(tab_data_index));
}

Tab* TabStrip::GetTabAtModelIndex(int model_index) const {
  return GetTabAtTabDataIndex(ModelIndexToTabIndex(model_index));
}

int TabStrip::GetMiniTabCount() const {
  int mini_count = 0;
  for (int i = 0; i < tab_count(); ++i) {
    if (base_tab_at_tab_index(i)->data().mini)
      mini_count++;
    else
      return mini_count;
  }
  return mini_count;
}

///////////////////////////////////////////////////////////////////////////////
// TabStrip, views::BaseButton::ButtonListener implementation:

void TabStrip::ButtonPressed(views::Button* sender, const views::Event& event) {
  if (sender == newtab_button_)
    controller()->CreateNewTab();
}

///////////////////////////////////////////////////////////////////////////////
// TabStrip, protected:

void TabStrip::ViewHierarchyChanged(bool is_add,
                                    views::View* parent,
                                    views::View* child) {
  if (is_add && child == this)
    InitTabStripButtons();
}

// Overridden to support automation. See automation_proxy_uitest.cc.
const views::View* TabStrip::GetViewByID(int view_id) const {
  if (tab_count() > 0) {
    if (view_id == VIEW_ID_TAB_LAST) {
      return base_tab_at_tab_index(tab_count() - 1);
    } else if ((view_id >= VIEW_ID_TAB_0) && (view_id < VIEW_ID_TAB_LAST)) {
      int index = view_id - VIEW_ID_TAB_0;
      if (index >= 0 && index < tab_count()) {
        return base_tab_at_tab_index(index);
      } else {
        return NULL;
      }
    }
  }

  return View::GetViewByID(view_id);
}

bool TabStrip::OnMouseDragged(const views::MouseEvent&  event) {
  if (drag_controller_.get())
    drag_controller_->Drag();
  return true;
}

void TabStrip::OnMouseReleased(const views::MouseEvent& event) {
  EndDrag(false);
}

void TabStrip::OnMouseCaptureLost() {
  EndDrag(true);
}

void TabStrip::GetCurrentTabWidths(double* unselected_width,
                                   double* selected_width) const {
  *unselected_width = current_unselected_width_;
  *selected_width = current_selected_width_;
}

///////////////////////////////////////////////////////////////////////////////
// TabStrip, private:

void TabStrip::Init() {
  set_id(VIEW_ID_TAB_STRIP);
  newtab_button_bounds_.SetRect(0, 0, kNewTabButtonWidth, kNewTabButtonHeight);
  if (drop_indicator_width == 0) {
    // Direction doesn't matter, both images are the same size.
    SkBitmap* drop_image = GetDropArrowImage(true);
    drop_indicator_width = drop_image->width();
    drop_indicator_height = drop_image->height();
  }
}

void TabStrip::InitTabStripButtons() {
  newtab_button_ = new NewTabButton(this, this);
  newtab_button_->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_ACCNAME_NEWTAB));
  AddChildView(newtab_button_);
}

BaseTab* TabStrip::CreateTab() {
  Tab* tab = new Tab(this);
  tab->set_animation_container(animation_container_.get());
  return tab;
}

void TabStrip::StartInsertTabAnimation(int model_index) {
  PrepareForAnimation();

  // The TabStrip can now use its entire width to lay out Tabs.
  in_tab_close_ = false;
  available_width_for_tabs_ = -1;

  GenerateIdealBounds();

  int tab_data_index = ModelIndexToTabIndex(model_index);
  BaseTab* tab = base_tab_at_tab_index(tab_data_index);
  if (model_index == 0) {
    tab->SetBounds(0, ideal_bounds(tab_data_index).y(), 0,
                   ideal_bounds(tab_data_index).height());
  } else {
    BaseTab* last_tab = base_tab_at_tab_index(tab_data_index - 1);
    tab->SetBounds(last_tab->bounds().right() + kTabHOffset,
                   ideal_bounds(tab_data_index).y(), 0,
                   ideal_bounds(tab_data_index).height());
  }

  AnimateToIdealBounds();
}

void TabStrip::StartMoveTabAnimation() {
  PrepareForAnimation();
  GenerateIdealBounds();
  AnimateToIdealBounds();
}

void TabStrip::StartRemoveTabAnimation(int model_index) {
  PrepareForAnimation();

  // Mark the tab as closing.
  BaseTab* tab = GetBaseTabAtModelIndex(model_index);
  tab->set_closing(true);

  // Start an animation for the tabs.
  GenerateIdealBounds();
  AnimateToIdealBounds();

  // Animate the tab being closed to 0x0.
  gfx::Rect tab_bounds = tab->bounds();
  tab_bounds.set_width(0);
  bounds_animator_.AnimateViewTo(tab, tab_bounds);

  // Register delegate to do cleanup when done, BoundsAnimator takes
  // ownership of RemoveTabDelegate.
  bounds_animator_.SetAnimationDelegate(tab, new RemoveTabDelegate(this, tab),
                                        true);
}

void TabStrip::StopAnimating(bool layout) {
  if (!IsAnimating())
    return;

  bounds_animator_.Cancel();

  if (layout)
    DoLayout();
}

void TabStrip::AnimateToIdealBounds() {
  for (int i = 0; i < tab_count(); ++i) {
    Tab* tab = GetTabAtTabDataIndex(i);
    if (!tab->closing() && !tab->dragging())
      bounds_animator_.AnimateViewTo(tab, ideal_bounds(i));
  }

  bounds_animator_.AnimateViewTo(newtab_button_, newtab_button_bounds_);
}

bool TabStrip::ShouldHighlightCloseButtonAfterRemove() {
  return in_tab_close_;
}

void TabStrip::DoLayout() {
  last_layout_size_ = size();

  StopAnimating(false);

  GenerateIdealBounds();

  for (int i = 0; i < tab_count(); ++i)
    tab_data_[i].tab->SetBoundsRect(tab_data_[i].ideal_bounds);

  SchedulePaint();

  // It is possible we don't have a new tab button yet.
  if (newtab_button_) {
    if (SizeTabButtonToTopOfTabStrip()) {
      newtab_button_bounds_.set_height(
          kNewTabButtonHeight + kNewTabButtonVOffset);
      newtab_button_->SetImageAlignment(views::ImageButton::ALIGN_LEFT,
                                        views::ImageButton::ALIGN_BOTTOM);
    } else {
      newtab_button_bounds_.set_height(kNewTabButtonHeight);
      newtab_button_->SetImageAlignment(views::ImageButton::ALIGN_LEFT,
                                        views::ImageButton::ALIGN_TOP);
    }
    newtab_button_->SetBoundsRect(newtab_button_bounds_);
  }
}

void TabStrip::LayoutDraggedTabsAt(const std::vector<BaseTab*>& tabs,
                                   BaseTab* active_tab,
                                   const gfx::Point& location,
                                   bool initial_drag) {
  std::vector<gfx::Rect> bounds;
  CalculateBoundsForDraggedTabs(tabs, &bounds);
  DCHECK_EQ(tabs.size(), bounds.size());
  int active_tab_model_index = GetModelIndexOfBaseTab(active_tab);
  int active_tab_index = static_cast<int>(
      std::find(tabs.begin(), tabs.end(), active_tab) - tabs.begin());
  for (size_t i = 0; i < tabs.size(); ++i) {
    BaseTab* tab = tabs[i];
    gfx::Rect new_bounds = bounds[i];
    new_bounds.Offset(location.x(), location.y());
    int consecutive_index =
        active_tab_model_index - (active_tab_index - static_cast<int>(i));
    // If this is the initial layout during a drag and the tabs aren't
    // consecutive animate the view into position. Do the same if the tab is
    // already animating (which means we previously caused it to animate).
    if ((initial_drag &&
         GetModelIndexOfBaseTab(tabs[i]) != consecutive_index) ||
        bounds_animator_.IsAnimating(tabs[i])) {
      bounds_animator_.SetTargetBounds(tabs[i], new_bounds);
    } else {
      tab->SetBoundsRect(new_bounds);
    }
  }
}

void TabStrip::CalculateBoundsForDraggedTabs(const std::vector<BaseTab*>& tabs,
                                             std::vector<gfx::Rect>* bounds) {
  int x = 0;
  for (size_t i = 0; i < tabs.size(); ++i) {
    BaseTab* tab = tabs[i];
    if (i > 0 && tab->data().mini != tabs[i - 1]->data().mini)
      x += kMiniToNonMiniGap;
    gfx::Rect new_bounds = tab->bounds();
    new_bounds.set_origin(gfx::Point(x, 0));
    bounds->push_back(new_bounds);
    x += tab->width() + kTabHOffset;
  }
}

int TabStrip::GetSizeNeededForTabs(const std::vector<BaseTab*>& tabs) {
  int width = 0;
  for (size_t i = 0; i < tabs.size(); ++i) {
    BaseTab* tab = tabs[i];
    width += tab->width();
    if (i > 0 && tab->data().mini != tabs[i - 1]->data().mini)
      width += kMiniToNonMiniGap;
  }
  if (tabs.size() > 0)
    width += kTabHOffset * static_cast<int>(tabs.size() - 1);
  return width;
}

void TabStrip::RemoveAndDeleteTab(BaseTab* tab) {
  int tab_data_index = TabIndexOfTab(tab);

  DCHECK(tab_data_index != -1);

  // Remove the Tab from the TabStrip's list...
  tab_data_.erase(tab_data_.begin() + tab_data_index);

  delete tab;
}

void TabStrip::StartedDraggingTabs(const std::vector<BaseTab*>& tabs) {
  PrepareForAnimation();

  // Reset dragging state of existing tabs.
  for (int i = 0; i < tab_count(); ++i)
    base_tab_at_tab_index(i)->set_dragging(false);

  for (size_t i = 0; i < tabs.size(); ++i) {
    tabs[i]->set_dragging(true);
    bounds_animator_.StopAnimatingView(tabs[i]);
  }

  // Move the dragged tabs to their ideal bounds.
  GenerateIdealBounds();

  // Sets the bounds of the dragged tabs.
  for (size_t i = 0; i < tabs.size(); ++i) {
    int tab_data_index = TabIndexOfTab(tabs[i]);
    DCHECK(tab_data_index != -1);
    tabs[i]->SetBoundsRect(ideal_bounds(tab_data_index));
  }
  SchedulePaint();
}

void TabStrip::StoppedDraggingTabs(const std::vector<BaseTab*>& tabs) {
  bool is_first_tab = true;
  for (size_t i = 0; i < tabs.size(); ++i)
    StoppedDraggingTab(tabs[i], &is_first_tab);
}

void TabStrip::StoppedDraggingTab(BaseTab* tab, bool* is_first_tab) {
  int tab_data_index = TabIndexOfTab(tab);
  if (tab_data_index == -1) {
    // The tab was removed before the drag completed. Don't do anything.
    return;
  }

  if (*is_first_tab) {
    *is_first_tab = false;
    PrepareForAnimation();

    // Animate the view back to its correct position.
    GenerateIdealBounds();
    AnimateToIdealBounds();
  }
  bounds_animator_.AnimateViewTo(tab, ideal_bounds(TabIndexOfTab(tab)));
  // Install a delegate to reset the dragging state when done. We have to leave
  // dragging true for the tab otherwise it'll draw beneath the new tab button.
  bounds_animator_.SetAnimationDelegate(
      tab, new ResetDraggingStateDelegate(tab), true);
}

void TabStrip::OwnDragController(TabDragController* controller) {
  drag_controller_.reset(controller);
}

void TabStrip::DestroyDragController() {
  drag_controller_.reset();
}

TabDragController* TabStrip::ReleaseDragController() {
  return drag_controller_.release();
}

void TabStrip::GetDesiredTabWidths(int tab_count,
                                   int mini_tab_count,
                                   double* unselected_width,
                                   double* selected_width) const {
  DCHECK(tab_count >= 0 && mini_tab_count >= 0 && mini_tab_count <= tab_count);
  const double min_unselected_width = Tab::GetMinimumUnselectedSize().width();
  const double min_selected_width = Tab::GetMinimumSelectedSize().width();

  *unselected_width = min_unselected_width;
  *selected_width = min_selected_width;

  if (tab_count == 0) {
    // Return immediately to avoid divide-by-zero below.
    return;
  }

  // Determine how much space we can actually allocate to tabs.
  int available_width;
  if (available_width_for_tabs_ < 0) {
    available_width = width();
    available_width -= (kNewTabButtonHOffset + newtab_button_bounds_.width());
  } else {
    // Interesting corner case: if |available_width_for_tabs_| > the result
    // of the calculation in the conditional arm above, the strip is in
    // overflow.  We can either use the specified width or the true available
    // width here; the first preserves the consistent "leave the last tab under
    // the user's mouse so they can close many tabs" behavior at the cost of
    // prolonging the glitchy appearance of the overflow state, while the second
    // gets us out of overflow as soon as possible but forces the user to move
    // their mouse for a few tabs' worth of closing.  We choose visual
    // imperfection over behavioral imperfection and select the first option.
    available_width = available_width_for_tabs_;
  }

  if (mini_tab_count > 0) {
    available_width -= mini_tab_count * (Tab::GetMiniWidth() + kTabHOffset);
    tab_count -= mini_tab_count;
    if (tab_count == 0) {
      *selected_width = *unselected_width = Tab::GetStandardSize().width();
      return;
    }
    // Account for gap between the last mini-tab and first non-mini-tab.
    available_width -= kMiniToNonMiniGap;
  }

  // Calculate the desired tab widths by dividing the available space into equal
  // portions.  Don't let tabs get larger than the "standard width" or smaller
  // than the minimum width for each type, respectively.
  const int total_offset = kTabHOffset * (tab_count - 1);
  const double desired_tab_width = std::min((static_cast<double>(
      available_width - total_offset) / static_cast<double>(tab_count)),
      static_cast<double>(Tab::GetStandardSize().width()));
  *unselected_width = std::max(desired_tab_width, min_unselected_width);
  *selected_width = std::max(desired_tab_width, min_selected_width);

  // When there are multiple tabs, we'll have one selected and some unselected
  // tabs.  If the desired width was between the minimum sizes of these types,
  // try to shrink the tabs with the smaller minimum.  For example, if we have
  // a strip of width 10 with 4 tabs, the desired width per tab will be 2.5.  If
  // selected tabs have a minimum width of 4 and unselected tabs have a minimum
  // width of 1, the above code would set *unselected_width = 2.5,
  // *selected_width = 4, which results in a total width of 11.5.  Instead, we
  // want to set *unselected_width = 2, *selected_width = 4, for a total width
  // of 10.
  if (tab_count > 1) {
    if ((min_unselected_width < min_selected_width) &&
        (desired_tab_width < min_selected_width)) {
      // Unselected width = (total width - selected width) / (num_tabs - 1)
      *unselected_width = std::max(static_cast<double>(
          available_width - total_offset - min_selected_width) /
          static_cast<double>(tab_count - 1), min_unselected_width);
    } else if ((min_unselected_width > min_selected_width) &&
               (desired_tab_width < min_unselected_width)) {
      // Selected width = (total width - (unselected width * (num_tabs - 1)))
      *selected_width = std::max(available_width - total_offset -
          (min_unselected_width * (tab_count - 1)), min_selected_width);
    }
  }
}

void TabStrip::ResizeLayoutTabs() {
  // We've been called back after the TabStrip has been emptied out (probably
  // just prior to the window being destroyed). We need to do nothing here or
  // else GetTabAt below will crash.
  if (tab_count() == 0)
    return;

  // It is critically important that this is unhooked here, otherwise we will
  // keep spying on messages forever.
  RemoveMessageLoopObserver();

  in_tab_close_ = false;
  available_width_for_tabs_ = -1;
  int mini_tab_count = GetMiniTabCount();
  if (mini_tab_count == tab_count()) {
    // Only mini-tabs, we know the tab widths won't have changed (all
    // mini-tabs have the same width), so there is nothing to do.
    return;
  }
  Tab* first_tab  = GetTabAtTabDataIndex(mini_tab_count);
  double unselected, selected;
  GetDesiredTabWidths(tab_count(), mini_tab_count, &unselected, &selected);
  // TODO: this is always selected, should it be 'selected : unselected'?
  int w = Round(first_tab->IsActive() ? selected : selected);

  // We only want to run the animation if we're not already at the desired
  // size.
  if (abs(first_tab->width() - w) > 1)
    StartResizeLayoutAnimation();
}

void TabStrip::SetTabBoundsForDrag(const std::vector<gfx::Rect>& tab_bounds) {
  StopAnimating(false);
  DCHECK_EQ(tab_count(), static_cast<int>(tab_bounds.size()));
  for (int i = 0; i < tab_count(); ++i)
    base_tab_at_tab_index(i)->SetBoundsRect(tab_bounds[i]);
}

void TabStrip::AddMessageLoopObserver() {
  if (!mouse_watcher_.get()) {
    mouse_watcher_.reset(
        new views::MouseWatcher(this, this,
                                gfx::Insets(0, 0, kTabStripAnimationVSlop, 0)));
  }
  mouse_watcher_->Start();
}

void TabStrip::RemoveMessageLoopObserver() {
  mouse_watcher_.reset(NULL);
}

gfx::Rect TabStrip::GetDropBounds(int drop_index,
                                  bool drop_before,
                                  bool* is_beneath) {
  DCHECK_NE(drop_index, -1);
  int center_x;
  if (drop_index < tab_count()) {
    Tab* tab = GetTabAtTabDataIndex(drop_index);
    if (drop_before)
      center_x = tab->x() - (kTabHOffset / 2);
    else
      center_x = tab->x() + (tab->width() / 2);
  } else {
    Tab* last_tab = GetTabAtTabDataIndex(drop_index - 1);
    center_x = last_tab->x() + last_tab->width() + (kTabHOffset / 2);
  }

  // Mirror the center point if necessary.
  center_x = GetMirroredXInView(center_x);

  // Determine the screen bounds.
  gfx::Point drop_loc(center_x - drop_indicator_width / 2,
                      -drop_indicator_height);
  ConvertPointToScreen(this, &drop_loc);
  gfx::Rect drop_bounds(drop_loc.x(), drop_loc.y(), drop_indicator_width,
                        drop_indicator_height);

  // If the rect doesn't fit on the monitor, push the arrow to the bottom.
#if defined(OS_WIN) && !defined(USE_AURA)
  gfx::Rect monitor_bounds = views::GetMonitorBoundsForRect(drop_bounds);
  *is_beneath = (monitor_bounds.IsEmpty() ||
                 !monitor_bounds.Contains(drop_bounds));
#else
  *is_beneath = false;
  NOTIMPLEMENTED();
#endif
  if (*is_beneath)
    drop_bounds.Offset(0, drop_bounds.height() + height());

  return drop_bounds;
}

void TabStrip::UpdateDropIndex(const DropTargetEvent& event) {
  // If the UI layout is right-to-left, we need to mirror the mouse
  // coordinates since we calculate the drop index based on the
  // original (and therefore non-mirrored) positions of the tabs.
  const int x = GetMirroredXInView(event.x());
  // We don't allow replacing the urls of mini-tabs.
  for (int i = GetMiniTabCount(); i < tab_count(); ++i) {
    Tab* tab = GetTabAtTabDataIndex(i);
    const int tab_max_x = tab->x() + tab->width();
    const int hot_width = tab->width() / kTabEdgeRatioInverse;
    if (x < tab_max_x) {
      if (x < tab->x() + hot_width)
        SetDropIndex(i, true);
      else if (x >= tab_max_x - hot_width)
        SetDropIndex(i + 1, true);
      else
        SetDropIndex(i, false);
      return;
    }
  }

  // The drop isn't over a tab, add it to the end.
  SetDropIndex(tab_count(), true);
}

void TabStrip::SetDropIndex(int tab_data_index, bool drop_before) {
  // Let the controller know of the index update.
  controller()->OnDropIndexUpdate(tab_data_index, drop_before);

  if (tab_data_index == -1) {
    if (drop_info_.get())
      drop_info_.reset(NULL);
    return;
  }

  if (drop_info_.get() && drop_info_->drop_index == tab_data_index &&
      drop_info_->drop_before == drop_before) {
    return;
  }

  bool is_beneath;
  gfx::Rect drop_bounds = GetDropBounds(tab_data_index, drop_before,
                                        &is_beneath);

  if (!drop_info_.get()) {
    drop_info_.reset(new DropInfo(tab_data_index, drop_before, !is_beneath));
  } else {
    drop_info_->drop_index = tab_data_index;
    drop_info_->drop_before = drop_before;
    if (is_beneath == drop_info_->point_down) {
      drop_info_->point_down = !is_beneath;
      drop_info_->arrow_view->SetImage(
          GetDropArrowImage(drop_info_->point_down));
    }
  }

  // Reposition the window. Need to show it too as the window is initially
  // hidden.
  drop_info_->arrow_window->SetBounds(drop_bounds);
  drop_info_->arrow_window->Show();
}

int TabStrip::GetDropEffect(const views::DropTargetEvent& event) {
  const int source_ops = event.source_operations();
  if (source_ops & ui::DragDropTypes::DRAG_COPY)
    return ui::DragDropTypes::DRAG_COPY;
  if (source_ops & ui::DragDropTypes::DRAG_LINK)
    return ui::DragDropTypes::DRAG_LINK;
  return ui::DragDropTypes::DRAG_MOVE;
}

// static
SkBitmap* TabStrip::GetDropArrowImage(bool is_down) {
  return ResourceBundle::GetSharedInstance().GetBitmapNamed(
      is_down ? IDR_TAB_DROP_DOWN : IDR_TAB_DROP_UP);
}

// TabStrip::DropInfo ----------------------------------------------------------

TabStrip::DropInfo::DropInfo(int drop_index, bool drop_before, bool point_down)
    : drop_index(drop_index),
      drop_before(drop_before),
      point_down(point_down) {
  arrow_view = new views::ImageView;
  arrow_view->SetImage(GetDropArrowImage(point_down));

  arrow_window = new views::Widget;
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_POPUP);
  params.keep_on_top = true;
  params.transparent = true;
  params.accept_events = false;
  params.can_activate = false;
  params.bounds = gfx::Rect(drop_indicator_width, drop_indicator_height);
  arrow_window->Init(params);
  arrow_window->SetContentsView(arrow_view);
}

TabStrip::DropInfo::~DropInfo() {
  // Close eventually deletes the window, which deletes arrow_view too.
  arrow_window->Close();
}

///////////////////////////////////////////////////////////////////////////////

bool TabStrip::IsAnimating() const {
  return bounds_animator_.IsAnimating();
}

void TabStrip::PrepareForAnimation() {
  if (!IsDragSessionActive() && !TabDragController::IsAttachedTo(this)) {
    for (int i = 0; i < tab_count(); ++i)
      base_tab_at_tab_index(i)->set_dragging(false);
  }
}

// Called from:
// - BasicLayout
// - Tab insertion/removal
// - Tab reorder
void TabStrip::GenerateIdealBounds() {
  int non_closing_tab_count = 0;
  int mini_tab_count = 0;
  for (int i = 0; i < tab_count(); ++i) {
    BaseTab* tab = base_tab_at_tab_index(i);
    if (!tab->closing()) {
      ++non_closing_tab_count;
      if (tab->data().mini)
        mini_tab_count++;
    }
  }

  double unselected, selected;
  GetDesiredTabWidths(non_closing_tab_count, mini_tab_count, &unselected,
                      &selected);

  current_unselected_width_ = unselected;
  current_selected_width_ = selected;

  // NOTE: This currently assumes a tab's height doesn't differ based on
  // selected state or the number of tabs in the strip!
  int tab_height = Tab::GetStandardSize().height();
  double tab_x = 0;
  bool last_was_mini = false;
  for (int i = 0; i < tab_count(); ++i) {
    Tab* tab = GetTabAtTabDataIndex(i);
    if (!tab->closing()) {
      double tab_width = unselected;
      if (tab->data().mini) {
        tab_width = Tab::GetMiniWidth();
      } else {
        if (last_was_mini) {
          // Give a bigger gap between mini and non-mini tabs.
          tab_x += kMiniToNonMiniGap;
        }
        if (tab->IsActive())
          tab_width = selected;
      }
      double end_of_tab = tab_x + tab_width;
      int rounded_tab_x = Round(tab_x);
      set_ideal_bounds(i,
          gfx::Rect(rounded_tab_x, 0, Round(end_of_tab) - rounded_tab_x,
                    tab_height));
      tab_x = end_of_tab + kTabHOffset;
      last_was_mini = tab->data().mini;
    }
  }

  // Update bounds of new tab button.
  int new_tab_x;
  int new_tab_y = SizeTabButtonToTopOfTabStrip() ? 0 : kNewTabButtonVOffset;
  if (abs(Round(unselected) - Tab::GetStandardSize().width()) > 1 &&
      !in_tab_close_) {
    // We're shrinking tabs, so we need to anchor the New Tab button to the
    // right edge of the TabStrip's bounds, rather than the right edge of the
    // right-most Tab, otherwise it'll bounce when animating.
    new_tab_x = width() - newtab_button_bounds_.width();
  } else {
    new_tab_x = Round(tab_x - kTabHOffset) + kNewTabButtonHOffset;
  }
  newtab_button_bounds_.set_origin(gfx::Point(new_tab_x, new_tab_y));
}

void TabStrip::StartResizeLayoutAnimation() {
  PrepareForAnimation();
  GenerateIdealBounds();
  AnimateToIdealBounds();
}

void TabStrip::StartMiniTabAnimation() {
  in_tab_close_ = false;
  available_width_for_tabs_ = -1;

  PrepareForAnimation();

  GenerateIdealBounds();
  AnimateToIdealBounds();
}

void TabStrip::StartMouseInitiatedRemoveTabAnimation(int model_index) {
  // The user initiated the close. We want to persist the bounds of all the
  // existing tabs, so we manually shift ideal_bounds then animate.
  int tab_data_index = ModelIndexToTabIndex(model_index);
  DCHECK(tab_data_index != tab_count());
  BaseTab* tab_closing = base_tab_at_tab_index(tab_data_index);
  int delta = tab_closing->width() + kTabHOffset;
  // If the tab being closed is a mini-tab next to a non-mini-tab, be sure to
  // add the extra padding.
  int next_tab_data_index = ModelIndexToTabIndex(model_index + 1);
  DCHECK_NE(next_tab_data_index, tab_count());
  if (tab_closing->data().mini && next_tab_data_index < tab_count() &&
      !base_tab_at_tab_index(next_tab_data_index)->data().mini) {
    delta += kMiniToNonMiniGap;
  }

  for (int i = tab_data_index + 1; i < tab_count(); ++i) {
    BaseTab* tab = base_tab_at_tab_index(i);
    if (!tab->closing()) {
      gfx::Rect bounds = ideal_bounds(i);
      bounds.set_x(bounds.x() - delta);
      set_ideal_bounds(i, bounds);
    }
  }

  newtab_button_bounds_.set_x(newtab_button_bounds_.x() - delta);

  PrepareForAnimation();

  // Mark the tab as closing.
  tab_closing->set_closing(true);

  AnimateToIdealBounds();

  gfx::Rect tab_bounds = tab_closing->bounds();
  tab_bounds.set_width(0);
  bounds_animator_.AnimateViewTo(tab_closing, tab_bounds);

  // Register delegate to do cleanup when done, BoundsAnimator takes
  // ownership of RemoveTabDelegate.
  bounds_animator_.SetAnimationDelegate(tab_closing,
                                        CreateRemoveTabDelegate(tab_closing),
                                        true);
}

ui::AnimationDelegate* TabStrip::CreateRemoveTabDelegate(BaseTab* tab) {
  return new RemoveTabDelegate(this, tab);
}

bool TabStrip::IsPointInTab(Tab* tab,
                            const gfx::Point& point_in_tabstrip_coords) {
  gfx::Point point_in_tab_coords(point_in_tabstrip_coords);
  View::ConvertPointToView(this, tab, &point_in_tab_coords);
  return tab->HitTest(point_in_tab_coords);
}
