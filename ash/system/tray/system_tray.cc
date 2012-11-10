// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/tray/system_tray.h"

#include "ash/shell.h"
#include "ash/shell/panel_window.h"
#include "ash/shell_window_ids.h"
#include "ash/system/audio/tray_volume.h"
#include "ash/system/bluetooth/tray_bluetooth.h"
#include "ash/system/brightness/tray_brightness.h"
#include "ash/system/date/tray_date.h"
#include "ash/system/drive/tray_drive.h"
#include "ash/system/ime/tray_ime.h"
#include "ash/system/locale/tray_locale.h"
#include "ash/system/network/tray_network.h"
#include "ash/system/network/tray_sms.h"
#include "ash/system/power/power_status_observer.h"
#include "ash/system/power/power_supply_status.h"
#include "ash/system/power/tray_power.h"
#include "ash/system/settings/tray_settings.h"
#include "ash/system/status_area_widget.h"
#include "ash/system/tray/system_tray_bubble.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "ash/system/tray/system_tray_item.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/system/tray_accessibility.h"
#include "ash/system/tray_caps_lock.h"
#include "ash/system/tray_update.h"
#include "ash/system/user/login_status.h"
#include "ash/system/user/tray_user.h"
#include "ash/wm/shelf_layout_manager.h"
#include "base/logging.h"
#include "base/timer.h"
#include "base/utf_string_conversions.h"
#include "grit/ash_strings.h"
#include "ui/aura/root_window.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/base/events.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/screen.h"
#include "ui/gfx/skia_util.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"

namespace ash {

namespace internal {

// Observe the tray layer animation and update the anchor when it changes.
// TODO(stevenjb): Observe or mirror the actual animation, not just the start
// and end points.
class SystemTrayLayerAnimationObserver : public ui::LayerAnimationObserver {
 public:
  explicit SystemTrayLayerAnimationObserver(SystemTray* host) : host_(host) {}

  virtual void OnLayerAnimationEnded(ui::LayerAnimationSequence* sequence) {
    host_->UpdateNotificationAnchor();
  }

  virtual void OnLayerAnimationAborted(ui::LayerAnimationSequence* sequence) {
    host_->UpdateNotificationAnchor();
  }

  virtual void OnLayerAnimationScheduled(ui::LayerAnimationSequence* sequence) {
    host_->UpdateNotificationAnchor();
  }

 private:
  SystemTray* host_;

