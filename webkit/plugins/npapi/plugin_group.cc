// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "webkit/plugins/npapi/plugin_group.h"

#include "base/memory/linked_ptr.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/sys_string_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "base/version.h"
#include "webkit/plugins/npapi/plugin_list.h"
#include "webkit/plugins/webplugininfo.h"

namespace webkit {
namespace npapi {

// static
const char PluginGroup::kAdobeReaderGroupName[] = "Adobe Acrobat";
const char PluginGroup::kJavaGroupName[] = "Java";
const char PluginGroup::kQuickTimeGroupName[] = "QuickTime";
const char PluginGroup::kShockwaveGroupName[] = "Shockwave";
const char PluginGroup::kRealPlayerGroupName[] = "RealPlayer";
const char PluginGroup::kSilverlightGroupName[] = "Silverlight";
const char PluginGroup::kWindowsMediaPlayerGroupName[] = "Windows Media Player";

PluginGroup::PluginGroup(const string16& group_name,
                         const string16& name_matcher,
                         const std::string& identifier)
    : identifier_(identifier),
      group_name_(group_name),
      name_matcher_(name_matcher) {
}

void PluginGroup::InitFrom(const PluginGroup& other) {
  identifier_ = other.identifier_;
  group_name_ = other.group_name_;
  name_matcher_ = other.name_matcher_;
  web_plugin_infos_ = other.web_plugin_infos_;
}

PluginGroup::PluginGroup(const PluginGroup& other) {
  InitFrom(other);
}

PluginGroup& PluginGroup::operator=(const PluginGroup& other) {
  InitFrom(other);
  return *this;
}

/*static*/
PluginGroup* PluginGroup::FromPluginGroupDefinition(
    const PluginGroupDefinition& definition) {
  return new PluginGroup(ASCIIToUTF16(definition.name),
                                      ASCIIToUTF16(definition.name_matcher),
                                      definition.identifier);
}

PluginGroup::~PluginGroup() { }

/*static*/
std::string PluginGroup::GetIdentifier(const WebPluginInfo& wpi) {
#if defined(OS_POSIX)
  return wpi.path.BaseName().value();
#elif defined(OS_WIN)
  return base::SysWideToUTF8(wpi.path.BaseName().value());
#endif
}

/*static*/
std::string PluginGroup::GetLongIdentifier(const WebPluginInfo& wpi) {
#if defined(OS_POSIX)
  return wpi.path.value();
#elif defined(OS_WIN)
  return base::SysWideToUTF8(wpi.path.value());
#endif
}

/*static*/
PluginGroup* PluginGroup::FromWebPluginInfo(const WebPluginInfo& wpi) {
  // Create a matcher from the name of this plugin.
  return new PluginGroup(wpi.name, wpi.name,
                         GetIdentifier(wpi));
}

bool PluginGroup::Match(const WebPluginInfo& plugin) const {
  if (name_matcher_.empty()) {
    return false;
  }

  // Look for the name matcher anywhere in the plugin name.
  if (plugin.name.find(name_matcher_) == string16::npos) {
    return false;
  }

  return true;
}

/* static */
std::string PluginGroup::RemoveLeadingZerosFromVersionComponents(
    const std::string& version) {
  std::string no_leading_zeros_version;
  std::vector<std::string> numbers;
  base::SplitString(version, '.', &numbers);
  for (size_t i = 0; i < numbers.size(); ++i) {
    size_t n = numbers[i].size();
    size_t j = 0;
    while (j < n && numbers[i][j] == '0') {
      ++j;
    }
    no_leading_zeros_version += (j < n) ? numbers[i].substr(j) : "0";
    if (i != numbers.size() - 1) {
      no_leading_zeros_version += ".";
    }
  }

  return no_leading_zeros_version;
}

/* static */
void PluginGroup::CreateVersionFromString(const string16& version_string,
                                          Version* parsed_version) {
  // Remove spaces and ')' from the version string,
  // Replace any instances of 'r', ',' or '(' with a dot.
  std::string version = UTF16ToASCII(version_string);
  RemoveChars(version, ") ", &version);
  std::replace(version.begin(), version.end(), 'd', '.');
  std::replace(version.begin(), version.end(), 'r', '.');
  std::replace(version.begin(), version.end(), ',', '.');
  std::replace(version.begin(), version.end(), '(', '.');
  std::replace(version.begin(), version.end(), '_', '.');

  // Remove leading zeros from each of the version components.
  version = RemoveLeadingZerosFromVersionComponents(version);

  *parsed_version = Version(version);
}

void PluginGroup::AddPlugin(const WebPluginInfo& plugin) {
  // Check if this group already contains this plugin.
  for (size_t i = 0; i < web_plugin_infos_.size(); ++i) {
    if (FilePath::CompareEqualIgnoreCase(web_plugin_infos_[i].path.value(),
                                         plugin.path.value())) {
      return;
    }
  }
  web_plugin_infos_.push_back(plugin);
}

bool PluginGroup::RemovePlugin(const FilePath& filename) {
  bool did_remove = false;
  for (size_t i = 0; i < web_plugin_infos_.size();) {
    if (web_plugin_infos_[i].path == filename) {
      web_plugin_infos_.erase(web_plugin_infos_.begin() + i);
      did_remove = true;
    } else {
      i++;
    }
  }
  return did_remove;
}

string16 PluginGroup::GetGroupName() const {
  if (!group_name_.empty())
    return group_name_;
  DCHECK_EQ(1u, web_plugin_infos_.size());
  FilePath::StringType path =
      web_plugin_infos_[0].path.BaseName().RemoveExtension().value();
#if defined(OS_POSIX)
  return UTF8ToUTF16(path);
#elif defined(OS_WIN)
  return WideToUTF16(path);
#endif
}

bool PluginGroup::ContainsPlugin(const FilePath& path) const {
  for (size_t i = 0; i < web_plugin_infos_.size(); ++i) {
    if (web_plugin_infos_[i].path == path)
      return true;
  }
  return false;
}

bool PluginGroup::IsEmpty() const {
  return web_plugin_infos_.empty();
}

}  // namespace npapi
}  // namespace webkit
