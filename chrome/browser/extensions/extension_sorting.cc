// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_sorting.h"

#include "chrome/browser/extensions/extension_scoped_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/notification_service.h"

namespace {

// The number of apps per page. This isn't a hard limit, but new apps installed
// from the webstore will overflow onto a new page if this limit is reached.
const size_t kNaturalAppPageSize = 18;

// A preference determining the order of which the apps appear on the NTP.
const char kPrefAppLaunchIndexDeprecated[] = "app_launcher_index";
const char kPrefAppLaunchOrdinal[] = "app_launcher_ordinal";

// A preference determining the page on which an app appears in the NTP.
const char kPrefPageIndexDeprecated[] = "page_index";
const char kPrefPageOrdinal[] = "page_ordinal";

}  // namespace

ExtensionSorting::ExtensionSorting(ExtensionScopedPrefs* extension_scoped_prefs,
                                   PrefService* pref_service)
    : extension_scoped_prefs_(extension_scoped_prefs),
      pref_service_(pref_service) {
}

ExtensionSorting::~ExtensionSorting() {
}

void ExtensionSorting::Initialize(
    const ExtensionPrefs::ExtensionIdSet& extension_ids) {
  InitializePageOrdinalMap(extension_ids);

  MigrateAppIndex(extension_ids);
}

void ExtensionSorting::MigrateAppIndex(
    const ExtensionPrefs::ExtensionIdSet& extension_ids) {
  if (extension_ids.empty())
    return;

  // Convert all the page index values to page ordinals. If there are any
  // app launch values that need to be migrated, inserted them into a sorted
  // set to be dealt with later.
  typedef std::map<StringOrdinal, std::map<int, const std::string*>,
      StringOrdinalLessThan> AppPositionToIdMapping;
  AppPositionToIdMapping app_launches_to_convert;
  for (ExtensionPrefs::ExtensionIdSet::const_iterator ext_id =
           extension_ids.begin(); ext_id != extension_ids.end(); ++ext_id) {
    int old_page_index = 0;
    StringOrdinal page = GetPageOrdinal(*ext_id);
    if (extension_scoped_prefs_->ReadExtensionPrefInteger(
            *ext_id,
            kPrefPageIndexDeprecated,
            &old_page_index)) {
      // Some extensions have invalid page index, so we don't
      // attempt to convert them.
      if (old_page_index < 0) {
        DLOG(WARNING) << "Extension " << *ext_id
                      << " has an invalid page index " << old_page_index
                      << ". Aborting attempt to convert its index.";
        break;
      }

      // Since we require all earlier StringOrdinals to already exist in order
      // to properly convert from integers and we are iterating though them in
      // no given order, we create earlier StringOrdinal values as required.
      // This should be filled in by the time we are done with this loop.
      if (ntp_ordinal_map_.empty())
        ntp_ordinal_map_[StringOrdinal::CreateInitialOrdinal()];
      while (ntp_ordinal_map_.size()
             <= static_cast<size_t>(old_page_index)) {
        StringOrdinal earlier_page =
            ntp_ordinal_map_.rbegin()->first.CreateAfter();
        ntp_ordinal_map_[earlier_page];
      }

      page = PageIntegerAsStringOrdinal(old_page_index);
      SetPageOrdinal(*ext_id, page);
      extension_scoped_prefs_->UpdateExtensionPref(
          *ext_id, kPrefPageIndexDeprecated, NULL);
    }

    int old_app_launch_index = 0;
    if (extension_scoped_prefs_->ReadExtensionPrefInteger(
            *ext_id,
            kPrefAppLaunchIndexDeprecated,
            &old_app_launch_index)) {
      // We can't update the app launch index value yet, because we use
      // GetNextAppLaunchOrdinal to get the new ordinal value and it requires
      // all the ordinals with lower values to have already been migrated.
      // A valid page ordinal is also required because otherwise there is
      // no page to add the app to.
      if (page.IsValid())
        app_launches_to_convert[page][old_app_launch_index] = &*ext_id;

      extension_scoped_prefs_->UpdateExtensionPref(
          *ext_id, kPrefAppLaunchIndexDeprecated, NULL);
    }
  }

  // Remove any empty pages that may have been added. This shouldn't occur,
  // but double check here to prevent future problems with conversions between
  // integers and StringOrdinals.
  for (PageOrdinalMap::iterator it = ntp_ordinal_map_.begin();
       it != ntp_ordinal_map_.end();) {
    if (it->second.empty()) {
      PageOrdinalMap::iterator prev_it = it;
      ++it;
      ntp_ordinal_map_.erase(prev_it);
    } else {
      ++it;
    }
  }

  if (app_launches_to_convert.empty())
    return;

  // Create the new app launch ordinals and remove the old preferences. Since
  // the set is sorted, each time we migrate an apps index, we know that all of
  // the remaining apps will appear further down the NTP than it or on a
  // different page.
  for (AppPositionToIdMapping::const_iterator page_it =
           app_launches_to_convert.begin();
       page_it != app_launches_to_convert.end(); ++page_it) {
    StringOrdinal page = page_it->first;
    for (std::map<int, const std::string*>::const_iterator launch_it =
            page_it->second.begin(); launch_it != page_it->second.end();
        ++launch_it) {
      SetAppLaunchOrdinal(*(launch_it->second),
                          CreateNextAppLaunchOrdinal(page));
    }
  }
}