  DISALLOW_COPY_AND_ASSIGN(SystemTrayLayerAnimationObserver);
};

}  // namespace internal

// SystemTray

using internal::SystemTrayBubble;
using internal::SystemTrayLayerAnimationObserver;
using internal::TrayBubbleView;

SystemTray::SystemTray(internal::StatusAreaWidget* status_area_widget)
    : internal::TrayBackgroundView(status_area_widget),
      items_(),
      accessibility_observer_(NULL),
      audio_observer_(NULL),
      bluetooth_observer_(NULL),
      brightness_observer_(NULL),
      caps_lock_observer_(NULL),
      clock_observer_(NULL),
      drive_observer_(NULL),
      ime_observer_(NULL),
      locale_observer_(NULL),
      network_observer_(NULL),
      update_observer_(NULL),
      user_observer_(NULL),
      should_show_launcher_(false),
      default_bubble_height_(0),
      hide_notifications_(false) {
}

SystemTray::~SystemTray() {
  bubble_.reset();
  for (std::vector<SystemTrayItem*>::iterator it = items_.begin();
       it != items_.end();
       ++it) {
    (*it)->DestroyTrayView();
  }
}

void SystemTray::Initialize() {
  layer_animation_observer_.reset(new SystemTrayLayerAnimationObserver(this));
  GetWidget()->GetNativeView()->layer()->GetAnimator()->AddObserver(
      layer_animation_observer_.get());
}

void SystemTray::CreateItems() {
  internal::TrayVolume* tray_volume = new internal::TrayVolume();
  internal::TrayBluetooth* tray_bluetooth = new internal::TrayBluetooth();
  internal::TrayBrightness* tray_brightness = new internal::TrayBrightness();
  internal::TrayDate* tray_date = new internal::TrayDate();
  internal::TrayPower* tray_power = new internal::TrayPower();
  internal::TrayNetwork* tray_network = new internal::TrayNetwork;
  internal::TraySms* tray_sms = new internal::TraySms();
  internal::TrayUser* tray_user = new internal::TrayUser;
  internal::TrayAccessibility* tray_accessibility =
      new internal::TrayAccessibility;
  internal::TrayCapsLock* tray_caps_lock = new internal::TrayCapsLock;
  internal::TrayDrive* tray_drive = new internal::TrayDrive;
  internal::TrayIME* tray_ime = new internal::TrayIME;
  internal::TrayLocale* tray_locale = new internal::TrayLocale;
  internal::TrayUpdate* tray_update = new internal::TrayUpdate;
  internal::TraySettings* tray_settings = new internal::TraySettings();

  accessibility_observer_ = tray_accessibility;
  audio_observer_ = tray_volume;
  bluetooth_observer_ = tray_bluetooth;
  brightness_observer_ = tray_brightness;
  caps_lock_observer_ = tray_caps_lock;
  clock_observer_ = tray_date;
  drive_observer_ = tray_drive;
  ime_observer_ = tray_ime;
  locale_observer_ = tray_locale;
  network_observer_ = tray_network;
  power_status_observers_.AddObserver(tray_power);
  power_status_observers_.AddObserver(tray_settings);
  sms_observer_ = tray_sms;
  update_observer_ = tray_update;
  user_observer_ = tray_user;

  AddTrayItem(tray_user);
  AddTrayItem(tray_power);
  AddTrayItem(tray_network);
  AddTrayItem(tray_bluetooth);
  AddTrayItem(tray_sms);
  AddTrayItem(tray_drive);
  AddTrayItem(tray_ime);
  AddTrayItem(tray_locale);
  AddTrayItem(tray_volume);
  AddTrayItem(tray_brightness);
  AddTrayItem(tray_update);
  AddTrayItem(tray_accessibility);
  AddTrayItem(tray_caps_lock);
  AddTrayItem(tray_settings);
  AddTrayItem(tray_date);
  SetVisible(ash::Shell::GetInstance()->tray_delegate()->
      GetTrayVisibilityOnStartup());
}

void SystemTray::AddTrayItem(SystemTrayItem* item) {
  items_.push_back(item);

  SystemTrayDelegate* delegate = Shell::GetInstance()->tray_delegate();
  views::View* tray_item = item->CreateTrayView(delegate->GetUserLoginStatus());
  item->UpdateAfterShelfAlignmentChange(
      ash::Shell::GetInstance()->system_tray()->shelf_alignment());

  if (tray_item) {
    tray_container()->AddChildViewAt(tray_item, 0);
    PreferredSizeChanged();
    tray_item_map_[item] = tray_item;
  }
}

void SystemTray::RemoveTrayItem(SystemTrayItem* item) {
  NOTIMPLEMENTED();
}

void SystemTray::ShowDefaultView(BubbleCreationType creation_type) {
  ShowDefaultViewWithOffset(creation_type,
                            TrayBubbleView::InitParams::kArrowDefaultOffset);
}

void SystemTray::ShowDetailedView(SystemTrayItem* item,
                                  int close_delay,
                                  bool activate,
                                  BubbleCreationType creation_type) {
  std::vector<SystemTrayItem*> items;
  items.push_back(item);
  ShowItems(items, true, activate, creation_type, GetTrayXOffset(item));
  bubble_->StartAutoCloseTimer(close_delay);
}

void SystemTray::SetDetailedViewCloseDelay(int close_delay) {
  if (bubble_.get() &&
      bubble_->bubble_type() == SystemTrayBubble::BUBBLE_TYPE_DETAILED)
    bubble_->StartAutoCloseTimer(close_delay);
}

void SystemTray::HideDetailedView(SystemTrayItem* item) {
  if (item != detailed_item_)
    return;
  DestroyBubble();
  UpdateNotificationBubble();
}

void SystemTray::ShowNotificationView(SystemTrayItem* item) {
  if (std::find(notification_items_.begin(), notification_items_.end(), item)
      != notification_items_.end())
    return;
  notification_items_.push_back(item);
  UpdateNotificationBubble();
}

void SystemTray::HideNotificationView(SystemTrayItem* item) {
  std::vector<SystemTrayItem*>::iterator found_iter =
      std::find(notification_items_.begin(), notification_items_.end(), item);
  if (found_iter == notification_items_.end())
    return;
  notification_items_.erase(found_iter);
  // Only update the notification bubble if visible (i.e. don't create one).
  if (notification_bubble_.get())
    UpdateNotificationBubble();
}

void SystemTray::UpdateAfterLoginStatusChange(user::LoginStatus login_status) {
  DestroyBubble();

  for (std::vector<SystemTrayItem*>::iterator it = items_.begin();
      it != items_.end();
      ++it) {
    (*it)->UpdateAfterLoginStatusChange(login_status);
  }

  SetVisible(true);
  PreferredSizeChanged();
}

void SystemTray::UpdateAfterShelfAlignmentChange(ShelfAlignment alignment) {
  for (std::vector<SystemTrayItem*>::iterator it = items_.begin();
      it != items_.end();
      ++it) {
    (*it)->UpdateAfterShelfAlignmentChange(alignment);
  }
}

void SystemTray::SetHideNotifications(bool hide_notifications) {
  if (notification_bubble_.get())
    notification_bubble_->SetVisible(!hide_notifications);
  hide_notifications_ = hide_notifications;
}

bool SystemTray::IsAnyBubbleVisible() const {
  if (bubble_.get() && bubble_->IsVisible())
    return true;
  if (notification_bubble_.get() && notification_bubble_->IsVisible())
    return true;
  return false;
}

bool SystemTray::CloseBubbleForTest() const {
  if (!bubble_.get())
    return false;
  bubble_->Close();
  return true;
}

// Private methods.

void SystemTray::DestroyBubble() {
  bubble_.reset();
  detailed_item_ = NULL;
}

void SystemTray::RemoveBubble(SystemTrayBubble* bubble) {
  if (bubble == bubble_.get()) {
    DestroyBubble();
    UpdateNotificationBubble();  // State changed, re-create notifications.
    if (should_show_launcher_) {
      // No need to show the launcher if the mouse isn't over the status area
      // anymore.
      should_show_launcher_ = GetWidget()->GetWindowBoundsInScreen().Contains(
          gfx::Screen::GetCursorScreenPoint());
      if (!should_show_launcher_)
        Shell::GetInstance()->shelf()->UpdateAutoHideState();
    }
  } else if (bubble == notification_bubble_) {
    notification_bubble_.reset();
  } else {
    NOTREACHED();
  }
}

int SystemTray::GetTrayXOffset(SystemTrayItem* item) const {
  // Don't attempt to align the arrow if the shelf is on the left or right.
  if (shelf_alignment() != SHELF_ALIGNMENT_BOTTOM)
    return TrayBubbleView::InitParams::kArrowDefaultOffset;

  std::map<SystemTrayItem*, views::View*>::const_iterator it =
      tray_item_map_.find(item);
  if (it == tray_item_map_.end())
    return TrayBubbleView::InitParams::kArrowDefaultOffset;

  const views::View* item_view = it->second;
  if (item_view->bounds().IsEmpty()) {
    // The bounds of item could be still empty if it does not have a visible
    // tray view. In that case, use the default (minimum) offset.
    return TrayBubbleView::InitParams::kArrowDefaultOffset;
  }

  gfx::Point point(item_view->width() / 2, 0);
  ConvertPointToWidget(item_view, &point);
  return point.x();
}

void SystemTray::ShowDefaultViewWithOffset(BubbleCreationType creation_type,
                                           int arrow_offset) {
  ShowItems(items_.get(), false, true, creation_type, arrow_offset);
}

void SystemTray::ShowItems(const std::vector<SystemTrayItem*>& items,
                           bool detailed,
                           bool can_activate,
                           BubbleCreationType creation_type,
                           int arrow_offset) {
  // Destroy any existing bubble and create a new one.
  SystemTrayBubble::BubbleType bubble_type = detailed ?
      SystemTrayBubble::BUBBLE_TYPE_DETAILED :
      SystemTrayBubble::BUBBLE_TYPE_DEFAULT;

  // Destroy the notification bubble here so that it doesn't get rebuilt
  // while we add items to the main bubble_ (e.g. in HideNotificationView).
  notification_bubble_.reset();

  if (bubble_.get() && creation_type == BUBBLE_USE_EXISTING) {
    bubble_->UpdateView(items, bubble_type);
  } else {
    bubble_.reset(new SystemTrayBubble(this, items, bubble_type));
    ash::SystemTrayDelegate* delegate =
        ash::Shell::GetInstance()->tray_delegate();
    views::View* anchor = tray_container();
    TrayBubbleView::InitParams init_params(TrayBubbleView::ANCHOR_TYPE_TRAY,
                                           shelf_alignment());
    init_params.can_activate = can_activate;
    if (detailed) {
      // This is the case where a volume control or brightness control bubble
      // is created.
      init_params.max_height = default_bubble_height_;
      init_params.arrow_color = kBackgroundColor;
    }
    init_params.arrow_offset = arrow_offset;
    bubble_->InitView(anchor, init_params, delegate->GetUserLoginStatus());
  }
  // Save height of default view for creating detailed views directly.
  if (!detailed)
    default_bubble_height_ = bubble_->bubble_view()->height();

  if (detailed && items.size() > 0)
    detailed_item_ = items[0];
  else
    detailed_item_ = NULL;

  // If we have focus the shelf should be visible and we need to continue
  // showing the shelf when the popup is shown.
  if (GetWidget()->IsActive())
    should_show_launcher_ = true;

  UpdateNotificationBubble();  // State changed, re-create notifications.
  status_area_widget()->HideNonSystemNotifications();
}

void SystemTray::UpdateNotificationBubble() {
  // Only show the notification buble if we have notifications and we are not
  // showing the default bubble.
  if (notification_items_.empty() ||
      (bubble_.get() &&
       bubble_->bubble_type() == SystemTrayBubble::BUBBLE_TYPE_DEFAULT)) {
    notification_bubble_.reset();
    return;
  }
  if (bubble_.get() &&
      bubble_->bubble_type() == SystemTrayBubble::BUBBLE_TYPE_DETAILED) {
    // Skip notifications for any currently displayed detailed item.
    std::vector<SystemTrayItem*> items;
    for (std::vector<SystemTrayItem*>::iterator iter =
             notification_items_.begin();
         iter != notification_items_.end(); ++ iter) {
      if (*iter != detailed_item_)
        items.push_back(*iter);
    }
    notification_bubble_.reset(new SystemTrayBubble(
        this, items, SystemTrayBubble::BUBBLE_TYPE_NOTIFICATION));
  } else {
    // Show all notifications.
    notification_bubble_.reset(new SystemTrayBubble(
        this, notification_items_, SystemTrayBubble::BUBBLE_TYPE_NOTIFICATION));
  }
  views::View* anchor;
  TrayBubbleView::AnchorType anchor_type;
  if (bubble_.get()) {
    anchor = bubble_->bubble_view();
    anchor_type = TrayBubbleView::ANCHOR_TYPE_BUBBLE;
  } else {
    anchor = tray_container();
    anchor_type = TrayBubbleView::ANCHOR_TYPE_TRAY;
  }
  TrayBubbleView::InitParams init_params(anchor_type, shelf_alignment());
  init_params.arrow_offset = GetTrayXOffset(notification_items_[0]);
  init_params.arrow_color = kBackgroundColor;
  user::LoginStatus login_status =
      Shell::GetInstance()->tray_delegate()->GetUserLoginStatus();
  notification_bubble_->InitView(anchor, init_params, login_status);
  if (hide_notifications_)
    notification_bubble_->SetVisible(false);
  else
    status_area_widget()->HideNonSystemNotifications();
}

void SystemTray::UpdateNotificationAnchor() {
  if (!notification_bubble_.get())
    return;
  notification_bubble_->bubble_view()->UpdateBubble();
  // Ensure that the notification buble is above the launcher/status area.
  notification_bubble_->bubble_view()->GetWidget()->StackAtTop();
}

void SystemTray::SetShelfAlignment(ShelfAlignment alignment) {
  if (alignment == shelf_alignment())
    return;
  internal::TrayBackgroundView::SetShelfAlignment(alignment);
  UpdateAfterShelfAlignmentChange(alignment);
  // Destroy any existing bubble so that it is rebuilt correctly.
  bubble_.reset();
  // Rebuild any notification bubble.
  if (notification_bubble_.get()) {
    notification_bubble_.reset();
    UpdateNotificationBubble();
  }
}

bool SystemTray::PerformAction(const views::Event& event) {
  // If we're already showing the default view, hide it; otherwise, show it
  // (and hide any popup that's currently shown).
  if (bubble_.get() &&
      bubble_->bubble_type() == SystemTrayBubble::BUBBLE_TYPE_DEFAULT) {
    bubble_->Close();
  } else {
    int arrow_offset = TrayBubbleView::InitParams::kArrowDefaultOffset;
    if (event.IsMouseEvent() || event.type() == ui::ET_GESTURE_TAP) {
      const views::LocatedEvent& located_event =
          static_cast<const views::LocatedEvent&>(event);
      if (shelf_alignment() == SHELF_ALIGNMENT_BOTTOM) {
        gfx::Point point(located_event.x(), 0);
        ConvertPointToWidget(this, &point);
        arrow_offset = point.x();
      }
    }
    ShowDefaultViewWithOffset(BUBBLE_CREATE_NEW, arrow_offset);
  }
  return true;
}

void SystemTray::OnMouseEntered(const views::MouseEvent& event) {
  TrayBackgroundView::OnMouseEntered(event);
  should_show_launcher_ = true;
}

void SystemTray::OnMouseExited(const views::MouseEvent& event) {
  TrayBackgroundView::OnMouseExited(event);
  // When the popup closes we'll update |should_show_launcher_|.
  if (!bubble_.get())
    should_show_launcher_ = false;
}

void SystemTray::AboutToRequestFocusFromTabTraversal(bool reverse) {
  views::View* v = GetNextFocusableView();
  if (v)
    v->AboutToRequestFocusFromTabTraversal(reverse);
}

void SystemTray::GetAccessibleState(ui::AccessibleViewState* state) {
  state->role = ui::AccessibilityTypes::ROLE_PUSHBUTTON;
  state->name = l10n_util::GetStringUTF16(
      IDS_ASH_STATUS_TRAY_ACCESSIBLE_NAME);
}

void SystemTray::OnPaintFocusBorder(gfx::Canvas* canvas) {
  // The tray itself expands to the right and bottom edge of the screen to make
  // sure clicking on the edges brings up the popup. However, the focus border
  // should be only around the container.
  if (GetWidget() && GetWidget()->IsActive())
    DrawBorder(canvas, GetContentsBounds());
}

}  // namespace ash
