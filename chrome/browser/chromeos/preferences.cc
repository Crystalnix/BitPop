// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/preferences.h"

#include "base/chromeos/chromeos_version.h"
#include "base/command_line.h"
#include "base/i18n/time_formatting.h"
#include "base/metrics/histogram.h"
#include "base/prefs/public/pref_member.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/drive/drive_file_system_util.h"
#include "chrome/browser/chromeos/input_method/input_method_configuration.h"
#include "chrome/browser/chromeos/input_method/input_method_manager.h"
#include "chrome/browser/chromeos/input_method/input_method_util.h"
#include "chrome/browser/chromeos/input_method/xkeyboard.h"
#include "chrome/browser/chromeos/login/login_utils.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/system/drm_settings.h"
#include "chrome/browser/chromeos/system/input_device_settings.h"
#include "chrome/browser/chromeos/system/power_manager_settings.h"
#include "chrome/browser/chromeos/system/statistics_provider.h"
#include "chrome/browser/download/download_util.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "googleurl/src/gurl.h"
#include "ui/base/events/event_constants.h"
#include "ui/base/events/event_utils.h"
#include "unicode/timezone.h"

namespace chromeos {

static const char kFallbackInputMethodLocale[] = "en-US";

Preferences::Preferences()
    : prefs_(NULL),
      input_method_manager_(input_method::GetInputMethodManager()) {
}

Preferences::Preferences(input_method::InputMethodManager* input_method_manager)
    : input_method_manager_(input_method_manager) {
}

Preferences::~Preferences() {}

// static
void Preferences::RegisterUserPrefs(PrefService* prefs) {
  std::string hardware_keyboard_id;
  // TODO(yusukes): Remove the runtime hack.
  if (base::chromeos::IsRunningOnChromeOS()) {
    input_method::InputMethodManager* manager =
        input_method::GetInputMethodManager();
    if (manager) {
      hardware_keyboard_id =
          manager->GetInputMethodUtil()->GetHardwareInputMethodId();
    }
  } else {
    hardware_keyboard_id = "xkb:us::eng";  // only for testing.
  }

  prefs->RegisterBooleanPref(prefs::kTapToClickEnabled,
                             true,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kTapDraggingEnabled,
                             false,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kEnableTouchpadThreeFingerClick,
                             false,
                             PrefService::UNSYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kEnableTouchpadThreeFingerSwipe,
                             false,
                             PrefService::UNSYNCABLE_PREF);
  prefs->RegisterBooleanPref(
      prefs::kNaturalScroll,
      CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kNaturalScrollDefault),
      PrefService::SYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kPrimaryMouseButtonRight,
                             false,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kLabsMediaplayerEnabled,
                             false,
                             PrefService::UNSYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kLabsAdvancedFilesystemEnabled,
                             false,
                             PrefService::UNSYNCABLE_PREF);
  // Check if the accessibility pref is already registered, which can happen
  // in WizardController::RegisterPrefs. We still want to try to register
  // the pref here in case of Chrome/Linux with ChromeOS=1.
  if (prefs->FindPreference(prefs::kSpokenFeedbackEnabled) == NULL) {
    prefs->RegisterBooleanPref(prefs::kSpokenFeedbackEnabled,
                               false,
                               PrefService::UNSYNCABLE_PREF);
  }
  if (prefs->FindPreference(prefs::kHighContrastEnabled) == NULL) {
    prefs->RegisterBooleanPref(prefs::kHighContrastEnabled,
                               false,
                               PrefService::UNSYNCABLE_PREF);
  }
  if (prefs->FindPreference(prefs::kScreenMagnifierEnabled) == NULL) {
    prefs->RegisterBooleanPref(prefs::kScreenMagnifierEnabled,
                               false,
                               PrefService::SYNCABLE_PREF);
  }
  if (prefs->FindPreference(prefs::kScreenMagnifierScale) == NULL) {
    prefs->RegisterDoublePref(prefs::kScreenMagnifierScale,
                              std::numeric_limits<double>::min(),
                              PrefService::UNSYNCABLE_PREF);
  }
  if (prefs->FindPreference(prefs::kShouldAlwaysShowAccessibilityMenu) ==
      NULL) {
    prefs->RegisterBooleanPref(prefs::kShouldAlwaysShowAccessibilityMenu,
                               false,
                               PrefService::UNSYNCABLE_PREF);
  }
  if (prefs->FindPreference(prefs::kVirtualKeyboardEnabled) == NULL) {
    prefs->RegisterBooleanPref(prefs::kVirtualKeyboardEnabled,
                               false,
                               PrefService::UNSYNCABLE_PREF);
  }
  prefs->RegisterIntegerPref(prefs::kMouseSensitivity,
                             3,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterIntegerPref(prefs::kTouchpadSensitivity,
                             3,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kUse24HourClock,
                             base::GetHourClockType() == base::k24HourClock,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kDisableDrive,
                             false,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kDisableDriveOverCellular,
                             true,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kDisableDriveHostedFiles,
                             false,
                             PrefService::SYNCABLE_PREF);
  // We don't sync prefs::kLanguageCurrentInputMethod and PreviousInputMethod
  // because they're just used to track the logout state of the device.
  prefs->RegisterStringPref(prefs::kLanguageCurrentInputMethod,
                            "",
                            PrefService::UNSYNCABLE_PREF);
  prefs->RegisterStringPref(prefs::kLanguagePreviousInputMethod,
                            "",
                            PrefService::UNSYNCABLE_PREF);
  // We don't sync the list of input methods and preferred languages since a
  // user might use two or more devices with different hardware keyboards.
  // crosbug.com/15181
  prefs->RegisterStringPref(prefs::kLanguagePreferredLanguages,
                            kFallbackInputMethodLocale,
                            PrefService::UNSYNCABLE_PREF);
  prefs->RegisterStringPref(prefs::kLanguagePreloadEngines,
                            hardware_keyboard_id,
                            PrefService::UNSYNCABLE_PREF);
  prefs->RegisterStringPref(prefs::kLanguageFilteredExtensionImes,
                            "",
                            PrefService::UNSYNCABLE_PREF);
  for (size_t i = 0; i < language_prefs::kNumChewingBooleanPrefs; ++i) {
    prefs->RegisterBooleanPref(
        language_prefs::kChewingBooleanPrefs[i].pref_name,
        language_prefs::kChewingBooleanPrefs[i].default_pref_value,
        language_prefs::kChewingBooleanPrefs[i].sync_status);
  }
  for (size_t i = 0; i < language_prefs::kNumChewingMultipleChoicePrefs; ++i) {
    prefs->RegisterStringPref(
        language_prefs::kChewingMultipleChoicePrefs[i].pref_name,
        language_prefs::kChewingMultipleChoicePrefs[i].default_pref_value,
        language_prefs::kChewingMultipleChoicePrefs[i].sync_status);
  }
  prefs->RegisterIntegerPref(
      language_prefs::kChewingHsuSelKeyType.pref_name,
      language_prefs::kChewingHsuSelKeyType.default_pref_value,
      language_prefs::kChewingHsuSelKeyType.sync_status);

  for (size_t i = 0; i < language_prefs::kNumChewingIntegerPrefs; ++i) {
    prefs->RegisterIntegerPref(
        language_prefs::kChewingIntegerPrefs[i].pref_name,
        language_prefs::kChewingIntegerPrefs[i].default_pref_value,
        language_prefs::kChewingIntegerPrefs[i].sync_status);
  }
  prefs->RegisterStringPref(
      prefs::kLanguageHangulKeyboard,
      language_prefs::kHangulKeyboardNameIDPairs[0].keyboard_id,
      PrefService::SYNCABLE_PREF);
  prefs->RegisterStringPref(prefs::kLanguageHangulHanjaBindingKeys,
                            language_prefs::kHangulHanjaBindingKeys,
                            // Don't sync the pref as it's not user-configurable
                            PrefService::UNSYNCABLE_PREF);
  for (size_t i = 0; i < language_prefs::kNumPinyinBooleanPrefs; ++i) {
    prefs->RegisterBooleanPref(
        language_prefs::kPinyinBooleanPrefs[i].pref_name,
        language_prefs::kPinyinBooleanPrefs[i].default_pref_value,
        language_prefs::kPinyinBooleanPrefs[i].sync_status);
  }
  for (size_t i = 0; i < language_prefs::kNumPinyinIntegerPrefs; ++i) {
    prefs->RegisterIntegerPref(
        language_prefs::kPinyinIntegerPrefs[i].pref_name,
        language_prefs::kPinyinIntegerPrefs[i].default_pref_value,
        language_prefs::kPinyinIntegerPrefs[i].sync_status);
  }
  prefs->RegisterIntegerPref(
      language_prefs::kPinyinDoublePinyinSchema.pref_name,
      language_prefs::kPinyinDoublePinyinSchema.default_pref_value,
      PrefService::UNSYNCABLE_PREF);

  for (size_t i = 0; i < language_prefs::kNumMozcBooleanPrefs; ++i) {
    prefs->RegisterBooleanPref(
        language_prefs::kMozcBooleanPrefs[i].pref_name,
        language_prefs::kMozcBooleanPrefs[i].default_pref_value,
        language_prefs::kMozcBooleanPrefs[i].sync_status);
  }
  for (size_t i = 0; i < language_prefs::kNumMozcMultipleChoicePrefs; ++i) {
    prefs->RegisterStringPref(
        language_prefs::kMozcMultipleChoicePrefs[i].pref_name,
        language_prefs::kMozcMultipleChoicePrefs[i].default_pref_value,
        language_prefs::kMozcMultipleChoicePrefs[i].sync_status);
  }
  for (size_t i = 0; i < language_prefs::kNumMozcIntegerPrefs; ++i) {
    prefs->RegisterIntegerPref(
        language_prefs::kMozcIntegerPrefs[i].pref_name,
        language_prefs::kMozcIntegerPrefs[i].default_pref_value,
        language_prefs::kMozcIntegerPrefs[i].sync_status);
  }
  prefs->RegisterIntegerPref(prefs::kLanguageRemapSearchKeyTo,
                             input_method::kSearchKey,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterIntegerPref(prefs::kLanguageRemapControlKeyTo,
                             input_method::kControlKey,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterIntegerPref(prefs::kLanguageRemapAltKeyTo,
                             input_method::kAltKey,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterIntegerPref(prefs::kLanguageRemapCapsLockKeyTo,
                             input_method::kCapsLockKey,
                             PrefService::SYNCABLE_PREF);
  // We don't sync the following keyboard prefs since they are not user-
  // configurable.
  prefs->RegisterBooleanPref(prefs::kLanguageXkbAutoRepeatEnabled,
                             true,
                             PrefService::UNSYNCABLE_PREF);
  prefs->RegisterIntegerPref(prefs::kLanguageXkbAutoRepeatDelay,
                             language_prefs::kXkbAutoRepeatDelayInMs,
                             PrefService::UNSYNCABLE_PREF);
  prefs->RegisterIntegerPref(prefs::kLanguageXkbAutoRepeatInterval,
                             language_prefs::kXkbAutoRepeatIntervalInMs,
                             PrefService::UNSYNCABLE_PREF);

  // Screen lock default to off.
  prefs->RegisterBooleanPref(prefs::kEnableScreenLock,
                             false,
                             PrefService::SYNCABLE_PREF);

  // Mobile plan notifications default to on.
  prefs->RegisterBooleanPref(prefs::kShowPlanNotifications,
                             true,
                             PrefService::SYNCABLE_PREF);

  // 3G first-time usage promo will be shown at least once.
  prefs->RegisterBooleanPref(prefs::kShow3gPromoNotification,
                             true,
                             PrefService::UNSYNCABLE_PREF);

  // Initially all existing users would see "What's new"
  // for current version after update.
  prefs->RegisterStringPref(prefs::kChromeOSReleaseNotesVersion,
                            "0.0.0.0",
                            PrefService::SYNCABLE_PREF);

  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kForceOAuth1)) {
    // Legacy OAuth1 all access token and secret pair.
    prefs->RegisterStringPref(prefs::kOAuth1Token,
                              "",
                              PrefService::UNSYNCABLE_PREF);
    prefs->RegisterStringPref(prefs::kOAuth1Secret,
                              "",
                              PrefService::UNSYNCABLE_PREF);
  }

  // TODO(wad): Once UI is connected, a final default can be set. At that point
  // change this pref from UNSYNCABLE to SYNCABLE.
  prefs->RegisterBooleanPref(prefs::kEnableCrosDRM,
                             true,
                             PrefService::UNSYNCABLE_PREF);

  prefs->RegisterBooleanPref(prefs::kExternalStorageDisabled,
                             false,
                             PrefService::UNSYNCABLE_PREF);
}

void Preferences::InitUserPrefs(PrefService* prefs) {
  prefs_ = prefs;

  BooleanPrefMember::NamedChangeCallback callback =
      base::Bind(&Preferences::OnPreferenceChanged, base::Unretained(this));

  tap_to_click_enabled_.Init(prefs::kTapToClickEnabled, prefs, callback);
  tap_dragging_enabled_.Init(prefs::kTapDraggingEnabled, prefs, callback);
  three_finger_click_enabled_.Init(prefs::kEnableTouchpadThreeFingerClick,
      prefs, callback);
  three_finger_swipe_enabled_.Init(prefs::kEnableTouchpadThreeFingerSwipe,
      prefs, callback);
  natural_scroll_.Init(prefs::kNaturalScroll, prefs, callback);
  accessibility_enabled_.Init(prefs::kSpokenFeedbackEnabled, prefs, callback);
  screen_magnifier_enabled_.Init(prefs::kScreenMagnifierEnabled,
                                 prefs, callback);
  screen_magnifier_scale_.Init(prefs::kScreenMagnifierScale, prefs, callback);
  mouse_sensitivity_.Init(prefs::kMouseSensitivity, prefs, callback);
  touchpad_sensitivity_.Init(prefs::kTouchpadSensitivity, prefs, callback);
  use_24hour_clock_.Init(prefs::kUse24HourClock, prefs, callback);
  disable_drive_.Init(prefs::kDisableDrive, prefs, callback);
  disable_drive_over_cellular_.Init(prefs::kDisableDriveOverCellular,
                                   prefs, callback);
  disable_drive_hosted_files_.Init(prefs::kDisableDriveHostedFiles,
                                   prefs, callback);
  download_default_directory_.Init(prefs::kDownloadDefaultDirectory,
                                   prefs, callback);
  primary_mouse_button_right_.Init(prefs::kPrimaryMouseButtonRight,
                                   prefs, callback);
  preferred_languages_.Init(prefs::kLanguagePreferredLanguages,
                            prefs, callback);
  preload_engines_.Init(prefs::kLanguagePreloadEngines, prefs, callback);
  filtered_extension_imes_.Init(prefs::kLanguageFilteredExtensionImes,
                                prefs, callback);
  current_input_method_.Init(prefs::kLanguageCurrentInputMethod,
                             prefs, callback);
  previous_input_method_.Init(prefs::kLanguagePreviousInputMethod,
                              prefs, callback);

  for (size_t i = 0; i < language_prefs::kNumChewingBooleanPrefs; ++i) {
    chewing_boolean_prefs_[i].Init(
        language_prefs::kChewingBooleanPrefs[i].pref_name, prefs, callback);
  }
  for (size_t i = 0; i < language_prefs::kNumChewingMultipleChoicePrefs; ++i) {
    chewing_multiple_choice_prefs_[i].Init(
        language_prefs::kChewingMultipleChoicePrefs[i].pref_name,
        prefs, callback);
  }
  chewing_hsu_sel_key_type_.Init(
      language_prefs::kChewingHsuSelKeyType.pref_name, prefs, callback);
  for (size_t i = 0; i < language_prefs::kNumChewingIntegerPrefs; ++i) {
    chewing_integer_prefs_[i].Init(
        language_prefs::kChewingIntegerPrefs[i].pref_name, prefs, callback);
  }
  hangul_keyboard_.Init(prefs::kLanguageHangulKeyboard, prefs, callback);
  hangul_hanja_binding_keys_.Init(
      prefs::kLanguageHangulHanjaBindingKeys, prefs, callback);
  for (size_t i = 0; i < language_prefs::kNumPinyinBooleanPrefs; ++i) {
    pinyin_boolean_prefs_[i].Init(
        language_prefs::kPinyinBooleanPrefs[i].pref_name, prefs, callback);
  }
  for (size_t i = 0; i < language_prefs::kNumPinyinIntegerPrefs; ++i) {
    pinyin_int_prefs_[i].Init(
        language_prefs::kPinyinIntegerPrefs[i].pref_name, prefs, callback);
  }
  pinyin_double_pinyin_schema_.Init(
      language_prefs::kPinyinDoublePinyinSchema.pref_name, prefs, callback);
  for (size_t i = 0; i < language_prefs::kNumMozcBooleanPrefs; ++i) {
    mozc_boolean_prefs_[i].Init(
        language_prefs::kMozcBooleanPrefs[i].pref_name, prefs, callback);
  }
  for (size_t i = 0; i < language_prefs::kNumMozcMultipleChoicePrefs; ++i) {
    mozc_multiple_choice_prefs_[i].Init(
        language_prefs::kMozcMultipleChoicePrefs[i].pref_name, prefs, callback);
  }
  for (size_t i = 0; i < language_prefs::kNumMozcIntegerPrefs; ++i) {
    mozc_integer_prefs_[i].Init(
        language_prefs::kMozcIntegerPrefs[i].pref_name, prefs, callback);
  }
  xkb_auto_repeat_enabled_.Init(
      prefs::kLanguageXkbAutoRepeatEnabled, prefs, callback);
  xkb_auto_repeat_delay_pref_.Init(
      prefs::kLanguageXkbAutoRepeatDelay, prefs, callback);
  xkb_auto_repeat_interval_pref_.Init(
      prefs::kLanguageXkbAutoRepeatInterval, prefs, callback);

  enable_screen_lock_.Init(prefs::kEnableScreenLock, prefs, callback);

  enable_drm_.Init(prefs::kEnableCrosDRM, prefs, callback);
}

void Preferences::Init(PrefService* prefs) {
  InitUserPrefs(prefs);

  // Initialize preferences to currently saved state.
  NotifyPrefChanged(NULL);

  // If a guest is logged in, initialize the prefs as if this is the first
  // login.
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kGuestSession)) {
    LoginUtils::Get()->SetFirstLoginPrefs(prefs);
  }
}

