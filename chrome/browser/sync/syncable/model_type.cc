// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/syncable/model_type.h"

#include "base/metrics/histogram.h"
#include "base/string_split.h"
#include "base/values.h"
#include "chrome/browser/sync/engine/syncproto.h"
#include "chrome/browser/sync/protocol/app_notification_specifics.pb.h"
#include "chrome/browser/sync/protocol/app_setting_specifics.pb.h"
#include "chrome/browser/sync/protocol/app_specifics.pb.h"
#include "chrome/browser/sync/protocol/autofill_specifics.pb.h"
#include "chrome/browser/sync/protocol/bookmark_specifics.pb.h"
#include "chrome/browser/sync/protocol/extension_setting_specifics.pb.h"
#include "chrome/browser/sync/protocol/extension_specifics.pb.h"
#include "chrome/browser/sync/protocol/nigori_specifics.pb.h"
#include "chrome/browser/sync/protocol/password_specifics.pb.h"
#include "chrome/browser/sync/protocol/preference_specifics.pb.h"
#include "chrome/browser/sync/protocol/search_engine_specifics.pb.h"
#include "chrome/browser/sync/protocol/session_specifics.pb.h"
#include "chrome/browser/sync/protocol/sync.pb.h"
#include "chrome/browser/sync/protocol/theme_specifics.pb.h"
#include "chrome/browser/sync/protocol/typed_url_specifics.pb.h"

