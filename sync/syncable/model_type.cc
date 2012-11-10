// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/internal_api/public/base/model_type.h"

#include "base/string_split.h"
#include "base/values.h"
#include "sync/protocol/app_notification_specifics.pb.h"
#include "sync/protocol/app_setting_specifics.pb.h"
#include "sync/protocol/app_specifics.pb.h"
#include "sync/protocol/autofill_specifics.pb.h"
#include "sync/protocol/bookmark_specifics.pb.h"
#include "sync/protocol/extension_setting_specifics.pb.h"
#include "sync/protocol/extension_specifics.pb.h"
#include "sync/protocol/nigori_specifics.pb.h"
#include "sync/protocol/password_specifics.pb.h"
#include "sync/protocol/preference_specifics.pb.h"
#include "sync/protocol/search_engine_specifics.pb.h"
#include "sync/protocol/session_specifics.pb.h"
#include "sync/protocol/sync.pb.h"
#include "sync/protocol/theme_specifics.pb.h"
#include "sync/protocol/typed_url_specifics.pb.h"
#include "sync/syncable/syncable_proto_util.h"

namespace syncer {

void AddDefaultFieldValue(ModelType datatype,
                          sync_pb::EntitySpecifics* specifics) {
  switch (datatype) {
    case BOOKMARKS:
      specifics->mutable_bookmark();
      break;
    case PASSWORDS:
      specifics->mutable_password();
      break;
    case PREFERENCES:
      specifics->mutable_preference();
      break;
    case AUTOFILL:
      specifics->mutable_autofill();
      break;
    case AUTOFILL_PROFILE:
      specifics->mutable_autofill_profile();
      break;
    case THEMES:
      specifics->mutable_theme();
      break;
    case TYPED_URLS:
      specifics->mutable_typed_url();
      break;
    case EXTENSIONS:
      specifics->mutable_extension();
      break;
    case NIGORI:
      specifics->mutable_nigori();
      break;
    case SEARCH_ENGINES:
      specifics->mutable_search_engine();
      break;
    case SESSIONS:
      specifics->mutable_session();
      break;
    case APPS:
      specifics->mutable_app();
      break;
    case APP_SETTINGS:
      specifics->mutable_app_setting();
      break;
    case EXTENSION_SETTINGS:
      specifics->mutable_extension_setting();
      break;
    case APP_NOTIFICATIONS:
      specifics->mutable_app_notification();
      break;
    default:
      NOTREACHED() << "No known extension for model type.";
  }
}

ModelType GetModelTypeFromSpecificsFieldNumber(int field_number) {
  for (int i = FIRST_REAL_MODEL_TYPE; i < MODEL_TYPE_COUNT; ++i) {
    ModelType model_type = ModelTypeFromInt(i);
    if (GetSpecificsFieldNumberFromModelType(model_type) == field_number)
      return model_type;
  }
  NOTREACHED();
  return UNSPECIFIED;
}

int GetSpecificsFieldNumberFromModelType(ModelType model_type) {
  switch (model_type) {
    case BOOKMARKS:
      return sync_pb::EntitySpecifics::kBookmarkFieldNumber;
      break;
    case PASSWORDS:
      return sync_pb::EntitySpecifics::kPasswordFieldNumber;
      break;
    case PREFERENCES:
      return sync_pb::EntitySpecifics::kPreferenceFieldNumber;
      break;
    case AUTOFILL:
      return sync_pb::EntitySpecifics::kAutofillFieldNumber;
      break;
    case AUTOFILL_PROFILE:
      return sync_pb::EntitySpecifics::kAutofillProfileFieldNumber;
      break;
    case THEMES:
      return sync_pb::EntitySpecifics::kThemeFieldNumber;
      break;
    case TYPED_URLS:
      return sync_pb::EntitySpecifics::kTypedUrlFieldNumber;
      break;
    case EXTENSIONS:
      return sync_pb::EntitySpecifics::kExtensionFieldNumber;
      break;
    case NIGORI:
      return sync_pb::EntitySpecifics::kNigoriFieldNumber;
      break;
    case SEARCH_ENGINES:
      return sync_pb::EntitySpecifics::kSearchEngineFieldNumber;
      break;
    case SESSIONS:
      return sync_pb::EntitySpecifics::kSessionFieldNumber;
      break;
    case APPS:
      return sync_pb::EntitySpecifics::kAppFieldNumber;
      break;
    case APP_SETTINGS:
      return sync_pb::EntitySpecifics::kAppSettingFieldNumber;
      break;
    case EXTENSION_SETTINGS:
      return sync_pb::EntitySpecifics::kExtensionSettingFieldNumber;
      break;
    case APP_NOTIFICATIONS:
      return sync_pb::EntitySpecifics::kAppNotificationFieldNumber;
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
ModelType GetModelType(const sync_pb::SyncEntity& sync_entity) {
  DCHECK(!IsRoot(sync_entity));  // Root shouldn't ever go over the wire.

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
      IsFolder(sync_entity)) {
    return TOP_LEVEL_FOLDER;
  }

  // This is an item of a datatype we can't understand. Maybe it's
  // from the future?  Either we mis-encoded the object, or the
  // server sent us entries it shouldn't have.
  NOTREACHED() << "Unknown datatype in sync proto.";
  return UNSPECIFIED;
}

ModelType GetModelTypeFromSpecifics(const sync_pb::EntitySpecifics& specifics) {
  if (specifics.has_bookmark())
    return BOOKMARKS;

  if (specifics.has_password())
    return PASSWORDS;

  if (specifics.has_preference())
    return PREFERENCES;

  if (specifics.has_autofill())
    return AUTOFILL;

  if (specifics.has_autofill_profile())
    return AUTOFILL_PROFILE;

  if (specifics.has_theme())
    return THEMES;

  if (specifics.has_typed_url())
    return TYPED_URLS;

  if (specifics.has_extension())
    return EXTENSIONS;

  if (specifics.has_nigori())
    return NIGORI;

  if (specifics.has_app())
    return APPS;

  if (specifics.has_search_engine())
    return SEARCH_ENGINES;

  if (specifics.has_session())
    return SESSIONS;

  if (specifics.has_app_setting())
    return APP_SETTINGS;

  if (specifics.has_extension_setting())
    return EXTENSION_SETTINGS;

  if (specifics.has_app_notification())
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
  if (model_type >= FIRST_REAL_MODEL_TYPE) {
    return Value::CreateStringValue(ModelTypeToString(model_type));
  } else if (model_type == TOP_LEVEL_FOLDER) {
    return Value::CreateStringValue("Top-level folder");
  } else if (model_type == UNSPECIFIED) {
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
      *notification_type = kAppSettingNotificationType;
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

}  // namespace syncer
