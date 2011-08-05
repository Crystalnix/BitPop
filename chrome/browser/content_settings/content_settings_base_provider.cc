// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "chrome/browser/content_settings/content_settings_base_provider.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "chrome/browser/content_settings/content_settings_utils.h"
#include "chrome/common/chrome_switches.h"
#include "googleurl/src/gurl.h"
#include "net/base/net_util.h"

namespace content_settings {

ExtendedContentSettings::ExtendedContentSettings() {}

ExtendedContentSettings::ExtendedContentSettings(
    const ExtendedContentSettings& rhs)
    : content_settings(rhs.content_settings),
      content_settings_for_resources(rhs.content_settings_for_resources) {
}

ExtendedContentSettings::~ExtendedContentSettings() {}

BaseProvider::BaseProvider(bool is_incognito)
    : is_incognito_(is_incognito) {
}

BaseProvider::~BaseProvider() {}

bool BaseProvider::AllDefault(
    const ExtendedContentSettings& settings) const {
  for (size_t i = 0; i < arraysize(settings.content_settings.settings); ++i) {
    if (settings.content_settings.settings[i] != CONTENT_SETTING_DEFAULT)
      return false;
  }
  return settings.content_settings_for_resources.empty();
}

ContentSetting BaseProvider::GetContentSetting(
    const GURL& requesting_url,
    const GURL& embedding_url,
    ContentSettingsType content_type,
    const ResourceIdentifier& resource_identifier) const {
  // Support for embedding_patterns is not implemented yet.
  DCHECK(requesting_url == embedding_url);

  if (!RequiresResourceIdentifier(content_type) ||
      (RequiresResourceIdentifier(content_type) && resource_identifier.empty()))
    return GetNonDefaultContentSettings(requesting_url).settings[content_type];

  // Resolve content settings with resource identifier.
  // 1. Check for pattern that exactly match the url/host
  //     1.1 In the content-settings-map
  //     1.2 In the incognito content-settings-map
  // 3. Shorten the url subdomain by subdomain and try to find a pattern in
  //     3.1 OTR content-settings-map
  //     3.2 content-settings-map
  base::AutoLock auto_lock(lock_);
  const std::string host(net::GetHostOrSpecFromURL(requesting_url));
  ContentSettingsTypeResourceIdentifierPair
      requested_setting(content_type, resource_identifier);

  // Check for exact matches first.
  HostContentSettings::const_iterator i(host_content_settings_.find(host));
  if (i != host_content_settings_.end() &&
      i->second.content_settings_for_resources.find(requested_setting) !=
      i->second.content_settings_for_resources.end()) {
    return i->second.content_settings_for_resources.find(
        requested_setting)->second;
  }

  // If this map is not for an incognito profile, these searches will never
  // match. The additional incognito exceptions always overwrite the
  // regular ones.
  i = incognito_settings_.find(host);
  if (i != incognito_settings_.end() &&
      i->second.content_settings_for_resources.find(requested_setting) !=
      i->second.content_settings_for_resources.end()) {
    return i->second.content_settings_for_resources.find(
        requested_setting)->second;
  }

  // Match patterns starting with the most concrete pattern match.
  for (std::string key =
       std::string(ContentSettingsPattern::kDomainWildcard) + host; ; ) {
    HostContentSettings::const_iterator i(incognito_settings_.find(key));
    if (i != incognito_settings_.end() &&
        i->second.content_settings_for_resources.find(requested_setting) !=
        i->second.content_settings_for_resources.end()) {
      return i->second.content_settings_for_resources.find(
          requested_setting)->second;
    }

    i = host_content_settings_.find(key);
    if (i != host_content_settings_.end() &&
        i->second.content_settings_for_resources.find(requested_setting) !=
        i->second.content_settings_for_resources.end()) {
      return i->second.content_settings_for_resources.find(
          requested_setting)->second;
    }

    const size_t next_dot =
        key.find('.', ContentSettingsPattern::kDomainWildcardLength);
    if (next_dot == std::string::npos)
      break;
    key.erase(ContentSettingsPattern::kDomainWildcardLength,
              next_dot - ContentSettingsPattern::kDomainWildcardLength + 1);
  }

  return CONTENT_SETTING_DEFAULT;
}

void BaseProvider::GetAllContentSettingsRules(
    ContentSettingsType content_type,
    const ResourceIdentifier& resource_identifier,
    Rules* content_setting_rules) const {
  DCHECK(content_setting_rules);
  content_setting_rules->clear();

  const HostContentSettings* map_to_return =
      is_incognito_ ? &incognito_settings_ : &host_content_settings_;
  ContentSettingsTypeResourceIdentifierPair requested_setting(
      content_type, resource_identifier);

  base::AutoLock auto_lock(lock_);
  for (HostContentSettings::const_iterator i(map_to_return->begin());
       i != map_to_return->end(); ++i) {
    ContentSetting setting;
    if (RequiresResourceIdentifier(content_type)) {
      if (i->second.content_settings_for_resources.find(requested_setting) !=
          i->second.content_settings_for_resources.end()) {
        setting = i->second.content_settings_for_resources.find(
            requested_setting)->second;
      } else {
        setting = CONTENT_SETTING_DEFAULT;
      }
    } else {
     setting = i->second.content_settings.settings[content_type];
    }
    if (setting != CONTENT_SETTING_DEFAULT) {
      // Use of push_back() relies on the map iterator traversing in order of
      // ascending keys.
      content_setting_rules->push_back(
          Rule(ContentSettingsPattern::LegacyFromString(i->first),
               ContentSettingsPattern::LegacyFromString(i->first),
               setting));
    }
  }
}

ContentSettings BaseProvider::GetNonDefaultContentSettings(
    const GURL& url) const {
  base::AutoLock auto_lock(lock_);

  const std::string host(net::GetHostOrSpecFromURL(url));
  ContentSettings output;
  for (int j = 0; j < CONTENT_SETTINGS_NUM_TYPES; ++j)
    output.settings[j] = CONTENT_SETTING_DEFAULT;

  // Check for exact matches first.
  HostContentSettings::const_iterator i(host_content_settings_.find(host));
  if (i != host_content_settings_.end())
    output = i->second.content_settings;

  // If this map is not for an incognito profile, these searches will never
  // match. The additional incognito exceptions always overwrite the
  // regular ones.
  i = incognito_settings_.find(host);
  if (i != incognito_settings_.end()) {
    for (int j = 0; j < CONTENT_SETTINGS_NUM_TYPES; ++j)
      if (i->second.content_settings.settings[j] != CONTENT_SETTING_DEFAULT)
        output.settings[j] = i->second.content_settings.settings[j];
  }

  // Match patterns starting with the most concrete pattern match.
  for (std::string key =
       std::string(ContentSettingsPattern::kDomainWildcard) + host; ; ) {
    HostContentSettings::const_iterator i(incognito_settings_.find(key));
    if (i != incognito_settings_.end()) {
      for (int j = 0; j < CONTENT_SETTINGS_NUM_TYPES; ++j) {
        if (output.settings[j] == CONTENT_SETTING_DEFAULT)
          output.settings[j] = i->second.content_settings.settings[j];
      }
    }
    i = host_content_settings_.find(key);
    if (i != host_content_settings_.end()) {
      for (int j = 0; j < CONTENT_SETTINGS_NUM_TYPES; ++j) {
        if (output.settings[j] == CONTENT_SETTING_DEFAULT)
          output.settings[j] = i->second.content_settings.settings[j];
      }
    }
    const size_t next_dot =
        key.find('.', ContentSettingsPattern::kDomainWildcardLength);
    if (next_dot == std::string::npos)
      break;
    key.erase(ContentSettingsPattern::kDomainWildcardLength,
              next_dot - ContentSettingsPattern::kDomainWildcardLength + 1);
  }

  return output;
}

void BaseProvider::UpdateContentSettingsMap(
    const ContentSettingsPattern& requesting_pattern,
    const ContentSettingsPattern& embedding_pattern,
    ContentSettingsType content_type,
    const ResourceIdentifier& resource_identifier,
    ContentSetting content_setting) {
  HostContentSettings* content_settings_map = host_content_settings();
  ExtendedContentSettings& extended_settings =
      (*content_settings_map)[requesting_pattern.ToString()];
  extended_settings.content_settings.settings[content_type] = content_setting;
}

}  // namespace content_settings