namespace syncable {

void AddDefaultExtensionValue(syncable::ModelType datatype,
                              sync_pb::EntitySpecifics* specifics) {
  switch (datatype) {
    case BOOKMARKS:
      specifics->MutableExtension(sync_pb::bookmark);
      break;
    case PASSWORDS:
      specifics->MutableExtension(sync_pb::password);
      break;
    case PREFERENCES:
      specifics->MutableExtension(sync_pb::preference);
      break;
    case AUTOFILL:
      specifics->MutableExtension(sync_pb::autofill);
      break;
    case AUTOFILL_PROFILE:
      specifics->MutableExtension(sync_pb::autofill_profile);
      break;
    case THEMES:
      specifics->MutableExtension(sync_pb::theme);
      break;
    case TYPED_URLS:
      specifics->MutableExtension(sync_pb::typed_url);
      break;
    case EXTENSIONS:
      specifics->MutableExtension(sync_pb::extension);
      break;
    case NIGORI:
      specifics->MutableExtension(sync_pb::nigori);
      break;
    case SEARCH_ENGINES:
      specifics->MutableExtension(sync_pb::search_engine);
      break;
    case SESSIONS:
      specifics->MutableExtension(sync_pb::session);
      break;
    case APPS:
      specifics->MutableExtension(sync_pb::app);
      break;
    case APP_SETTINGS:
      specifics->MutableExtension(sync_pb::app_setting);
      break;
    case EXTENSION_SETTINGS:
      specifics->MutableExtension(sync_pb::extension_setting);
      break;
    case APP_NOTIFICATIONS:
      specifics->MutableExtension(sync_pb::app_notification);
      break;
    default:
      NOTREACHED() << "No known extension for model type.";
  }
}

ModelType GetModelTypeFromExtensionFieldNumber(int field_number) {
  for (int i = FIRST_REAL_MODEL_TYPE; i < MODEL_TYPE_COUNT; ++i) {
    ModelType model_type = ModelTypeFromInt(i);
    if (GetExtensionFieldNumberFromModelType(model_type) == field_number)
      return model_type;
  }
  NOTREACHED();
  return UNSPECIFIED;
}

int GetExtensionFieldNumberFromModelType(ModelType model_type) {
  switch (model_type) {
    case BOOKMARKS:
      return sync_pb::kBookmarkFieldNumber;
      break;
    case PASSWORDS:
      return sync_pb::kPasswordFieldNumber;
      break;
    case PREFERENCES:
      return sync_pb::kPreferenceFieldNumber;
      break;
    case AUTOFILL:
      return sync_pb::kAutofillFieldNumber;
      break;
    case AUTOFILL_PROFILE:
      return sync_pb::kAutofillProfileFieldNumber;
      break;
    case THEMES:
      return sync_pb::kThemeFieldNumber;
      break;
    case TYPED_URLS:
      return sync_pb::kTypedUrlFieldNumber;
      break;
    case EXTENSIONS:
      return sync_pb::kExtensionFieldNumber;
      break;
    case NIGORI:
      return sync_pb::kNigoriFieldNumber;
      break;
    case SEARCH_ENGINES:
      return sync_pb::kSearchEngineFieldNumber;
      break;
    case SESSIONS:
      return sync_pb::kSessionFieldNumber;
      break;
    case APPS:
      return sync_pb::kAppFieldNumber;
      break;
    case APP_SETTINGS:
      return sync_pb::kAppSettingFieldNumber;
      break;
    case EXTENSION_SETTINGS:
      return sync_pb::kExtensionSettingFieldNumber;
      break;
    case APP_NOTIFICATIONS:
      return sync_pb::kAppNotificationFieldNumber;
      break;
    default:
      NOTREACHED() << "No known extension for model type.";
      return 0;
  }
  NOTREACHED() << "Needed for linux_keep_shadow_stacks because of "
               << "http://gcc.gnu.org/bugzilla/show_bug.cgi?id=20681";
  return 0;
}

// Note: keep this consistent with GetModelType in syncable.cc!
ModelType GetModelType(const sync_pb::SyncEntity& sync_pb_entity) {
  const browser_sync::SyncEntity& sync_entity =
      static_cast<const browser_sync::SyncEntity&>(sync_pb_entity);
  DCHECK(!sync_entity.id().IsRoot());  // Root shouldn't ever go over the wire.

  if (sync_entity.deleted())
    return UNSPECIFIED;

  // Backwards compatibility with old (pre-specifics) protocol.
  if (sync_entity.has_bookmarkdata())
    return BOOKMARKS;

  ModelType specifics_type = GetModelTypeFromSpecifics(sync_entity.specifics());
  if (specifics_type != UNSPECIFIED)
    return specifics_type;

  // Loose check for server-created top-level folders that aren't
  // bound to a particular model type.
  if (!sync_entity.server_defined_unique_tag().empty() &&
      sync_entity.IsFolder()) {
    return TOP_LEVEL_FOLDER;
  }

  // This is an item of a datatype we can't understand. Maybe it's
  // from the future?  Either we mis-encoded the object, or the
  // server sent us entries it shouldn't have.
  NOTREACHED() << "Unknown datatype in sync proto.";
  return UNSPECIFIED;
}

ModelType GetModelTypeFromSpecifics(const sync_pb::EntitySpecifics& specifics) {
  if (specifics.HasExtension(sync_pb::bookmark))
    return BOOKMARKS;

  if (specifics.HasExtension(sync_pb::password))
    return PASSWORDS;

  if (specifics.HasExtension(sync_pb::preference))
    return PREFERENCES;

  if (specifics.HasExtension(sync_pb::autofill))
    return AUTOFILL;

  if (specifics.HasExtension(sync_pb::autofill_profile))
    return AUTOFILL_PROFILE;

  if (specifics.HasExtension(sync_pb::theme))
    return THEMES;

  if (specifics.HasExtension(sync_pb::typed_url))
    return TYPED_URLS;

  if (specifics.HasExtension(sync_pb::extension))
    return EXTENSIONS;

  if (specifics.HasExtension(sync_pb::nigori))
    return NIGORI;

  if (specifics.HasExtension(sync_pb::app))
    return APPS;

  if (specifics.HasExtension(sync_pb::search_engine))
    return SEARCH_ENGINES;

  if (specifics.HasExtension(sync_pb::session))
    return SESSIONS;

  if (specifics.HasExtension(sync_pb::app_setting))
    return APP_SETTINGS;

  if (specifics.HasExtension(sync_pb::extension_setting))
    return EXTENSION_SETTINGS;

  if (specifics.HasExtension(sync_pb::app_notification))
    return APP_NOTIFICATIONS;

  return UNSPECIFIED;
}

bool ShouldMaintainPosition(ModelType model_type) {
  return model_type == BOOKMARKS;
}

const char* ModelTypeToString(ModelType model_type) {
  // This is used in serialization routines as well as for displaying debug
  // information.  Do not attempt to change these string values unless you know
  // what you're doing.
  switch (model_type) {
    case TOP_LEVEL_FOLDER:
      return "Top Level Folder";
    case UNSPECIFIED:
      return "Unspecified";
    case BOOKMARKS:
      return "Bookmarks";
    case PREFERENCES:
      return "Preferences";
    case PASSWORDS:
      return "Passwords";
    case AUTOFILL:
      return "Autofill";
    case THEMES:
      return "Themes";
    case TYPED_URLS:
      return "Typed URLs";
    case EXTENSIONS:
      return "Extensions";
    case NIGORI:
      return "Encryption keys";
    case SEARCH_ENGINES:
      return "Search Engines";
    case SESSIONS:
      return "Sessions";
    case APPS:
      return "Apps";
    case AUTOFILL_PROFILE:
      return "Autofill Profiles";
    case APP_SETTINGS:
      return "App settings";
    case EXTENSION_SETTINGS:
      return "Extension settings";
    case APP_NOTIFICATIONS:
      return "App Notifications";
    default:
      break;
  }
  NOTREACHED() << "No known extension for model type.";
  return "INVALID";
}

StringValue* ModelTypeToValue(ModelType model_type) {
  if (model_type >= syncable::FIRST_REAL_MODEL_TYPE) {
    return Value::CreateStringValue(ModelTypeToString(model_type));
  } else if (model_type == syncable::TOP_LEVEL_FOLDER) {
    return Value::CreateStringValue("Top-level folder");
  } else if (model_type == syncable::UNSPECIFIED) {
    return Value::CreateStringValue("Unspecified");
  }
  NOTREACHED();
  return Value::CreateStringValue("");
}

ModelType ModelTypeFromValue(const Value& value) {
  if (value.IsType(Value::TYPE_STRING)) {
    std::string result;
    CHECK(value.GetAsString(&result));
    return ModelTypeFromString(result);
  } else if (value.IsType(Value::TYPE_INTEGER)) {
    int result;
    CHECK(value.GetAsInteger(&result));
    return ModelTypeFromInt(result);
  } else {
    NOTREACHED() << "Unsupported value type: " << value.GetType();
    return UNSPECIFIED;
  }
}

ModelType ModelTypeFromString(const std::string& model_type_string) {
  if (model_type_string == "Bookmarks")
    return BOOKMARKS;
  else if (model_type_string == "Preferences")
    return PREFERENCES;
  else if (model_type_string == "Passwords")
    return PASSWORDS;
  else if (model_type_string == "Autofill")
    return AUTOFILL;
  else if (model_type_string == "Autofill Profiles")
    return AUTOFILL_PROFILE;
  else if (model_type_string == "Themes")
    return THEMES;
  else if (model_type_string == "Typed URLs")
    return TYPED_URLS;
  else if (model_type_string == "Extensions")
    return EXTENSIONS;
  else if (model_type_string == "Encryption keys")
    return NIGORI;
  else if (model_type_string == "Search Engines")
    return SEARCH_ENGINES;
  else if (model_type_string == "Sessions")
    return SESSIONS;
  else if (model_type_string == "Apps")
    return APPS;
  else if (model_type_string == "App settings")
    return APP_SETTINGS;
  else if (model_type_string == "Extension settings")
    return EXTENSION_SETTINGS;
  else if (model_type_string == "App Notifications")
    return APP_NOTIFICATIONS;
  else
    NOTREACHED() << "No known model type corresponding to "
                 << model_type_string << ".";
  return UNSPECIFIED;
}

std::string ModelTypeSetToString(ModelTypeSet model_types) {
  std::string result;
  for (ModelTypeSet::Iterator it = model_types.First(); it.Good(); it.Inc()) {
    if (!result.empty()) {
      result += ", ";
    }
    result += ModelTypeToString(it.Get());
  }
  return result;
}

base::ListValue* ModelTypeSetToValue(ModelTypeSet model_types) {
  ListValue* value = new ListValue();
  for (ModelTypeSet::Iterator it = model_types.First(); it.Good(); it.Inc()) {
    value->Append(
        Value::CreateStringValue(ModelTypeToString(it.Get())));
  }
  return value;
}

ModelTypeSet ModelTypeSetFromValue(const base::ListValue& value) {
  ModelTypeSet result;
  for (ListValue::const_iterator i = value.begin(); i != value.end(); ++i) {
    result.Put(ModelTypeFromValue(**i));
  }
  return result;
}

// TODO(zea): remove all hardcoded tags in model associators and have them use
// this instead.
std::string ModelTypeToRootTag(ModelType type) {
  switch (type) {
    case BOOKMARKS:
      return "google_chrome_bookmarks";
    case PREFERENCES:
      return "google_chrome_preferences";
    case PASSWORDS:
      return "google_chrome_passwords";
    case AUTOFILL:
      return "google_chrome_autofill";
    case THEMES:
      return "google_chrome_themes";
    case TYPED_URLS:
      return "google_chrome_typed_urls";
    case EXTENSIONS:
      return "google_chrome_extensions";
    case NIGORI:
      return "google_chrome_nigori";
    case SEARCH_ENGINES:
      return "google_chrome_search_engines";
    case SESSIONS:
      return "google_chrome_sessions";
    case APPS:
      return "google_chrome_apps";
    case AUTOFILL_PROFILE:
      return "google_chrome_autofill_profiles";
    case APP_SETTINGS:
      return "google_chrome_app_settings";
    case EXTENSION_SETTINGS:
      return "google_chrome_extension_settings";
    case APP_NOTIFICATIONS:
      return "google_chrome_app_notifications";
    default:
      break;
  }
  NOTREACHED() << "No known extension for model type.";
  return "INVALID";
}

// For now, this just implements UMA_HISTOGRAM_LONG_TIMES. This can be adjusted
// if we feel the min, max, or bucket count amount are not appropriate.
#define SYNC_FREQ_HISTOGRAM(name, time) UMA_HISTOGRAM_CUSTOM_TIMES( \
    name, time, base::TimeDelta::FromMilliseconds(1), \
    base::TimeDelta::FromHours(1), 50)