void ExtensionSorting::FixNTPOrdinalCollisions() {
  for (PageOrdinalMap::iterator page_it = ntp_ordinal_map_.begin();
       page_it != ntp_ordinal_map_.end(); ++page_it) {
    AppLaunchOrdinalMap& page = page_it->second;

    AppLaunchOrdinalMap::iterator app_launch_it = page.begin();
    while (app_launch_it != page.end()) {
      int app_count = page.count(app_launch_it->first);
      if (app_count == 1) {
        ++app_launch_it;
        continue;
      }

      StringOrdinal repeated_ordinal = app_launch_it->first;

      // Sort the conflicting keys by their extension id, this is how
      // the order is decided.
      std::vector<std::string> conflicting_ids;
      for (int i = 0; i < app_count; ++i, ++app_launch_it)
        conflicting_ids.push_back(app_launch_it->second);
      std::sort(conflicting_ids.begin(), conflicting_ids.end());

      StringOrdinal upper_bound_ordinal = app_launch_it == page.end() ?
          StringOrdinal() :
          app_launch_it->first;
      StringOrdinal lower_bound_ordinal = repeated_ordinal;

      // Start at position 1 because the first extension can keep the conflicted
      // value.
      for (int i = 1; i < app_count; ++i) {
        StringOrdinal unique_app_launch;
        if (upper_bound_ordinal.IsValid()) {
          unique_app_launch =
              lower_bound_ordinal.CreateBetween(upper_bound_ordinal);
        } else {
          unique_app_launch = lower_bound_ordinal.CreateAfter();
        }

        SetAppLaunchOrdinal(conflicting_ids[i], unique_app_launch);
        lower_bound_ordinal = unique_app_launch;
      }
    }
  }

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_EXTENSION_LAUNCHER_REORDERED,
      content::Source<ExtensionSorting>(this),
      content::NotificationService::NoDetails());
}

void ExtensionSorting::EnsureValidOrdinals(const std::string& extension_id) {
  StringOrdinal page_ordinal = GetPageOrdinal(extension_id);
  if (!page_ordinal.IsValid()) {
    // The webstore app should always start be on the first page.
    page_ordinal = extension_id == extension_misc::kWebStoreAppId ?
        CreateFirstAppPageOrdinal() :
        GetNaturalAppPageOrdinal();
    SetPageOrdinal(extension_id, page_ordinal);
  }

  StringOrdinal app_launch_ordinal = GetAppLaunchOrdinal(extension_id);
  if (!app_launch_ordinal.IsValid()) {
    // The webstore app should always start in the position.
    app_launch_ordinal = extension_id == extension_misc::kWebStoreAppId ?
        CreateFirstAppLaunchOrdinal(page_ordinal) :
        CreateNextAppLaunchOrdinal(page_ordinal);
    SetAppLaunchOrdinal(extension_id,
                        app_launch_ordinal);
  }
}

void ExtensionSorting::OnExtensionMoved(
    const std::string& moved_extension_id,
    const std::string& predecessor_extension_id,
    const std::string& successor_extension_id) {
  // We only need to change the StringOrdinal if there are neighbours.
  if (!predecessor_extension_id.empty() || !successor_extension_id.empty()) {
    if (predecessor_extension_id.empty()) {
      // Only a successor.
      SetAppLaunchOrdinal(
          moved_extension_id,
          GetAppLaunchOrdinal(successor_extension_id).CreateBefore());
    } else if (successor_extension_id.empty()) {
      // Only a predecessor.
      SetAppLaunchOrdinal(
          moved_extension_id,
          GetAppLaunchOrdinal(predecessor_extension_id).CreateAfter());
    } else {
      // Both a successor and predecessor
      const StringOrdinal& predecessor_ordinal =
          GetAppLaunchOrdinal(predecessor_extension_id);
      const StringOrdinal& successor_ordinal =
          GetAppLaunchOrdinal(successor_extension_id);
      SetAppLaunchOrdinal(moved_extension_id,
                          predecessor_ordinal.CreateBetween(successor_ordinal));
    }
  }

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_EXTENSION_LAUNCHER_REORDERED,
      content::Source<ExtensionSorting>(this),
      content::NotificationService::NoDetails());
}


