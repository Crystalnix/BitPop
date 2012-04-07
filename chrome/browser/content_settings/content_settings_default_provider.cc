// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/content_settings/content_settings_default_provider.h"

#include <string>
#include <vector>

#include "base/auto_reset.h"
#include "base/basictypes.h"
#include "base/command_line.h"
#include "base/metrics/histogram.h"
#include "chrome/browser/content_settings/content_settings_rule.h"
#include "chrome/browser/content_settings/content_settings_utils.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/content_settings.h"
#include "chrome/common/content_settings_pattern.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/user_metrics.h"
#include "googleurl/src/gurl.h"

using content::BrowserThread;
using content::UserMetricsAction;

namespace {

// The default setting for each content type.
const ContentSetting kDefaultSettings[] = {
  CONTENT_SETTING_ALLOW,    // CONTENT_SETTINGS_TYPE_COOKIES
  CONTENT_SETTING_ALLOW,    // CONTENT_SETTINGS_TYPE_IMAGES
  CONTENT_SETTING_ALLOW,    // CONTENT_SETTINGS_TYPE_JAVASCRIPT
  CONTENT_SETTING_ALLOW,    // CONTENT_SETTINGS_TYPE_PLUGINS
  CONTENT_SETTING_BLOCK,    // CONTENT_SETTINGS_TYPE_POPUPS
  CONTENT_SETTING_ASK,      // CONTENT_SETTINGS_TYPE_GEOLOCATION
  CONTENT_SETTING_ASK,      // CONTENT_SETTINGS_TYPE_NOTIFICATIONS
  CONTENT_SETTING_ASK,      // CONTENT_SETTINGS_TYPE_INTENTS
  CONTENT_SETTING_DEFAULT,  // CONTENT_SETTINGS_TYPE_AUTO_SELECT_CERTIFICATE
  CONTENT_SETTING_ASK,      // CONTENT_SETTINGS_TYPE_FULLSCREEN
  CONTENT_SETTING_ASK,      // CONTENT_SETTINGS_TYPE_MOUSELOCK
};
COMPILE_ASSERT(arraysize(kDefaultSettings) == CONTENT_SETTINGS_NUM_TYPES,
               default_settings_incorrect_size);

}  // namespace

namespace content_settings {

namespace {

class DefaultRuleIterator : public RuleIterator {
 public:
  explicit DefaultRuleIterator(const base::Value* value) {
    if (value)
      value_.reset(value->DeepCopy());
  }

  bool HasNext() const {
    return value_.get() != NULL;
  }

  Rule Next() {
    DCHECK(value_.get());
    return Rule(ContentSettingsPattern::Wildcard(),
                ContentSettingsPattern::Wildcard(),
                value_.release());
  }