void Preferences::InitUserPrefsForTesting(PrefService* prefs) {
  InitUserPrefs(prefs);
}

void Preferences::SetInputMethodListForTesting() {
  SetInputMethodList();
}

void Preferences::OnPreferenceChanged(const std::string& pref_name) {
  NotifyPrefChanged(&pref_name);
}

void Preferences::NotifyPrefChanged(const std::string* pref_name) {
  if (!pref_name || *pref_name == prefs::kTapToClickEnabled) {
    const bool enabled = tap_to_click_enabled_.GetValue();
    system::touchpad_settings::SetTapToClick(enabled);
    if (pref_name)
      UMA_HISTOGRAM_BOOLEAN("Touchpad.TapToClick.Changed", enabled);
    else
      UMA_HISTOGRAM_BOOLEAN("Touchpad.TapToClick.Started", enabled);

    // Save owner preference in local state to use on login screen.
    if (chromeos::UserManager::Get()->IsCurrentUserOwner()) {
      PrefService* prefs = g_browser_process->local_state();
      if (prefs->GetBoolean(prefs::kOwnerTapToClickEnabled) != enabled)
        prefs->SetBoolean(prefs::kOwnerTapToClickEnabled, enabled);
    }
  }
  if (!pref_name || *pref_name == prefs::kTapDraggingEnabled) {
    const bool enabled = tap_dragging_enabled_.GetValue();
    system::touchpad_settings::SetTapDragging(enabled);
    if (pref_name)
      UMA_HISTOGRAM_BOOLEAN("Touchpad.TapDragging.Changed", enabled);
    else
      UMA_HISTOGRAM_BOOLEAN("Touchpad.TapDragging.Started", enabled);
  }
  if (!pref_name || *pref_name == prefs::kEnableTouchpadThreeFingerClick) {
    const bool enabled = three_finger_click_enabled_.GetValue();
    system::touchpad_settings::SetThreeFingerClick(enabled);
    if (pref_name)
      UMA_HISTOGRAM_BOOLEAN("Touchpad.ThreeFingerClick.Changed", enabled);
    else
      UMA_HISTOGRAM_BOOLEAN("Touchpad.ThreeFingerClick.Started", enabled);
  }
  if (!pref_name || *pref_name == prefs::kEnableTouchpadThreeFingerSwipe) {
    const bool enabled = three_finger_swipe_enabled_.GetValue();
    system::touchpad_settings::SetThreeFingerSwipe(enabled);
    if (pref_name)
      UMA_HISTOGRAM_BOOLEAN("Touchpad.ThreeFingerSwipe.Changed", enabled);
    else
      UMA_HISTOGRAM_BOOLEAN("Touchpad.ThreeFingerSwipe.Started", enabled);
  }
  if (!pref_name || *pref_name == prefs::kNaturalScroll) {
    // Force natural scroll to on if kNaturalScrollDefault is specified on the
    // cmd line.
    if (CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kNaturalScrollDefault) &&
        !pref_name &&
        !prefs_->GetUserPrefValue(prefs::kNaturalScroll)) {
      natural_scroll_.SetValue(true);
      DVLOG(1) << "Natural scroll forced to true";
      UMA_HISTOGRAM_BOOLEAN("Touchpad.NaturalScroll.Forced", true);
    }

    const bool enabled = natural_scroll_.GetValue();
    DVLOG(1) << "Natural scroll set to " << enabled;
    ui::SetNaturalScroll(enabled);
    if (pref_name)
      UMA_HISTOGRAM_BOOLEAN("Touchpad.NaturalScroll.Changed", enabled);
    else
      UMA_HISTOGRAM_BOOLEAN("Touchpad.NaturalScroll.Started", enabled);
  }
  if (!pref_name || *pref_name == prefs::kMouseSensitivity) {
    const int sensitivity = mouse_sensitivity_.GetValue();
    system::mouse_settings::SetSensitivity(sensitivity);
    if (pref_name) {
      UMA_HISTOGRAM_CUSTOM_COUNTS(
          "Mouse.Sensitivity.Changed", sensitivity, 1, 5, 5);
    } else {
      UMA_HISTOGRAM_CUSTOM_COUNTS(
          "Mouse.Sensitivity.Started", sensitivity, 1, 5, 5);
    }
  }
  if (!pref_name || *pref_name == prefs::kTouchpadSensitivity) {
    const int sensitivity = touchpad_sensitivity_.GetValue();
    system::touchpad_settings::SetSensitivity(sensitivity);
    if (pref_name) {
      UMA_HISTOGRAM_CUSTOM_COUNTS(
          "Touchpad.Sensitivity.Changed", sensitivity, 1, 5, 5);
    } else {
      UMA_HISTOGRAM_CUSTOM_COUNTS(
          "Touchpad.Sensitivity.Started", sensitivity, 1, 5, 5);
    }
  }
  if (!pref_name || *pref_name == prefs::kPrimaryMouseButtonRight) {
    const bool right = primary_mouse_button_right_.GetValue();
    system::mouse_settings::SetPrimaryButtonRight(right);
    if (pref_name)
      UMA_HISTOGRAM_BOOLEAN("Mouse.PrimaryButtonRight.Changed", right);
    else
      UMA_HISTOGRAM_BOOLEAN("Mouse.PrimaryButtonRight.Started", right);

    // Save owner preference in local state to use on login screen.
    if (chromeos::UserManager::Get()->IsCurrentUserOwner()) {
      PrefService* prefs = g_browser_process->local_state();
      if (prefs->GetBoolean(prefs::kOwnerPrimaryMouseButtonRight) != right)
        prefs->SetBoolean(prefs::kOwnerPrimaryMouseButtonRight, right);
    }
  }
  if (!pref_name || *pref_name == prefs::kDownloadDefaultDirectory) {
    const bool default_download_to_drive = drive::util::IsUnderDriveMountPoint(
        download_default_directory_.GetValue());
    if (pref_name)
      UMA_HISTOGRAM_BOOLEAN(
          "FileBrowser.DownloadDestination.IsGoogleDrive.Changed",
          default_download_to_drive);
    else
      UMA_HISTOGRAM_BOOLEAN(
          "FileBrowser.DownloadDestination.IsGoogleDrive.Started",
          default_download_to_drive);
  }

  if (!pref_name || *pref_name == prefs::kLanguagePreferredLanguages) {
    // Unlike kLanguagePreloadEngines and some other input method
    // preferencs, we don't need to send this to ibus-daemon.
  }

  if (!pref_name || *pref_name == prefs::kLanguageXkbAutoRepeatEnabled) {
    const bool enabled = xkb_auto_repeat_enabled_.GetValue();
    input_method::XKeyboard::SetAutoRepeatEnabled(enabled);
  }
  if (!pref_name || ((*pref_name == prefs::kLanguageXkbAutoRepeatDelay) ||
                     (*pref_name == prefs::kLanguageXkbAutoRepeatInterval))) {
    UpdateAutoRepeatRate();
  }

  if (!pref_name) {
    SetInputMethodList();
  } else if (*pref_name == prefs::kLanguagePreloadEngines) {
    SetLanguageConfigStringListAsCSV(language_prefs::kGeneralSectionName,
                                     language_prefs::kPreloadEnginesConfigName,
                                     preload_engines_.GetValue());
  }

  if (!pref_name || *pref_name == prefs::kLanguageFilteredExtensionImes) {
    std::string value(filtered_extension_imes_.GetValue());

    std::vector<std::string> split_values;
    if (!value.empty())
      base::SplitString(value, ',', &split_values);

    input_method_manager_->SetFilteredExtensionImes(&split_values);
  }

  // Do not check |*pref_name| of the prefs for remembering current/previous
  // input methods here. We're only interested in initial values of the prefs.

  for (size_t i = 0; i < language_prefs::kNumChewingBooleanPrefs; ++i) {
    if (!pref_name ||
        *pref_name == language_prefs::kChewingBooleanPrefs[i].pref_name) {
      SetLanguageConfigBoolean(
          language_prefs::kChewingSectionName,
          language_prefs::kChewingBooleanPrefs[i].ibus_config_name,
          chewing_boolean_prefs_[i].GetValue());
    }
  }
  for (size_t i = 0; i < language_prefs::kNumChewingMultipleChoicePrefs; ++i) {
    if (!pref_name ||
        *pref_name ==
        language_prefs::kChewingMultipleChoicePrefs[i].pref_name) {
      SetLanguageConfigString(
          language_prefs::kChewingSectionName,
          language_prefs::kChewingMultipleChoicePrefs[i].ibus_config_name,
          chewing_multiple_choice_prefs_[i].GetValue());
    }
  }
  if (!pref_name ||
      *pref_name == language_prefs::kChewingHsuSelKeyType.pref_name) {
    SetLanguageConfigInteger(
        language_prefs::kChewingSectionName,
        language_prefs::kChewingHsuSelKeyType.ibus_config_name,
        chewing_hsu_sel_key_type_.GetValue());
  }
  for (size_t i = 0; i < language_prefs::kNumChewingIntegerPrefs; ++i) {
    if (!pref_name ||
        *pref_name == language_prefs::kChewingIntegerPrefs[i].pref_name) {
      SetLanguageConfigInteger(
          language_prefs::kChewingSectionName,
          language_prefs::kChewingIntegerPrefs[i].ibus_config_name,
          chewing_integer_prefs_[i].GetValue());
    }
  }
  if (!pref_name ||
      *pref_name == prefs::kLanguageHangulKeyboard) {
    SetLanguageConfigString(language_prefs::kHangulSectionName,
                            language_prefs::kHangulKeyboardConfigName,
                            hangul_keyboard_.GetValue());
  }
  if (!pref_name || *pref_name == prefs::kLanguageHangulHanjaBindingKeys) {
    SetLanguageConfigString(language_prefs::kHangulSectionName,
                            language_prefs::kHangulHanjaBindingKeysConfigName,
                            hangul_hanja_binding_keys_.GetValue());
  }
  for (size_t i = 0; i < language_prefs::kNumPinyinBooleanPrefs; ++i) {
    if (!pref_name ||
        *pref_name == language_prefs::kPinyinBooleanPrefs[i].pref_name) {
      SetLanguageConfigBoolean(
          language_prefs::kPinyinSectionName,
          language_prefs::kPinyinBooleanPrefs[i].ibus_config_name,
          pinyin_boolean_prefs_[i].GetValue());
    }
  }
  for (size_t i = 0; i < language_prefs::kNumPinyinIntegerPrefs; ++i) {
    if (!pref_name ||
        *pref_name == language_prefs::kPinyinIntegerPrefs[i].pref_name) {
      SetLanguageConfigInteger(
          language_prefs::kPinyinSectionName,
          language_prefs::kPinyinIntegerPrefs[i].ibus_config_name,
          pinyin_int_prefs_[i].GetValue());
    }
  }
  if (!pref_name ||
      *pref_name == language_prefs::kPinyinDoublePinyinSchema.pref_name) {
    SetLanguageConfigInteger(
        language_prefs::kPinyinSectionName,
        language_prefs::kPinyinDoublePinyinSchema.ibus_config_name,
        pinyin_double_pinyin_schema_.GetValue());
  }
  for (size_t i = 0; i < language_prefs::kNumMozcBooleanPrefs; ++i) {
    if (!pref_name ||
        *pref_name == language_prefs::kMozcBooleanPrefs[i].pref_name) {
      SetLanguageConfigBoolean(
          language_prefs::kMozcSectionName,
          language_prefs::kMozcBooleanPrefs[i].ibus_config_name,
          mozc_boolean_prefs_[i].GetValue());
    }
  }
  for (size_t i = 0; i < language_prefs::kNumMozcMultipleChoicePrefs; ++i) {
    if (!pref_name ||
        *pref_name == language_prefs::kMozcMultipleChoicePrefs[i].pref_name) {
      SetLanguageConfigString(
          language_prefs::kMozcSectionName,
          language_prefs::kMozcMultipleChoicePrefs[i].ibus_config_name,
          mozc_multiple_choice_prefs_[i].GetValue());
    }
  }
  for (size_t i = 0; i < language_prefs::kNumMozcIntegerPrefs; ++i) {
    if (!pref_name ||
        *pref_name == language_prefs::kMozcIntegerPrefs[i].pref_name) {
      SetLanguageConfigInteger(
          language_prefs::kMozcSectionName,
          language_prefs::kMozcIntegerPrefs[i].ibus_config_name,
          mozc_integer_prefs_[i].GetValue());
    }
  }

  // Init or update power manager config.
  if (!pref_name || *pref_name == prefs::kEnableScreenLock) {
    system::power_manager_settings::EnableScreenLock(
        enable_screen_lock_.GetValue());
  }

  // Init or update protected content (DRM) support.
  if (!pref_name || *pref_name == prefs::kEnableCrosDRM) {
    system::ToggleDrm(enable_drm_.GetValue());
  }

  // Change the download directory to the default value if a Drive directory is
  // selected and Drive is disabled.
  if (!pref_name || *pref_name == prefs::kDisableDrive) {
    if (disable_drive_.GetValue()) {
      if (drive::util::IsUnderDriveMountPoint(
          download_default_directory_.GetValue())) {
        prefs_->SetFilePath(prefs::kDownloadDefaultDirectory,
                            download_util::GetDefaultDownloadDirectory());
      }
    }
  }
}