void PostTimeToTypeHistogram(ModelType model_type, base::TimeDelta time) {
  switch (model_type) {
    case BOOKMARKS: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqBookmarks", time);
        return;
    }
    case PREFERENCES: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqPreferences", time);
        return;
    }
    case PASSWORDS: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqPasswords", time);
        return;
    }
    case AUTOFILL: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqAutofill", time);
        return;
    }
    case AUTOFILL_PROFILE: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqAutofillProfiles", time);
        return;
    }
    case THEMES: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqThemes", time);
        return;
    }
    case TYPED_URLS: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqTypedUrls", time);
        return;
    }
    case EXTENSIONS: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqExtensions", time);
        return;
    }
    case NIGORI: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqNigori", time);
        return;
    }
    case SEARCH_ENGINES: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqSearchEngines", time);
        return;
    }
    case SESSIONS: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqSessions", time);
        return;
    }
    case APPS: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqApps", time);
        return;
    }
    case APP_SETTINGS: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqAppSettings", time);
        return;
    }
    case EXTENSION_SETTINGS: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqExtensionSettings", time);
        return;
    }
    case APP_NOTIFICATIONS: {
        SYNC_FREQ_HISTOGRAM("Sync.FreqAppNotifications", time);
        return;
    }
    default:
      LOG(ERROR) << "No known extension for model type.";
  }
}