 private:
  scoped_ptr<base::Value> value_;
};

}  // namespace

// static
void DefaultProvider::RegisterUserPrefs(PrefService* prefs) {
  // The registration of the preference prefs::kDefaultContentSettings should
  // also include the default values for default content settings. This allows
  // functional tests to get default content settings by reading the preference
  // prefs::kDefaultContentSettings via pyauto.
  // TODO(markusheintz): Write pyauto hooks for the content settings map as
  // content settings should be read from the host content settings map.
  DictionaryValue* default_content_settings = new DictionaryValue();
  prefs->RegisterDictionaryPref(prefs::kDefaultContentSettings,
                                default_content_settings,
                                PrefService::SYNCABLE_PREF);

  // Obsolete prefs, for migrations:
  prefs->RegisterIntegerPref(
      prefs::kDesktopNotificationDefaultContentSetting,
      kDefaultSettings[CONTENT_SETTINGS_TYPE_NOTIFICATIONS],
      PrefService::SYNCABLE_PREF);
  prefs->RegisterIntegerPref(
      prefs::kGeolocationDefaultContentSetting,
      kDefaultSettings[CONTENT_SETTINGS_TYPE_GEOLOCATION],
      PrefService::UNSYNCABLE_PREF);
}

DefaultProvider::DefaultProvider(PrefService* prefs, bool incognito)
    : prefs_(prefs),
      is_incognito_(incognito),
      updating_preferences_(false) {
  DCHECK(prefs_);
  MigrateObsoleteNotificationPref();
  MigrateObsoleteGeolocationPref();

  // Read global defaults.
  ReadDefaultSettings(true);

  UMA_HISTOGRAM_ENUMERATION(
      "ContentSettings.DefaultCookiesSetting",
      ValueToContentSetting(
          default_settings_[CONTENT_SETTINGS_TYPE_COOKIES].get()),
      CONTENT_SETTING_NUM_SETTINGS);
  UMA_HISTOGRAM_ENUMERATION(
      "ContentSettings.DefaultImagesSetting",
      ValueToContentSetting(
          default_settings_[CONTENT_SETTINGS_TYPE_IMAGES].get()),
      CONTENT_SETTING_NUM_SETTINGS);
  UMA_HISTOGRAM_ENUMERATION(
      "ContentSettings.DefaultJavaScriptSetting",
      ValueToContentSetting(
          default_settings_[CONTENT_SETTINGS_TYPE_JAVASCRIPT].get()),
      CONTENT_SETTING_NUM_SETTINGS);
  UMA_HISTOGRAM_ENUMERATION(
      "ContentSettings.DefaultPluginsSetting",
      ValueToContentSetting(
          default_settings_[CONTENT_SETTINGS_TYPE_PLUGINS].get()),
      CONTENT_SETTING_NUM_SETTINGS);
  UMA_HISTOGRAM_ENUMERATION(
      "ContentSettings.DefaultPopupsSetting",
      ValueToContentSetting(
          default_settings_[CONTENT_SETTINGS_TYPE_POPUPS].get()),
      CONTENT_SETTING_NUM_SETTINGS);
  UMA_HISTOGRAM_ENUMERATION(
      "ContentSettings.DefaultLocationSetting",
      ValueToContentSetting(
          default_settings_[CONTENT_SETTINGS_TYPE_GEOLOCATION].get()),
      CONTENT_SETTING_NUM_SETTINGS);
  UMA_HISTOGRAM_ENUMERATION(
      "ContentSettings.DefaultNotificationsSetting",
      ValueToContentSetting(
          default_settings_[CONTENT_SETTINGS_TYPE_NOTIFICATIONS].get()),
      CONTENT_SETTING_NUM_SETTINGS);
  UMA_HISTOGRAM_ENUMERATION(
      "ContentSettings.DefaultHandlersSetting",
      ValueToContentSetting(
          default_settings_[CONTENT_SETTINGS_TYPE_INTENTS].get()),
      CONTENT_SETTING_NUM_SETTINGS);
  UMA_HISTOGRAM_ENUMERATION(
      "ContentSettings.DefaultMouseCursorSetting",
      ValueToContentSetting(
          default_settings_[CONTENT_SETTINGS_TYPE_MOUSELOCK].get()),
      CONTENT_SETTING_NUM_SETTINGS);

  pref_change_registrar_.Init(prefs_);
  pref_change_registrar_.Add(prefs::kDefaultContentSettings, this);
  pref_change_registrar_.Add(prefs::kGeolocationDefaultContentSetting, this);
}

DefaultProvider::~DefaultProvider() {
}

bool DefaultProvider::SetWebsiteSetting(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsType content_type,
    const ResourceIdentifier& resource_identifier,
    Value* in_value) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(prefs_);

  // Ignore non default settings
  if (primary_pattern != ContentSettingsPattern::Wildcard() ||
      secondary_pattern != ContentSettingsPattern::Wildcard()) {
    return false;
  }

  // The default settings may not be directly modified for OTR sessions.
  // Instead, they are synced to the main profile's setting.
  if (is_incognito_)
    return false;