void Preferences::SetLanguageConfigBoolean(const char* section,
                                           const char* name,
                                           bool value) {
  input_method::InputMethodConfigValue config;
  config.type = input_method::InputMethodConfigValue::kValueTypeBool;
  config.bool_value = value;
  input_method_manager_->SetInputMethodConfig(section, name, config);
}

void Preferences::SetLanguageConfigInteger(const char* section,
                                           const char* name,
                                           int value) {
  input_method::InputMethodConfigValue config;
  config.type = input_method::InputMethodConfigValue::kValueTypeInt;
  config.int_value = value;
  input_method_manager_->SetInputMethodConfig(section, name, config);
}

void Preferences::SetLanguageConfigString(const char* section,
                                          const char* name,
                                          const std::string& value) {
  input_method::InputMethodConfigValue config;
  config.type = input_method::InputMethodConfigValue::kValueTypeString;
  config.string_value = value;
  input_method_manager_->SetInputMethodConfig(section, name, config);
}

void Preferences::SetLanguageConfigStringList(
    const char* section,
    const char* name,
    const std::vector<std::string>& values) {
  input_method::InputMethodConfigValue config;
  config.type = input_method::InputMethodConfigValue::kValueTypeStringList;
  for (size_t i = 0; i < values.size(); ++i)
    config.string_list_value.push_back(values[i]);

  input_method_manager_->SetInputMethodConfig(section, name, config);
}