#undef SYNC_FREQ_HISTOGRAM

// TODO(akalin): Figure out a better way to do these mappings.

namespace {
const char kBookmarkNotificationType[] = "BOOKMARK";
const char kPreferenceNotificationType[] = "PREFERENCE";
const char kPasswordNotificationType[] = "PASSWORD";
const char kAutofillNotificationType[] = "AUTOFILL";
const char kThemeNotificationType[] = "THEME";
const char kTypedUrlNotificationType[] = "TYPED_URL";
const char kExtensionNotificationType[] = "EXTENSION";
const char kExtensionSettingNotificationType[] = "EXTENSION_SETTING";
const char kNigoriNotificationType[] = "NIGORI";
const char kAppSettingNotificationType[] = "APP_SETTING";
const char kAppNotificationType[] = "APP";
const char kSearchEngineNotificationType[] = "SEARCH_ENGINE";
const char kSessionNotificationType[] = "SESSION";
const char kAutofillProfileNotificationType[] = "AUTOFILL_PROFILE";
const char kAppNotificationNotificationType[] = "APP_NOTIFICATION";
}  // namespace

bool RealModelTypeToNotificationType(ModelType model_type,
                                     std::string* notification_type) {
  switch (model_type) {
    case BOOKMARKS:
      *notification_type = kBookmarkNotificationType;
      return true;
    case PREFERENCES:
      *notification_type = kPreferenceNotificationType;
      return true;
    case PASSWORDS:
      *notification_type = kPasswordNotificationType;
      return true;
    case AUTOFILL:
      *notification_type = kAutofillNotificationType;
      return true;
    case THEMES:
      *notification_type = kThemeNotificationType;
      return true;
    case TYPED_URLS:
      *notification_type = kTypedUrlNotificationType;
      return true;
    case EXTENSIONS:
      *notification_type = kExtensionNotificationType;
      return true;
    case NIGORI:
      *notification_type = kNigoriNotificationType;
      return true;
    case APP_SETTINGS:
      *notification_type = kAppNotificationType;
      return true;
    case APPS:
      *notification_type = kAppNotificationType;
      return true;
    case SEARCH_ENGINES:
      *notification_type = kSearchEngineNotificationType;
      return true;
    case SESSIONS:
      *notification_type = kSessionNotificationType;
      return true;
    case AUTOFILL_PROFILE:
      *notification_type = kAutofillProfileNotificationType;
      return true;
    case EXTENSION_SETTINGS:
      *notification_type = kExtensionSettingNotificationType;
      return true;
    case APP_NOTIFICATIONS:
      *notification_type = kAppNotificationNotificationType;
      return true;
    default:
      break;
  }
  notification_type->clear();
  return false;
}