  // Put |in_value| in a scoped pointer to ensure that it gets cleaned up
  // properly if we don't pass on the ownership.
  scoped_ptr<base::Value> value(in_value);
  {
    AutoReset<bool> auto_reset(&updating_preferences_, true);
    // Keep the obsolete pref in sync as long as backwards compatibility is
    // required. This is required to keep sync working correctly.
    if (content_type == CONTENT_SETTINGS_TYPE_GEOLOCATION) {
      if (value.get()) {
        prefs_->Set(prefs::kGeolocationDefaultContentSetting, *value);
      } else {
        prefs_->ClearPref(prefs::kGeolocationDefaultContentSetting);
      }
    }

    // |DefaultProvider| should not send any notifications when holding
    // |lock_|. |DictionaryPrefUpdate| destructor and
    // |PrefService::SetInteger()| send out notifications. As a response, the
    // upper layers may call |GetAllContentSettingRules| which acquires |lock_|
    // again.
    DictionaryPrefUpdate update(prefs_, prefs::kDefaultContentSettings);
    DictionaryValue* default_settings_dictionary = update.Get();
    base::AutoLock lock(lock_);
    if (value.get() == NULL ||
        ValueToContentSetting(value.get()) == kDefaultSettings[content_type]) {
      // If |value| is NULL we need to reset the default setting the the
      // hardcoded default.
      default_settings_[content_type].reset(
          Value::CreateIntegerValue(kDefaultSettings[content_type]));

      // Remove the corresponding pref entry since the hardcoded default value
      // is used.
      default_settings_dictionary->RemoveWithoutPathExpansion(
          GetTypeName(content_type), NULL);
    } else {
      default_settings_[content_type].reset(value->DeepCopy());
      // Transfer ownership of |value| to the |default_settings_dictionary|.
      default_settings_dictionary->SetWithoutPathExpansion(
          GetTypeName(content_type), value.release());
    }
  }

  NotifyObservers(ContentSettingsPattern(),
                  ContentSettingsPattern(),
                  content_type,
                  std::string());

  return true;
}

RuleIterator* DefaultProvider::GetRuleIterator(
    ContentSettingsType content_type,
    const ResourceIdentifier& resource_identifier,
    bool incognito) const {
  base::AutoLock lock(lock_);
  if (resource_identifier.empty()) {
    ValueMap::const_iterator it(default_settings_.find(content_type));
    if (it != default_settings_.end()) {
      return new DefaultRuleIterator(it->second.get());
    }
    NOTREACHED();
  }
  return new EmptyRuleIterator();
}

void DefaultProvider::ClearAllContentSettingsRules(
    ContentSettingsType content_type) {
  // TODO(markusheintz): This method is only called when the
  // |DesktopNotificationService| calls |ClearAllSettingsForType| method on the
  // |HostContentSettingsMap|. Don't implement this method yet, otherwise the
  // default notification settings will be cleared as well.
}

void DefaultProvider::ShutdownOnUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(prefs_);
  RemoveAllObservers();
  pref_change_registrar_.RemoveAll();
  prefs_ = NULL;
}

void DefaultProvider::Observe(int type,
                              const content::NotificationSource& source,
                              const content::NotificationDetails& details) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (type == chrome::NOTIFICATION_PREF_CHANGED) {
    DCHECK_EQ(prefs_, content::Source<PrefService>(source).ptr());
    if (updating_preferences_)
      return;

    std::string* name = content::Details<std::string>(details).ptr();
    if (*name == prefs::kDefaultContentSettings) {
      ReadDefaultSettings(true);
    } else if (*name == prefs::kGeolocationDefaultContentSetting) {
      MigrateObsoleteGeolocationPref();
      // Return and don't send a notifications. Migrating the obsolete
      // geolocation pref will change the prefs::kDefaultContentSettings and
      // cause the notification to be fired.
      return;
    } else {
      NOTREACHED() << "Unexpected preference observed";
      return;
    }

    NotifyObservers(ContentSettingsPattern(),
                    ContentSettingsPattern(),
                    CONTENT_SETTINGS_TYPE_DEFAULT,
                    std::string());
  } else {
    NOTREACHED() << "Unexpected notification";
  }
}

