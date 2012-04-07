// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/status/caps_lock_menu_button.h"

#include <string>

#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/input_method/input_method_manager.h"
#include "chrome/browser/chromeos/input_method/xkeyboard.h"
#include "chrome/browser/chromeos/login/screen_locker.h"
#include "chrome/browser/chromeos/status/status_area_bubble.h"
#include "chrome/browser/chromeos/system/runtime_environment.h"
#include "chrome/browser/chromeos/view_ids.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/pref_names.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/widget/widget.h"

namespace {

const size_t kMaxBubbleCount = 3;

PrefService* GetPrefService() {
  Profile* profile = ProfileManager::GetDefaultProfile();
  if (profile)
    return profile->GetPrefs();
  return NULL;
}

views::ImageView* CreateImageViewWithCapsLockIcon() {
  const gfx::Image& image =
      ResourceBundle::GetSharedInstance().GetImageNamed(IDR_CAPS_LOCK_ICON);
  views::ImageView* image_view = new views::ImageView;
  image_view->SetImage(image.ToSkBitmap());
  return image_view;
}

}  // namespace

namespace chromeos {

////////////////////////////////////////////////////////////////////////////////
// CapsLockMenuButton

CapsLockMenuButton::CapsLockMenuButton(StatusAreaButton::Delegate* delegate)
    : StatusAreaButton(delegate, this),
      prefs_(GetPrefService()),
      status_(NULL),
      should_show_bubble_(true),
      bubble_count_(0) {
  set_id(VIEW_ID_STATUS_BUTTON_CAPS_LOCK);
  if (prefs_)
    remap_search_key_to_.Init(
        prefs::kLanguageXkbRemapSearchKeyTo, prefs_, this);

  SetIcon(*ResourceBundle::GetSharedInstance().GetBitmapNamed(
      IDR_STATUSBAR_CAPS_LOCK));
  UpdateAccessibleName();

  input_method::InputMethodManager* ime_manager =
      input_method::InputMethodManager::GetInstance();
  UpdateUIFromCurrentCapsLock(ime_manager->GetXKeyboard()->CapsLockIsEnabled());

  // Status bar should be initialized after SystemKeyEventListener on the
  // device. SystemKeyEventListener is never initialized on chrome for cros
  // running on linux.
  DCHECK(SystemKeyEventListener::GetInstance() ||
         !system::runtime_environment::IsRunningOnChromeOS());
  if (SystemKeyEventListener::GetInstance())
    SystemKeyEventListener::GetInstance()->AddCapsLockObserver(this);
}

CapsLockMenuButton::~CapsLockMenuButton() {
  if (SystemKeyEventListener::GetInstance())
    SystemKeyEventListener::GetInstance()->RemoveCapsLockObserver(this);
}

////////////////////////////////////////////////////////////////////////////////
// views::View implementation:

void CapsLockMenuButton::OnLocaleChanged() {
  input_method::InputMethodManager* ime_manager =
      input_method::InputMethodManager::GetInstance();
  UpdateUIFromCurrentCapsLock(ime_manager->GetXKeyboard()->CapsLockIsEnabled());
}

////////////////////////////////////////////////////////////////////////////////
// views::MenuDelegate implementation:

string16 CapsLockMenuButton::GetLabel(int id) const {
  return string16();
}

////////////////////////////////////////////////////////////////////////////////
// views::ViewMenuDelegate implementation:

void CapsLockMenuButton::RunMenu(views::View* source, const gfx::Point& pt) {
  static const int kDummyCommandId = 1000;

  if (IsBubbleShown())
    HideBubble();

  views::MenuItemView* menu = new views::MenuItemView(this);
  // MenuRunner takes ownership of |menu|.
  menu_runner_.reset(new views::MenuRunner(menu));
  views::MenuItemView* submenu = menu->AppendMenuItem(
      kDummyCommandId,
      string16(),
      views::MenuItemView::NORMAL);
  status_ = new StatusAreaBubbleContentView(CreateImageViewWithCapsLockIcon(),
                                            GetText());
  submenu->AddChildView(status_);
  menu->CreateSubmenu()->set_resize_open_menu(true);
  menu->SetMargins(0, 0);
  submenu->SetMargins(0, 0);
  menu->ChildrenChanged();

  gfx::Point screen_location;
  views::View::ConvertPointToScreen(source, &screen_location);
  gfx::Rect bounds(screen_location, source->size());
  if (menu_runner_->RunMenuAt(
          source->GetWidget()->GetTopLevelWidget(), this, bounds,
          views::MenuItemView::TOPRIGHT, views::MenuRunner::HAS_MNEMONICS) ==
      views::MenuRunner::MENU_DELETED)
    return;
  status_ = NULL;
  menu_runner_.reset(NULL);
}

////////////////////////////////////////////////////////////////////////////////
// SystemKeyEventListener::CapsLockObserver implementation

void CapsLockMenuButton::OnCapsLockChange(bool enabled) {
  if (!enabled && !HasCapsLock() && bubble_count_ > 0) {
    // Both shift keys are pressed. We can assume that the user now recognizes
    // how to turn off Caps Lock.
    should_show_bubble_ = false;
  }

  // Update the indicator.
  UpdateUIFromCurrentCapsLock(enabled);

  // Update the drop-down menu and bubble. Since the constructor also calls
  // UpdateUIFromCurrentCapsLock, we shouldn't do this in the function.
  if (enabled && IsMenuShown()) {
    // Update the drop-down menu if it's already shown.
    status_->SetMessage(GetText());
  } else if (!enabled && IsMenuShown()) {
    HideMenu();
  }
  if (enabled)
    MaybeShowBubble();
  else if (!enabled && IsBubbleShown())
    HideBubble();
}

////////////////////////////////////////////////////////////////////////////////
// content::NotificationObserver implementation

void CapsLockMenuButton::Observe(int type,
                                 const content::NotificationSource& source,
                                 const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_PREF_CHANGED)
    UpdateAccessibleName();
}