void Preferences::SetLanguageConfigStringListAsCSV(const char* section,
                                                   const char* name,
                                                   const std::string& value) {
  VLOG(1) << "Setting " << name << " to '" << value << "'";

  std::vector<std::string> split_values;
  if (!value.empty())
    base::SplitString(value, ',', &split_values);

  if (section == std::string(language_prefs::kGeneralSectionName) &&
      name == std::string(language_prefs::kPreloadEnginesConfigName)) {
    input_method_manager_->EnableInputMethods(split_values);
    return;
  }

  // We should call the cros API even when |value| is empty, to disable default
  // config.
  SetLanguageConfigStringList(section, name, split_values);
}

void Preferences::SetInputMethodList() {
  // When |preload_engines_| are set, InputMethodManager::ChangeInputMethod()
  // might be called to change the current input method to the first one in the
  // |preload_engines_| list. This also updates previous/current input method
  // prefs. That's why GetValue() calls are placed before the
  // SetLanguageConfigStringListAsCSV() call below.
  const std::string previous_input_method_id =
      previous_input_method_.GetValue();
  const std::string current_input_method_id = current_input_method_.GetValue();
  SetLanguageConfigStringListAsCSV(language_prefs::kGeneralSectionName,
                                   language_prefs::kPreloadEnginesConfigName,
                                   preload_engines_.GetValue());

  // ChangeInputMethod() has to be called AFTER the value of |preload_engines_|
  // is sent to the InputMethodManager. Otherwise, the ChangeInputMethod request
  // might be ignored as an invalid input method ID. The ChangeInputMethod()
  // calls are also necessary to restore the previous/current input method prefs
  // which could have been modified by the SetLanguageConfigStringListAsCSV call
  // above to the original state.
  if (!previous_input_method_id.empty())
    input_method_manager_->ChangeInputMethod(previous_input_method_id);
  if (!current_input_method_id.empty())
    input_method_manager_->ChangeInputMethod(current_input_method_id);
}

void Preferences::UpdateAutoRepeatRate() {
  // Avoid setting repeat rate on desktop dev environment.
  if (!base::chromeos::IsRunningOnChromeOS())
    return;

  input_method::AutoRepeatRate rate;
  rate.initial_delay_in_ms = xkb_auto_repeat_delay_pref_.GetValue();
  rate.repeat_interval_in_ms = xkb_auto_repeat_interval_pref_.GetValue();
  DCHECK(rate.initial_delay_in_ms > 0);
  DCHECK(rate.repeat_interval_in_ms > 0);
  input_method::XKeyboard::SetAutoRepeatRate(rate);
}

}  // namespace chromeos