void DefaultProvider::ReadDefaultSettings(bool overwrite) {
  base::AutoLock lock(lock_);
  const DictionaryValue* default_settings_dictionary =
      prefs_->GetDictionary(prefs::kDefaultContentSettings);

  if (overwrite)
    default_settings_.clear();

  // Careful: The returned value could be NULL if the pref has never been set.
  if (default_settings_dictionary)
    GetSettingsFromDictionary(default_settings_dictionary);

  ForceDefaultsToBeExplicit();
}

void DefaultProvider::ForceDefaultsToBeExplicit() {
  for (int i = 0; i < CONTENT_SETTINGS_NUM_TYPES; ++i) {
    ContentSettingsType type = ContentSettingsType(i);
    if (!default_settings_[type].get() &&
        kDefaultSettings[i] != CONTENT_SETTING_DEFAULT) {
      default_settings_[type].reset(
          Value::CreateIntegerValue(kDefaultSettings[i]));
    }
  }
}

void DefaultProvider::GetSettingsFromDictionary(
    const DictionaryValue* dictionary) {
  for (DictionaryValue::key_iterator i(dictionary->begin_keys());
       i != dictionary->end_keys(); ++i) {
    const std::string& content_type(*i);
    for (size_t type = 0; type < CONTENT_SETTINGS_NUM_TYPES; ++type) {
      if (content_type == GetTypeName(ContentSettingsType(type))) {
        int int_value = CONTENT_SETTING_DEFAULT;
        bool found = dictionary->GetIntegerWithoutPathExpansion(content_type,
                                                                &int_value);
        DCHECK(found);
        default_settings_[ContentSettingsType(type)].reset(
            Value::CreateIntegerValue(int_value));
        break;
      }
    }
  }
  // Migrate obsolete cookie prompt mode.
  if (ValueToContentSetting(
          default_settings_[CONTENT_SETTINGS_TYPE_COOKIES].get()) ==
              CONTENT_SETTING_ASK) {
    default_settings_[CONTENT_SETTINGS_TYPE_COOKIES].reset(
        Value::CreateIntegerValue(CONTENT_SETTING_BLOCK));
  }
}

void DefaultProvider::MigrateObsoleteNotificationPref() {
  if (prefs_->HasPrefPath(prefs::kDesktopNotificationDefaultContentSetting)) {
    const base::Value* value = prefs_->FindPreference(
        prefs::kDesktopNotificationDefaultContentSetting)->GetValue();
    // Do not clear the old preference yet as long as we need to maintain
    // backward compatibility.
    SetWebsiteSetting(
        ContentSettingsPattern::Wildcard(),
        ContentSettingsPattern::Wildcard(),
        CONTENT_SETTINGS_TYPE_NOTIFICATIONS,
        std::string(),
        value->DeepCopy());
    prefs_->ClearPref(prefs::kDesktopNotificationDefaultContentSetting);
  }
}

void DefaultProvider::MigrateObsoleteGeolocationPref() {
  if (prefs_->HasPrefPath(prefs::kGeolocationDefaultContentSetting)) {
    const base::Value* value = prefs_->FindPreference(
        prefs::kGeolocationDefaultContentSetting)->GetValue();
    // Do not clear the old preference yet as long as we need to maintain
    // backward compatibility.
    SetWebsiteSetting(
        ContentSettingsPattern::Wildcard(),
        ContentSettingsPattern::Wildcard(),
        CONTENT_SETTINGS_TYPE_GEOLOCATION,
        std::string(),
        value->DeepCopy());
  }
}

}  // namespace content_settings
