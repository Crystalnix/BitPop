// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/launcher/launcher_view.h"

#include "ash/launcher/app_launcher_button.h"
#include "ash/launcher/launcher_model.h"
#include "ash/launcher/launcher_window_cycler.h"
#include "ash/launcher/tabbed_launcher_button.h"
#include "ash/launcher/view_model.h"
#include "ash/launcher/view_model_utils.h"
#include "ash/shell.h"
#include "ash/shell_delegate.h"
#include "base/utf_string_conversions.h"
#include "grit/ui_resources.h"
#include "ui/aura/window.h"
#include "ui/base/animation/animation.h"
#include "ui/base/animation/throb_animation.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/compositor/layer.h"
#include "ui/gfx/image/image.h"
#include "ui/views/animation/bounds_animator.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/menu/menu_model_adapter.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

using ui::Animation;
using views::View;

namespace ash {
namespace internal {

// Amount content is inset on the left edge.
static const int kLeadingInset = 8;

// Height of the LauncherView. Hard coded to avoid resizing as items are
// added/removed.
static const int kPreferredHeight = 48;

// Minimum distance before drag starts.
static const int kMinimumDragDistance = 8;

// Size given to the buttons on the launcher.
static const int kButtonWidth = 60;
static const int kButtonHeight = 48;

namespace {

// ui::SimpleMenuModel::Delegate implementation that remembers the id of the
// menu that was activated.
class MenuDelegateImpl : public ui::SimpleMenuModel::Delegate {
 public:
  MenuDelegateImpl() : activated_command_id_(-1) {}

  int activated_command_id() const { return activated_command_id_; }

  // ui::SimpleMenuModel::Delegate overrides:
  virtual bool IsCommandIdChecked(int command_id) const OVERRIDE {
    return false;
  }
  virtual bool IsCommandIdEnabled(int command_id) const OVERRIDE {
    return true;
  }
  virtual bool GetAcceleratorForCommandId(
      int command_id,
      ui::Accelerator* accelerator) OVERRIDE {
    return false;
  }
  virtual void ExecuteCommand(int command_id) OVERRIDE {
    activated_command_id_ = command_id;
  }

 private:
  // ID of the command passed to ExecuteCommand.
  int activated_command_id_;