StringOrdinal ExtensionSorting::GetAppLaunchOrdinal(
    const std::string& extension_id) const {
  std::string raw_value;
  // If the preference read fails then raw_value will still be unset and we
  // will return an invalid StringOrdinal to signal that no app launch ordinal
  // was found.
  extension_scoped_prefs_->ReadExtensionPrefString(
      extension_id, kPrefAppLaunchOrdinal, &raw_value);
  return StringOrdinal(raw_value);
}

void ExtensionSorting::SetAppLaunchOrdinal(
    const std::string& extension_id,
    const StringOrdinal& new_app_launch_ordinal) {
  StringOrdinal page_ordinal = GetPageOrdinal(extension_id);
  RemoveOrdinalMapping(
      extension_id, page_ordinal, GetAppLaunchOrdinal(extension_id));
  AddOrdinalMapping(extension_id, page_ordinal, new_app_launch_ordinal);

  Value* new_value = new_app_launch_ordinal.IsValid() ?
      Value::CreateStringValue(new_app_launch_ordinal.ToString()) :
      NULL;

  extension_scoped_prefs_->UpdateExtensionPref(
      extension_id,
      kPrefAppLaunchOrdinal,
      new_value);
}

StringOrdinal ExtensionSorting::CreateFirstAppLaunchOrdinal(
    const StringOrdinal& page_ordinal) const {
  const StringOrdinal& min_ordinal =
      GetMinOrMaxAppLaunchOrdinalsOnPage(page_ordinal,
                                         ExtensionSorting::MIN_ORDINAL);

  if (min_ordinal.IsValid())
    return min_ordinal.CreateBefore();
  else
    return StringOrdinal::CreateInitialOrdinal();
}

StringOrdinal ExtensionSorting::CreateNextAppLaunchOrdinal(
    const StringOrdinal& page_ordinal) const {
  const StringOrdinal& max_ordinal =
      GetMinOrMaxAppLaunchOrdinalsOnPage(page_ordinal,
                                         ExtensionSorting::MAX_ORDINAL);

  if (max_ordinal.IsValid())
    return max_ordinal.CreateAfter();
  else
    return StringOrdinal::CreateInitialOrdinal();
}

StringOrdinal ExtensionSorting::CreateFirstAppPageOrdinal() const {
  const DictionaryValue* extensions = pref_service_->GetDictionary(
          ExtensionPrefs::kExtensionsPref);
  CHECK(extensions);

  if (ntp_ordinal_map_.empty())
    return StringOrdinal::CreateInitialOrdinal();

  return ntp_ordinal_map_.begin()->first;
}

StringOrdinal ExtensionSorting::GetNaturalAppPageOrdinal() const {
  const DictionaryValue* extensions = pref_service_->GetDictionary(
          ExtensionPrefs::kExtensionsPref);
  CHECK(extensions);

  if (ntp_ordinal_map_.empty())
    return StringOrdinal::CreateInitialOrdinal();

  for (PageOrdinalMap::const_iterator it = ntp_ordinal_map_.begin();
       it != ntp_ordinal_map_.end(); ++it) {
    if (it->second.size() < kNaturalAppPageSize)
      return it->first;
  }

  // Add a new page as all existing pages are full.
  StringOrdinal last_element = ntp_ordinal_map_.rbegin()->first;
  return last_element.CreateAfter();
}

StringOrdinal ExtensionSorting::GetPageOrdinal(const std::string& extension_id)
    const {
  std::string raw_data;
  // If the preference read fails then raw_data will still be unset and we will
  // return an invalid StringOrdinal to signal that no page ordinal was found.
  extension_scoped_prefs_->ReadExtensionPrefString(
      extension_id, kPrefPageOrdinal, &raw_data);
  return StringOrdinal(raw_data);
}

void ExtensionSorting::SetPageOrdinal(const std::string& extension_id,
                                    const StringOrdinal& new_page_ordinal) {
  StringOrdinal app_launch_ordinal = GetAppLaunchOrdinal(extension_id);
  RemoveOrdinalMapping(
      extension_id, GetPageOrdinal(extension_id), app_launch_ordinal);
  AddOrdinalMapping(extension_id, new_page_ordinal, app_launch_ordinal);

  Value* new_value = new_page_ordinal.IsValid() ?
      Value::CreateStringValue(new_page_ordinal.ToString()) :
      NULL;

  extension_scoped_prefs_->UpdateExtensionPref(
      extension_id,
      kPrefPageOrdinal,
      new_value);
}

