// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note: this file is used by Aura on all platforms, even though it is currently
// in a chromeos specific location.

#include "chrome/browser/chromeos/status/clock_menu_button.h"

#include "base/basictypes.h"
#include "base/i18n/time_formatting.h"
#include "base/string_util.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/status/status_area_view_chromeos.h"
#include "chrome/browser/chromeos/view_ids.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"
#include "unicode/datefmt.h"

namespace {

// views::MenuItemView item ids
enum ClockMenuItem {
  CLOCK_DISPLAY_ITEM,
  CLOCK_OPEN_OPTIONS_ITEM
};

}  // namespace

// Amount of slop to add into the timer to make sure we're into the next minute
// when the timer goes off.
const int kTimerSlopSeconds = 1;

ClockMenuButton::ClockMenuButton(StatusAreaButton::Delegate* delegate)
    : StatusAreaButton(delegate, this),
      pref_service_(NULL),
      use_24hour_clock_(false) {
  set_id(VIEW_ID_STATUS_BUTTON_CLOCK);
  UpdateProfile();
  UpdateTextAndSetNextTimer();
}

ClockMenuButton::~ClockMenuButton() {
  timer_.Stop();
}

void ClockMenuButton::UpdateProfile() {
#if defined(OS_CHROMEOS)  // See note at top of file
  // Start monitoring the kUse24HourClock preference.
  Profile* profile = ProfileManager::GetDefaultProfile();
  if (profile && profile->GetPrefs() != pref_service_) {
    pref_service_ = profile->GetPrefs();
    use_24hour_clock_ = pref_service_->GetBoolean(prefs::kUse24HourClock);
    registrar_.reset(new PrefChangeRegistrar);
    registrar_->Init(pref_service_);
    registrar_->Add(prefs::kUse24HourClock, this);
    UpdateText();
  }
#endif
}

void ClockMenuButton::UpdateTextAndSetNextTimer() {
  UpdateText();

  // Try to set the timer to go off at the next change of the minute. We don't
  // want to have the timer go off more than necessary since that will cause
  // the CPU to wake up and consume power.
  base::Time now = base::Time::Now();
  base::Time::Exploded exploded;
  now.LocalExplode(&exploded);

  // Often this will be called at minute boundaries, and we'll actually want
  // 60 seconds from now.
  int seconds_left = 60 - exploded.second;
  if (seconds_left == 0)
    seconds_left = 60;

  // Make sure that the timer fires on the next minute. Without this, if it is
  // called just a teeny bit early, then it will skip the next minute.
  seconds_left += kTimerSlopSeconds;

  timer_.Start(FROM_HERE, base::TimeDelta::FromSeconds(seconds_left), this,
               &ClockMenuButton::UpdateTextAndSetNextTimer);
}

void ClockMenuButton::UpdateText() {
  base::Time time(base::Time::Now());
  SetText(base::TimeFormatTimeOfDayWithHourClockType(
      time,
      use_24hour_clock_ ? base::k24HourClock : base::k12HourClock,
      base::kDropAmPm));
  string16 friendly_time_string(base::TimeFormatFriendlyDateAndTime(time));
  SetTooltipText(friendly_time_string);
  SetAccessibleName(friendly_time_string);
  SchedulePaint();
}

void ClockMenuButton::SetUse24HourClock(bool use_24hour_clock) {
  if (use_24hour_clock_ == use_24hour_clock)
    return;
  use_24hour_clock_ = use_24hour_clock;
  UpdateText();
}

// ClockMenuButton, content::NotificationObserver implementation:

void ClockMenuButton::Observe(int type,
                              const content::NotificationSource& source,
                              const content::NotificationDetails& details) {
#if defined(OS_CHROMEOS)  // See note at top of file
  if (type == chrome::NOTIFICATION_PREF_CHANGED) {
    std::string* pref_name = content::Details<std::string>(details).ptr();
    if (*pref_name == prefs::kUse24HourClock) {
      Profile* profile = ProfileManager::GetDefaultProfile();
      if (profile) {
        SetUse24HourClock(
            profile->GetPrefs()->GetBoolean(prefs::kUse24HourClock));
      }
    }
  }
#endif
}

// ClockMenuButton, views::MenuDelegate implementation:
string16 ClockMenuButton::GetLabel(int id) const {
  DCHECK_EQ(CLOCK_DISPLAY_ITEM, id);
  return base::TimeFormatFriendlyDate(base::Time::Now());
}

bool ClockMenuButton::IsCommandEnabled(int id) const {
  DCHECK(id == CLOCK_DISPLAY_ITEM || id == CLOCK_OPEN_OPTIONS_ITEM);
  return id == CLOCK_OPEN_OPTIONS_ITEM;
}

void ClockMenuButton::ExecuteCommand(int id) {
  DCHECK_EQ(CLOCK_OPEN_OPTIONS_ITEM, id);
  delegate()->ExecuteStatusAreaCommand(
      this, StatusAreaButton::Delegate::SHOW_SYSTEM_OPTIONS);
}

// StatusAreaButton implementation
void ClockMenuButton::SetMenuActive(bool active) {
  // Activation gets updated when we change login state, so profile may change.
  if (active)
    UpdateProfile();
  StatusAreaButton::SetMenuActive(active);
}

int ClockMenuButton::horizontal_padding() {
  return 3;
}

// ClockMenuButton, views::ViewMenuDelegate implementation:

void ClockMenuButton::RunMenu(views::View* source, const gfx::Point& pt) {
  // View passed in must be a views::MenuButton, i.e. the ClockMenuButton.
  DCHECK_EQ(source, this);

  scoped_ptr<views::MenuRunner> menu_runner(CreateMenu());

  gfx::Point screen_location;
  views::View::ConvertPointToScreen(source, &screen_location);
  gfx::Rect bounds(screen_location, source->size());
  ignore_result(menu_runner->RunMenuAt(
                    source->GetWidget()->GetTopLevelWidget(),
                    this,
                    bounds,
                    views::MenuItemView::TOPRIGHT,
                    views::MenuRunner::HAS_MNEMONICS));
}

// ClockMenuButton, views::View implementation:

void ClockMenuButton::OnLocaleChanged() {
  UpdateText();
}

views::MenuRunner* ClockMenuButton::CreateMenu() {
  views::MenuItemView* menu = new views::MenuItemView(this);
  // menu_runner takes ownership of menu.
  views::MenuRunner* menu_runner = new views::MenuRunner(menu);

  // Text for this item will be set by GetLabel().
  menu->AppendDelegateMenuItem(CLOCK_DISPLAY_ITEM);

  // If options UI is available, show a separator and configure menu item.
  if (delegate()->ShouldExecuteStatusAreaCommand(
          this, StatusAreaButton::Delegate::SHOW_SYSTEM_OPTIONS)) {
    menu->AppendSeparator();

    const string16 clock_open_options_label =
        l10n_util::GetStringUTF16(IDS_STATUSBAR_CLOCK_OPEN_OPTIONS_DIALOG);
    menu->AppendMenuItemWithLabel(CLOCK_OPEN_OPTIONS_ITEM,
                                  clock_open_options_label);
  }
  return menu_runner;
}