void CapsLockMenuButton::UpdateAccessibleName() {
  int id = IDS_STATUSBAR_CAPS_LOCK_ENABLED_PRESS_SHIFT_AND_SEARCH_KEYS;
  if (HasCapsLock())
    id = IDS_STATUSBAR_CAPS_LOCK_ENABLED_PRESS_SEARCH;
  SetAccessibleName(l10n_util::GetStringUTF16(id));
}

string16 CapsLockMenuButton::GetText() const {
  int id = IDS_STATUSBAR_CAPS_LOCK_ENABLED_PRESS_SHIFT_AND_SEARCH_KEYS;
  if (HasCapsLock())
    id = IDS_STATUSBAR_CAPS_LOCK_ENABLED_PRESS_SEARCH;
  return l10n_util::GetStringUTF16(id);
}

void CapsLockMenuButton::UpdateUIFromCurrentCapsLock(bool enabled) {
  SetVisible(enabled);
  SchedulePaint();
}

bool CapsLockMenuButton::IsMenuShown() const {
  return menu_runner_.get() && status_;
}

void CapsLockMenuButton::HideMenu() {
  if (!IsMenuShown())
    return;
  menu_runner_->Cancel();
}

bool CapsLockMenuButton::IsBubbleShown() const {
  return bubble_controller_.get() && bubble_controller_->IsBubbleShown();
}

void CapsLockMenuButton::MaybeShowBubble() {
  if (IsBubbleShown() ||
      // We've already shown the bubble |kMaxBubbleCount| times.
      !should_show_bubble_ ||
      // Don't show the bubble when Caps Lock key is available.
      HasCapsLock() ||
      // Don't show it when the status area is hidden.
      (parent() && !parent()->visible()) ||
      // Don't show the bubble when screen is locked as this results in two
      // visible caps lock bubbles (crbug.com/105280). The greater problem of
      // displaying bubbles from all caps lock menu buttons regardless of
      // visibility is described in crbug.com/106776.
      ScreenLocker::default_screen_locker())
    return;

  ++bubble_count_;
  if (bubble_count_ > kMaxBubbleCount) {
    should_show_bubble_ = false;
  } else {
    CreateAndShowBubble();
  }
}

void CapsLockMenuButton::CreateAndShowBubble() {
  if (IsBubbleShown()) {
    NOTREACHED();
    return;
  }
  bubble_controller_.reset(
      StatusAreaBubbleController::ShowBubbleUnderViewForAWhile(
          this,
          new StatusAreaBubbleContentView(CreateImageViewWithCapsLockIcon(),
                                          GetText())));
}

void CapsLockMenuButton::HideBubble() {
  bubble_controller_.reset();
}

bool CapsLockMenuButton::HasCapsLock() const {
  return (prefs_ &&
          (remap_search_key_to_.GetValue() == input_method::kCapsLockKey)) ||
      // A keyboard for Linux usually has Caps Lock.
      !system::runtime_environment::IsRunningOnChromeOS();
}

}  // namespace chromeos