void ExtensionSorting::ClearOrdinals(const std::string& extension_id) {
  RemoveOrdinalMapping(extension_id,
                       GetPageOrdinal(extension_id),
                       GetAppLaunchOrdinal(extension_id));

  extension_scoped_prefs_->UpdateExtensionPref(
      extension_id, kPrefPageOrdinal, NULL);
  extension_scoped_prefs_->UpdateExtensionPref(
      extension_id, kPrefAppLaunchOrdinal, NULL);
}

int ExtensionSorting::PageStringOrdinalAsInteger(
    const StringOrdinal& page_ordinal) const {
  if (!page_ordinal.IsValid())
    return -1;

  PageOrdinalMap::const_iterator it = ntp_ordinal_map_.find(page_ordinal);
  if (it != ntp_ordinal_map_.end()) {
    return std::distance(ntp_ordinal_map_.begin(), it);
  } else {
    return -1;
  }
}

StringOrdinal ExtensionSorting::PageIntegerAsStringOrdinal(size_t page_index)
    const {
  // We shouldn't have a page_index that is more than 1 position away from the
  // current end.
  CHECK_LE(page_index, ntp_ordinal_map_.size());

  const DictionaryValue* extensions = pref_service_->GetDictionary(
          ExtensionPrefs::kExtensionsPref);
  if (!extensions)
    return StringOrdinal();

  if (page_index < ntp_ordinal_map_.size()) {
    PageOrdinalMap::const_iterator it = ntp_ordinal_map_.begin();
    std::advance(it, page_index);

    return it->first;

  } else {
    if (ntp_ordinal_map_.empty())
      return StringOrdinal::CreateInitialOrdinal();
    else
      return ntp_ordinal_map_.rbegin()->first.CreateAfter();
  }
}

StringOrdinal ExtensionSorting::GetMinOrMaxAppLaunchOrdinalsOnPage(
    const StringOrdinal& target_page_ordinal,
    AppLaunchOrdinalReturn return_type) const {
  CHECK(target_page_ordinal.IsValid());

  StringOrdinal return_value;

  PageOrdinalMap::const_iterator page =
      ntp_ordinal_map_.find(target_page_ordinal);
  if (page != ntp_ordinal_map_.end()) {
    const AppLaunchOrdinalMap& app_list = page->second;

    if (app_list.empty())
      return StringOrdinal();

    if (return_type == ExtensionSorting::MAX_ORDINAL)
      return_value = app_list.rbegin()->first;
    else if (return_type == ExtensionSorting::MIN_ORDINAL)
      return_value = app_list.begin()->first;
  }

  return return_value;
}

void ExtensionSorting::InitializePageOrdinalMap(
    const ExtensionPrefs::ExtensionIdSet& extension_ids) {
  for (ExtensionPrefs::ExtensionIdSet::const_iterator ext_it =
           extension_ids.begin(); ext_it != extension_ids.end(); ++ext_it) {
    AddOrdinalMapping(*ext_it,
                      GetPageOrdinal(*ext_it),
                      GetAppLaunchOrdinal(*ext_it));

    // Ensure that the web store app still isn't found in this list, since
    // it is added after this loop.
    DCHECK(*ext_it != extension_misc::kWebStoreAppId);
  }

  // Include the Web Store App since it is displayed on the NTP.
  StringOrdinal web_store_app_page =
      GetPageOrdinal(extension_misc::kWebStoreAppId);
  if (web_store_app_page.IsValid()) {
    AddOrdinalMapping(extension_misc::kWebStoreAppId,
                      web_store_app_page,
                      GetAppLaunchOrdinal(extension_misc::kWebStoreAppId));
  }
}

void ExtensionSorting::AddOrdinalMapping(
    const std::string& extension_id,
    const StringOrdinal& page_ordinal,
    const StringOrdinal& app_launch_ordinal) {
  if (!page_ordinal.IsValid() || !app_launch_ordinal.IsValid())
    return;

  ntp_ordinal_map_[page_ordinal].insert(
      std::make_pair(app_launch_ordinal, extension_id));
}

void ExtensionSorting::RemoveOrdinalMapping(
    const std::string& extension_id,
    const StringOrdinal& page_ordinal,
    const StringOrdinal& app_launch_ordinal) {
  if (!page_ordinal.IsValid() || !app_launch_ordinal.IsValid())
    return;

  // Check that the page exists using find to prevent creating a new page
  // if |page_ordinal| isn't a used page.
  PageOrdinalMap::iterator page_map = ntp_ordinal_map_.find(page_ordinal);
  if (page_map == ntp_ordinal_map_.end())
    return;

  for (AppLaunchOrdinalMap::iterator it =
           page_map->second.find(app_launch_ordinal);
       it != page_map->second.end(); ++it) {
    if (it->second == extension_id) {
      page_map->second.erase(it);
      break;
    }
  }
}