  DISALLOW_COPY_AND_ASSIGN(MenuDelegateImpl);
};

// AnimationDelegate that deletes a view when done. This is used when a launcher
// item is removed, which triggers a remove animation. When the animation is
// done we delete the view.
class DeleteViewAnimationDelegate :
      public views::BoundsAnimator::OwnedAnimationDelegate {
 public:
  explicit DeleteViewAnimationDelegate(views::View* view) : view_(view) {}
  virtual ~DeleteViewAnimationDelegate() {}

 private:
  scoped_ptr<views::View> view_;

  DISALLOW_COPY_AND_ASSIGN(DeleteViewAnimationDelegate);
};

// AnimationDelegate used when inserting a new item. This steadily increases the
// opacity of the layer as the animation progress.
class FadeInAnimationDelegate :
      public views::BoundsAnimator::OwnedAnimationDelegate {
 public:
  explicit FadeInAnimationDelegate(views::View* view) : view_(view) {}
  virtual ~FadeInAnimationDelegate() {}

  // AnimationDelegate overrides:
  virtual void AnimationProgressed(const Animation* animation) OVERRIDE {
    view_->layer()->SetOpacity(animation->GetCurrentValue());
    view_->layer()->ScheduleDraw();
  }
  virtual void AnimationEnded(const Animation* animation) OVERRIDE {
    view_->layer()->SetOpacity(1.0f);
    view_->layer()->ScheduleDraw();
  }
  virtual void AnimationCanceled(const Animation* animation) OVERRIDE {
    view_->layer()->SetOpacity(1.0f);
    view_->layer()->ScheduleDraw();
  }

 private:
  views::View* view_;

  DISALLOW_COPY_AND_ASSIGN(FadeInAnimationDelegate);
};

}  // namespace

// AnimationDelegate used when inserting a new item. This steadily decreased the
// opacity of the layer as the animation progress.
class LauncherView::FadeOutAnimationDelegate :
      public views::BoundsAnimator::OwnedAnimationDelegate {
 public:
  FadeOutAnimationDelegate(LauncherView* host, views::View* view)
      : launcher_view_(host),
        view_(view) {}
  virtual ~FadeOutAnimationDelegate() {}

  // AnimationDelegate overrides:
  virtual void AnimationProgressed(const Animation* animation) OVERRIDE {
    view_->layer()->SetOpacity(1 - animation->GetCurrentValue());
    view_->layer()->ScheduleDraw();
  }
  virtual void AnimationEnded(const Animation* animation) OVERRIDE {
    launcher_view_->AnimateToIdealBounds();
  }
  virtual void AnimationCanceled(const Animation* animation) OVERRIDE {
  }

 private:
  LauncherView* launcher_view_;
  scoped_ptr<views::View> view_;

  DISALLOW_COPY_AND_ASSIGN(FadeOutAnimationDelegate);
};

// AnimationDelegate used to trigger fading an element in. When an item is
// inserted this delegate is attached to the animation that expands the size of
// the item.  When done it kicks off another animation to fade the item in.
class LauncherView::StartFadeAnimationDelegate :
      public views::BoundsAnimator::OwnedAnimationDelegate {
 public:
  StartFadeAnimationDelegate(LauncherView* host,
                             views::View* view)
      : launcher_view_(host),
        view_(view) {}
  virtual ~StartFadeAnimationDelegate() {}

  // AnimationDelegate overrides:
  virtual void AnimationEnded(const Animation* animation) OVERRIDE {
    view_->SetVisible(true);
    launcher_view_->FadeIn(view_);
  }
  virtual void AnimationCanceled(const Animation* animation) OVERRIDE {
    view_->SetVisible(true);
  }

 private:
  LauncherView* launcher_view_;
  views::View* view_;

  DISALLOW_COPY_AND_ASSIGN(StartFadeAnimationDelegate);
};

LauncherView::LauncherView(LauncherModel* model)
    : model_(model),
      view_model_(new ViewModel),
      overflow_button_(NULL),
      dragging_(NULL),
      drag_view_(NULL),
      drag_offset_(0),
      start_drag_index_(-1) {
  DCHECK(model_);
  bounds_animator_.reset(new views::BoundsAnimator(this));
}

LauncherView::~LauncherView() {
  model_->RemoveObserver(this);
}

void LauncherView::Init() {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  model_->AddObserver(this);

  const LauncherItems& items(model_->items());
  for (LauncherItems::const_iterator i = items.begin(); i != items.end(); ++i) {
    views::View* child = CreateViewForItem(*i);
    child->SetPaintToLayer(true);
    view_model_->Add(child, static_cast<int>(i - items.begin()));
    AddChildView(child);
  }

  overflow_button_ = new views::ImageButton(this);
  overflow_button_->set_focusable(true);
  overflow_button_->SetImage(
      views::CustomButton::BS_NORMAL,
      rb.GetImageNamed(IDR_AURA_LAUNCHER_OVERFLOW).ToSkBitmap());
  overflow_button_->SetImage(
      views::CustomButton::BS_HOT,
      rb.GetImageNamed(IDR_AURA_LAUNCHER_OVERFLOW_HOT).ToSkBitmap());
  overflow_button_->SetImage(
      views::CustomButton::BS_PUSHED,
      rb.GetImageNamed(IDR_AURA_LAUNCHER_OVERFLOW_PUSHED).ToSkBitmap());
  ConfigureChildView(overflow_button_);
  AddChildView(overflow_button_);

  // We'll layout when our bounds change.
}

void LauncherView::LayoutToIdealBounds() {
  IdealBounds ideal_bounds;
  CalculateIdealBounds(&ideal_bounds);
  ViewModelUtils::SetViewBoundsToIdealBounds(*view_model_);
  overflow_button_->SetBoundsRect(ideal_bounds.overflow_bounds);
}

void LauncherView::CalculateIdealBounds(IdealBounds* bounds) {
  int available_width = width();
  if (!available_width)
    return;

  int x = kLeadingInset;
  for (int i = 0; i < view_model_->view_size(); ++i) {
    gfx::Size pref(kButtonWidth, kButtonHeight);
    view_model_->set_ideal_bounds(i, gfx::Rect(
        x, (kPreferredHeight - pref.height()) / 2, pref.width(),
        pref.height()));
    x += pref.width();
  }

  bounds->overflow_bounds.set_size(gfx::Size(kButtonWidth, kButtonHeight));
  int last_visible_index = DetermineLastVisibleIndex(
      available_width - kLeadingInset - bounds->overflow_bounds.width());
  bool show_overflow =
      (last_visible_index + 1 != view_model_->view_size());
  if (overflow_button_->visible() != show_overflow) {
    // Only change visibility of the views if the visibility of the overflow
    // button changes. Otherwise we'll effect the insertion animation, which
    // changes the visibility.
    for (int i = 0; i <= last_visible_index; ++i)
      view_model_->view_at(i)->SetVisible(true);
    for (int i = last_visible_index + 1; i < view_model_->view_size(); ++i)
      view_model_->view_at(i)->SetVisible(false);
  }
  overflow_button_->SetVisible(show_overflow);
  if (show_overflow) {
    DCHECK_NE(0, view_model_->view_size());
    x = last_visible_index == -1 ?
        kLeadingInset : view_model_->ideal_bounds(last_visible_index).right();
    bounds->overflow_bounds.set_x(x);
    bounds->overflow_bounds.set_y(
        (kPreferredHeight - bounds->overflow_bounds.height()) / 2);
    x = bounds->overflow_bounds.right();
  }
}

int LauncherView::DetermineLastVisibleIndex(int max_x) {
  int index = view_model_->view_size() - 1;
  while (index >= 0 && view_model_->ideal_bounds(index).right() > max_x)
    index--;
  return index;
}

void LauncherView::AnimateToIdealBounds() {
  IdealBounds ideal_bounds;
  CalculateIdealBounds(&ideal_bounds);
  for (int i = 0; i < view_model_->view_size(); ++i) {
    bounds_animator_->AnimateViewTo(view_model_->view_at(i),
                                    view_model_->ideal_bounds(i));
  }
  overflow_button_->SetBoundsRect(ideal_bounds.overflow_bounds);
}

views::View* LauncherView::CreateViewForItem(const LauncherItem& item) {
  views::View* view = NULL;
  switch (item.type) {
    case TYPE_TABBED: {
      TabbedLauncherButton* button = new TabbedLauncherButton(this, this);
      button->SetTabImage(item.image, item.num_tabs);
      view = button;
      break;
    }

    case TYPE_APP: {
      AppLauncherButton* button = new AppLauncherButton(this, this);
      button->SetAppImage(item.image);
      view = button;
      break;
    }

    case TYPE_APP_LIST: {
      ResourceBundle& rb = ResourceBundle::GetSharedInstance();
      views::ImageButton* button = new AppLauncherButton(this, this);
      button->SetImage(
          views::CustomButton::BS_NORMAL,
          rb.GetImageNamed(IDR_AURA_LAUNCHER_ICON_APPLIST).ToSkBitmap());
      button->SetImage(
          views::CustomButton::BS_HOT,
          rb.GetImageNamed(IDR_AURA_LAUNCHER_ICON_APPLIST_HOT).ToSkBitmap());
      button->SetImage(
          views::CustomButton::BS_PUSHED,
          rb.GetImageNamed(IDR_AURA_LAUNCHER_ICON_APPLIST_PUSHED).ToSkBitmap());
      view = button;
      break;
    }

    case TYPE_BROWSER_SHORTCUT: {
      ResourceBundle& rb = ResourceBundle::GetSharedInstance();
      views::ImageButton* button = new AppLauncherButton(this, this);
      ShellDelegate* delegate = Shell::GetInstance()->delegate();
      int image_id = delegate ?
          delegate->GetBrowserShortcutResourceId() :
          IDR_AURA_LAUNCHER_BROWSER_SHORTCUT;
      button->SetImage(views::CustomButton::BS_NORMAL,
                       rb.GetImageNamed(image_id).ToSkBitmap());
      view = button;
      cycler_.reset(new LauncherWindowCycler);
      break;
    }

    default:
      break;
  }

  DCHECK(view);
  ConfigureChildView(view);
  return view;
}

void LauncherView::FadeIn(views::View* view) {
  view->SetVisible(true);
  view->layer()->SetOpacity(0);
  AnimateToIdealBounds();
  bounds_animator_->SetAnimationDelegate(
      view, new FadeInAnimationDelegate(view), true);
}

void LauncherView::PrepareForDrag(const views::MouseEvent& event) {
  DCHECK(drag_view_);
  dragging_ = true;
  start_drag_index_ = view_model_->GetIndexOfView(drag_view_);
  // Move the view to the front so that it appears on top of other views.
  ReorderChildView(drag_view_, -1);
  bounds_animator_->StopAnimatingView(drag_view_);
}

void LauncherView::ContinueDrag(const views::MouseEvent& event) {
  // TODO: I don't think this works correctly with RTL.
  gfx::Point drag_point(event.x(), 0);
  views::View::ConvertPointToView(drag_view_, this, &drag_point);
  int current_index = view_model_->GetIndexOfView(drag_view_);
  DCHECK_NE(-1, current_index);

  // Constrain the x location so that it doesn't overlap the two buttons.
  int x = std::max(view_model_->ideal_bounds(0).x(),
                   drag_point.x() - drag_offset_);
  x = std::min(view_model_->ideal_bounds(view_model_->view_size() - 1).right() -
               view_model_->ideal_bounds(current_index).width(),
               x);
  if (drag_view_->x() == x)
    return;

  drag_view_->SetX(x);
  int target_index =
      ViewModelUtils::DetermineMoveIndex(*view_model_, drag_view_, x);
  if (target_index == current_index)
    return;

  // Remove the observer while we mutate the model so that we don't attempt to
  // cancel the drag.
  model_->RemoveObserver(this);
  model_->Move(current_index, target_index);
  model_->AddObserver(this);
  view_model_->Move(current_index, target_index);
  AnimateToIdealBounds();
  bounds_animator_->StopAnimatingView(drag_view_);
}

void LauncherView::ConfigureChildView(views::View* view) {
  view->SetPaintToLayer(true);
  view->layer()->SetFillsBoundsOpaquely(false);
}

void LauncherView::GetOverflowItems(std::vector<LauncherItem>* items) {
  int index = 0;
  while (index < view_model_->view_size() &&
         view_model_->view_at(index)->visible()) {
    index++;
  }
  while (index < view_model_->view_size()) {
    const LauncherItem& item = model_->items()[index];
    if (item.type == TYPE_TABBED || item.type == TYPE_APP)
      items->push_back(item);
    index++;
  }
}

void LauncherView::ShowOverflowMenu() {
#if !defined(OS_MACOSX)
  ShellDelegate* delegate = Shell::GetInstance()->delegate();
  if (!delegate)
    return;

  std::vector<LauncherItem> items;
  GetOverflowItems(&items);
  if (items.empty())
    return;

  MenuDelegateImpl menu_delegate;
  ui::SimpleMenuModel menu_model(&menu_delegate);
  for (size_t i = 0; i < items.size(); ++i) {
    menu_model.AddItem(static_cast<int>(i),
                       delegate->GetLauncherItemTitle(items[i]));
  }
  views::MenuModelAdapter menu_adapter(&menu_model);
  overflow_menu_runner_.reset(new views::MenuRunner(menu_adapter.CreateMenu()));
  gfx::Rect bounds(overflow_button_->size());
  gfx::Point origin;
  ConvertPointToScreen(overflow_button_, &origin);
  if (overflow_menu_runner_->RunMenuAt(GetWidget(), NULL,
          gfx::Rect(origin, size()), views::MenuItemView::TOPLEFT, 0) ==
      views::MenuRunner::MENU_DELETED ||
      menu_delegate.activated_command_id() == -1)
    return;

  LauncherID activated_id = items[menu_delegate.activated_command_id()].id;
  LauncherItems::const_iterator window_iter = model_->ItemByID(activated_id);
  if (window_iter == model_->items().end())
    return;  // Window was deleted while menu was up.
  if (!delegate)
    return;
  delegate->LauncherItemClicked(*window_iter);
#endif  // !defined(OS_MACOSX)
}

void LauncherView::CancelDrag(views::View* deleted_view) {
  if (!drag_view_)
    return;
  bool was_dragging = dragging_;
  views::View* drag_view = drag_view_;
  dragging_ = false;
  drag_view_ = NULL;
  if (drag_view == deleted_view) {
    // The view that was being dragged is being deleted. Don't do anything.
    return;
  }
  if (!was_dragging)
    return;

  view_model_->Move(view_model_->GetIndexOfView(drag_view), start_drag_index_);
  AnimateToIdealBounds();
}

void LauncherView::MaybeResetWindowCycler(views::View* view) {
  int view_index = view_model_->GetIndexOfView(view);
  if (view_index != -1 &&
      model_->items()[view_index].type == TYPE_BROWSER_SHORTCUT) {
    cycler_->Reset();
  }
}

gfx::Size LauncherView::GetPreferredSize() {
  IdealBounds ideal_bounds;
  CalculateIdealBounds(&ideal_bounds);
  if (view_model_->view_size() >= 2) {
    // Should always have two items.
    return gfx::Size(view_model_->ideal_bounds(1).right() + kLeadingInset,
                     kPreferredHeight);
  }
  return gfx::Size(kButtonWidth * 2 + kLeadingInset * 2, kPreferredHeight);
}

void LauncherView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  LayoutToIdealBounds();
}

void LauncherView::LauncherItemAdded(int model_index) {
  CancelDrag(NULL);

  views::View* view = CreateViewForItem(model_->items()[model_index]);
  AddChildView(view);
  // Hide the view, it'll be made visible when the animation is done.
  view->SetVisible(false);
  view_model_->Add(view, model_index);

  // The first animation moves all the views to their target position. |view| is
  // hidden, so it visually appears as though we are providing space for
  // it. When done we'll fade the view in.
  AnimateToIdealBounds();
  if (!overflow_button_->visible()) {
    bounds_animator_->SetAnimationDelegate(
        view, new StartFadeAnimationDelegate(this, view), true);
  }
}

void LauncherView::LauncherItemRemoved(int model_index) {
  views::View* view = view_model_->view_at(model_index);
  CancelDrag(view);
  view_model_->Remove(model_index);
  // The first animation fades out the view. When done we'll animate the rest of
  // the views to their target location.
  bounds_animator_->AnimateViewTo(view, view->bounds());
  bounds_animator_->SetAnimationDelegate(
      view, new FadeOutAnimationDelegate(this, view), true);
}

void LauncherView::LauncherItemChanged(int model_index,
                                       const ash::LauncherItem& old_item) {
  const LauncherItem& item(model_->items()[model_index]);
  if (old_item.type != item.type) {
    // Type changed, swap the views.
    scoped_ptr<views::View> old_view(view_model_->view_at(model_index));
    bounds_animator_->StopAnimatingView(old_view.get());
    CancelDrag(old_view.get());
    view_model_->Remove(model_index);
    views::View* new_view = CreateViewForItem(item);
    AddChildView(new_view);
    view_model_->Add(new_view, model_index);
    new_view->SetBoundsRect(old_view->bounds());
    return;
  }

  views::View* view = view_model_->view_at(model_index);
  switch (item.type) {
    case TYPE_TABBED: {
      TabbedLauncherButton* button = static_cast<TabbedLauncherButton*>(view);
      gfx::Size pref = button->GetPreferredSize();
      button->SetTabImage(item.image, item.num_tabs);
      if (pref != button->GetPreferredSize())
        AnimateToIdealBounds();
      else
        button->SchedulePaint();
      break;
    }

    case TYPE_APP: {
      AppLauncherButton* button = static_cast<AppLauncherButton*>(view);
      button->SetAppImage(item.image);
      button->SchedulePaint();
      break;
    }

    default:
      break;
  }
}

void LauncherView::LauncherItemMoved(int start_index, int target_index) {
  view_model_->Move(start_index, target_index);
  AnimateToIdealBounds();
}

void LauncherView::LauncherItemWillChange(int index) {
  const LauncherItem& item(model_->items()[index]);
  views::View* view = view_model_->view_at(index);
  if (item.type == TYPE_TABBED)
    static_cast<TabbedLauncherButton*>(view)->PrepareForImageChange();
}

void LauncherView::MousePressedOnButton(views::View* view,
                                        const views::MouseEvent& event) {
  if (view_model_->GetIndexOfView(view) == -1 || view_model_->view_size() <= 1)
    return;  // View is being deleted, ignore request.

  drag_view_ = view;
  drag_offset_ = event.x();
}

void LauncherView::MouseDraggedOnButton(views::View* view,
                                        const views::MouseEvent& event) {
  if (!dragging_ && drag_view_ &&
      abs(event.x() - drag_offset_) >= kMinimumDragDistance)
    PrepareForDrag(event);
  if (dragging_)
    ContinueDrag(event);
  if (!view->GetLocalBounds().Contains(event.location()))
    MaybeResetWindowCycler(view);
}

void LauncherView::MouseReleasedOnButton(views::View* view,
                                         bool canceled) {
  if (canceled) {
    CancelDrag(NULL);
  } else {
    dragging_ = false;
    drag_view_ = NULL;
    AnimateToIdealBounds();
  }
}

void LauncherView::MouseExitedButton(views::View* view) {
  MaybeResetWindowCycler(view);
}

void LauncherView::ButtonPressed(views::Button* sender,
                                 const views::Event& event) {
  if (sender == overflow_button_)
    ShowOverflowMenu();

  ShellDelegate* delegate = Shell::GetInstance()->delegate();
  if (!delegate)
    return;
  int view_index = view_model_->GetIndexOfView(sender);
  // May be -1 while in the process of animating closed.
  if (view_index == -1)
    return;

  switch (model_->items()[view_index].type) {
    case TYPE_TABBED:
    case TYPE_APP:
      delegate->LauncherItemClicked(model_->items()[view_index]);
      break;

    case TYPE_APP_LIST:
      Shell::GetInstance()->ToggleAppList();
      break;

    case TYPE_BROWSER_SHORTCUT:
      cycler_->Cycle();
      break;

    default:
      NOTREACHED();
  }
}

}  // namespace internal
}  // namespace ash