bool NotificationTypeToRealModelType(const std::string& notification_type,
                                     ModelType* model_type) {
  if (notification_type == kBookmarkNotificationType) {
    *model_type = BOOKMARKS;
    return true;
  } else if (notification_type == kPreferenceNotificationType) {
    *model_type = PREFERENCES;
    return true;
  } else if (notification_type == kPasswordNotificationType) {
    *model_type = PASSWORDS;
    return true;
  } else if (notification_type == kAutofillNotificationType) {
    *model_type = AUTOFILL;
    return true;
  } else if (notification_type == kThemeNotificationType) {
    *model_type = THEMES;
    return true;
  } else if (notification_type == kTypedUrlNotificationType) {
    *model_type = TYPED_URLS;
    return true;
  } else if (notification_type == kExtensionNotificationType) {
    *model_type = EXTENSIONS;
    return true;
  } else if (notification_type == kNigoriNotificationType) {
    *model_type = NIGORI;
    return true;
  } else if (notification_type == kAppNotificationType) {
    *model_type = APPS;
    return true;
  } else if (notification_type == kSearchEngineNotificationType) {
    *model_type = SEARCH_ENGINES;
    return true;
  } else if (notification_type == kSessionNotificationType) {
    *model_type = SESSIONS;
    return true;
  } else if (notification_type == kAutofillProfileNotificationType) {
    *model_type = AUTOFILL_PROFILE;
    return true;
  } else if (notification_type == kAppSettingNotificationType) {
    *model_type = APP_SETTINGS;
    return true;
  } else if (notification_type == kExtensionSettingNotificationType) {
    *model_type = EXTENSION_SETTINGS;
    return true;
  } else if (notification_type == kAppNotificationNotificationType) {
    *model_type = APP_NOTIFICATIONS;
    return true;
  } else {
    *model_type = UNSPECIFIED;
    return false;
  }
}

bool IsRealDataType(ModelType model_type) {
  return model_type >= FIRST_REAL_MODEL_TYPE && model_type < MODEL_TYPE_COUNT;
}

}  // namespace syncable
