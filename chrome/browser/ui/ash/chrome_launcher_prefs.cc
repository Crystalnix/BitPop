// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/chrome_launcher_prefs.h"

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/values.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/common/pref_names.h"

namespace {

// App ID of default pinned apps.
const char* kDefaultPinnedApps[] = {
  "pjkljhegncpnkpknbcohdijeoejaedia",  // Gmail
  "coobgpohoikkiipiblmjeljniedjpjpf",  // Search
  "apdfllckaahabafndbhieahigkjlhalf",  // Doc
  "blpcfgokakmgnkcojhhkbfbldkacnbeo",  // YouTube
};

base::ListValue* CreateDefaultPinnedAppsList() {
  scoped_ptr<base::ListValue> apps(new base::ListValue);
  for (size_t i = 0; i < arraysize(kDefaultPinnedApps); ++i)
    apps->Append(ash::CreateAppDict(kDefaultPinnedApps[i]));

  return apps.release();
}

}  // namespace

namespace ash {

const char kPinnedAppsPrefAppIDPath[] = "id";

const char kShelfAutoHideBehaviorAlways[] = "Always";
const char kShelfAutoHideBehaviorDefault[] = "Default";
const char kShelfAutoHideBehaviorNever[] = "Never";

extern const char kShelfAlignmentBottom[] = "Bottom";
extern const char kShelfAlignmentLeft[] = "Left";
extern const char kShelfAlignmentRight[] = "Right";

void RegisterChromeLauncherUserPrefs(PrefService* user_prefs) {
  // TODO: If we want to support multiple profiles this will likely need to be
  // pushed to local state and we'll need to track profile per item.
  user_prefs->RegisterBooleanPref(prefs::kUseDefaultPinnedApps,
                                  true,
                                  PrefService::SYNCABLE_PREF);
  user_prefs->RegisterListPref(prefs::kPinnedLauncherApps,
                               CreateDefaultPinnedAppsList(),
                               PrefService::SYNCABLE_PREF);
  user_prefs->RegisterStringPref(prefs::kShelfAutoHideBehavior,
                                 kShelfAutoHideBehaviorDefault,
                                 PrefService::SYNCABLE_PREF);
  user_prefs->RegisterStringPref(prefs::kShelfAlignment,
                                 kShelfAlignmentBottom,
                                 PrefService::SYNCABLE_PREF);
}

base::DictionaryValue* CreateAppDict(const std::string& app_id) {
  scoped_ptr<base::DictionaryValue> app_value(new base::DictionaryValue);
  app_value->SetString(kPinnedAppsPrefAppIDPath, app_id);
  return app_value.release();
}

}  // namespace ash
