// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/npapi/plugin_list.h"

#import <Foundation/Foundation.h>

#include "base/file_util.h"
#include "base/mac/mac_util.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "webkit/plugins/npapi/plugin_lib.h"

namespace webkit {
namespace npapi {

namespace {

void GetPluginCommonDirectory(std::vector<FilePath>* plugin_dirs,
                              bool user) {
  // Note that there are no NSSearchPathDirectory constants for these
  // directories so we can't use Cocoa's NSSearchPathForDirectoriesInDomains().
  // Interestingly, Safari hard-codes the location (see
  // WebKit/WebKit/mac/Plugins/WebPluginDatabase.mm's +_defaultPlugInPaths).
  FSRef ref;
  OSErr err = FSFindFolder(user ? kUserDomain : kLocalDomain,
                           kInternetPlugInFolderType, false, &ref);

  if (err)
    return;

  plugin_dirs->push_back(FilePath(base::mac::PathFromFSRef(ref)));
}

// Returns true if the plugin should be prevented from loading.
bool IsBlacklistedPlugin(const WebPluginInfo& info) {
  // We blacklist Gears by included MIME type, since that is more stable than
  // its name. Be careful about adding any more plugins to this list though,
  // since it's easy to accidentally blacklist plugins that support lots of
  // MIME types.
  for (std::vector<WebPluginMimeType>::const_iterator i =
           info.mime_types.begin(); i != info.mime_types.end(); ++i) {
    // The Gears plugin is Safari-specific, so don't load it.
    if (i->mime_type == "application/x-googlegears")
      return true;
  }

  // Versions of Flip4Mac 2.3 before 2.3.6 often hang the renderer, so don't
  // load them.
  if (StartsWith(info.name, ASCIIToUTF16("Flip4Mac Windows Media"), false) &&
      StartsWith(info.version, ASCIIToUTF16("2.3"), false)) {
    std::vector<string16> components;
    base::SplitString(info.version, '.', &components);
    int bugfix_version = 0;
    return (components.size() >= 3 &&
            base::StringToInt(components[2], &bugfix_version) &&
            bugfix_version < 6);
  }

  return false;
}

}  // namespace

void PluginList::PlatformInit() {
}

void PluginList::GetPluginDirectories(std::vector<FilePath>* plugin_dirs) {
  // Load from the user's area
  GetPluginCommonDirectory(plugin_dirs, true);

  // Load from the machine-wide area
  GetPluginCommonDirectory(plugin_dirs, false);

  // 10.5 includes the Java2 plugin, but as of Java for Mac OS X 10.5 Update 10
  // no longer has a symlink to it in the Internet Plug-Ins directory.
  // Manually include it since there's no other way to support Java.
  if (base::mac::IsOSLeopard()) {
    plugin_dirs->push_back(FilePath(
        "/System/Library/Java/Support/Deploy.bundle/Contents/Resources"));
  }
}

void PluginList::GetPluginsInDir(
    const FilePath& path, std::vector<FilePath>* plugins) {
  file_util::FileEnumerator enumerator(path,
                                       false, // not recursive
                                       file_util::FileEnumerator::DIRECTORIES);
  for (FilePath path = enumerator.Next(); !path.value().empty();
       path = enumerator.Next()) {
    plugins->push_back(path);
  }
}

bool PluginList::ShouldLoadPlugin(const WebPluginInfo& info,
                                  ScopedVector<PluginGroup>* plugin_groups) {
  if (IsBlacklistedPlugin(info))
    return false;

  // Hierarchy check
  // (we're loading plugins hierarchically from Library folders, so plugins we
  //  encounter earlier must override plugins we encounter later)
  for (size_t i = 0; i < plugin_groups->size(); ++i) {
    const std::vector<WebPluginInfo>& plugins =
        (*plugin_groups)[i]->web_plugin_infos();
    for (size_t j = 0; j < plugins.size(); ++j) {
      if (plugins[j].path.BaseName() == info.path.BaseName()) {
        return false;  // Already have a loaded plugin higher in the hierarchy.
      }
    }
  }

  return true;
}

}  // namespace npapi
}  // namespace webkit
