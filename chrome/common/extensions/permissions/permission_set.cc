// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/permissions/permission_set.h"

#include <algorithm>
#include <iterator>
#include <string>

#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/permissions/permissions_info.h"
#include "chrome/common/extensions/url_pattern.h"
#include "chrome/common/extensions/url_pattern_set.h"
#include "content/public/common/url_constants.h"
#include "grit/generated_resources.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

// Helper for GetDistinctHosts(): com > net > org > everything else.
bool RcdBetterThan(const std::string& a, const std::string& b) {
  if (a == b)
    return false;
  if (a == "com")
    return true;
  if (a == "net")
    return b != "com";
  if (a == "org")
    return b != "com" && b != "net";
  return false;
}

// Names of API modules that can be used without listing it in the
// permissions section of the manifest.
const char* kNonPermissionModuleNames[] = {
  "app",
  "browserAction",
  "devtools",
  "events",
  "extension",
  "i18n",
  "omnibox",
  "pageAction",
  "pageActions",
  "permissions",
  "runtime",
  "scriptBadge",
  "test",
  "types"
};
const size_t kNumNonPermissionModuleNames =
    arraysize(kNonPermissionModuleNames);

// Names of functions (within modules requiring permissions) that can be used
// without asking for the module permission. In other words, functions you can
// use with no permissions specified.
const char* kNonPermissionFunctionNames[] = {
  "management.getPermissionWarningsByManifest",
  "tabs.create",
  "tabs.onRemoved",
  "tabs.remove",
  "tabs.update",
};
const size_t kNumNonPermissionFunctionNames =
    arraysize(kNonPermissionFunctionNames);

void AddPatternsAndRemovePaths(const URLPatternSet& set, URLPatternSet* out) {
  DCHECK(out);
  for (URLPatternSet::const_iterator i = set.begin(); i != set.end(); ++i) {
    URLPattern p = *i;
    p.SetPath("/*");
    out->AddPattern(p);
  }
}

// Strips out the API name from a function or event name.
// Functions will be of the form api_name.function
// Events will be of the form api_name/id or api_name.optional.stuff
std::string GetPermissionName(const std::string& function_name) {
  size_t separator = function_name.find_first_of("./");
  if (separator != std::string::npos)
    return function_name.substr(0, separator);
  else
    return function_name;
}

}  // namespace

namespace extensions {

//
// PermissionSet
//

PermissionSet::PermissionSet() {}

PermissionSet::PermissionSet(
    const extensions::Extension* extension,
    const APIPermissionSet& apis,
    const URLPatternSet& explicit_hosts)
    : apis_(apis) {
  DCHECK(extension);
  AddPatternsAndRemovePaths(explicit_hosts, &explicit_hosts_);
  InitImplicitExtensionPermissions(extension);
  InitImplicitPermissions();
  InitEffectiveHosts();
}

PermissionSet::PermissionSet(
    const APIPermissionSet& apis,
    const URLPatternSet& explicit_hosts,
    const URLPatternSet& scriptable_hosts)
    : apis_(apis),
      scriptable_hosts_(scriptable_hosts) {
  AddPatternsAndRemovePaths(explicit_hosts, &explicit_hosts_);
  InitImplicitPermissions();
  InitEffectiveHosts();
}

// static
PermissionSet* PermissionSet::CreateDifference(
    const PermissionSet* set1,
    const PermissionSet* set2) {
  scoped_refptr<PermissionSet> empty = new PermissionSet();
  const PermissionSet* set1_safe = (set1 == NULL) ? empty : set1;
  const PermissionSet* set2_safe = (set2 == NULL) ? empty : set2;

  APIPermissionSet apis;
  std::set_difference(set1_safe->apis().begin(), set1_safe->apis().end(),
                      set2_safe->apis().begin(), set2_safe->apis().end(),
                      std::insert_iterator<APIPermissionSet>(
                          apis, apis.begin()));

  URLPatternSet explicit_hosts;
  URLPatternSet::CreateDifference(set1_safe->explicit_hosts(),
                                  set2_safe->explicit_hosts(),
                                  &explicit_hosts);

  URLPatternSet scriptable_hosts;
  URLPatternSet::CreateDifference(set1_safe->scriptable_hosts(),
                                  set2_safe->scriptable_hosts(),
                                  &scriptable_hosts);

  return new PermissionSet(apis, explicit_hosts, scriptable_hosts);
}

// static
PermissionSet* PermissionSet::CreateIntersection(
    const PermissionSet* set1,
    const PermissionSet* set2) {
  scoped_refptr<PermissionSet> empty = new PermissionSet();
  const PermissionSet* set1_safe = (set1 == NULL) ? empty : set1;
  const PermissionSet* set2_safe = (set2 == NULL) ? empty : set2;

  APIPermissionSet apis;
  std::set_intersection(set1_safe->apis().begin(), set1_safe->apis().end(),
                        set2_safe->apis().begin(), set2_safe->apis().end(),
                        std::insert_iterator<APIPermissionSet>(
                            apis, apis.begin()));
  URLPatternSet explicit_hosts;
  URLPatternSet::CreateIntersection(set1_safe->explicit_hosts(),
                                    set2_safe->explicit_hosts(),
                                    &explicit_hosts);

  URLPatternSet scriptable_hosts;
  URLPatternSet::CreateIntersection(set1_safe->scriptable_hosts(),
                                    set2_safe->scriptable_hosts(),
                                    &scriptable_hosts);

  return new PermissionSet(apis, explicit_hosts, scriptable_hosts);
}

// static
PermissionSet* PermissionSet::CreateUnion(
    const PermissionSet* set1,
    const PermissionSet* set2) {
  scoped_refptr<PermissionSet> empty = new PermissionSet();
  const PermissionSet* set1_safe = (set1 == NULL) ? empty : set1;
  const PermissionSet* set2_safe = (set2 == NULL) ? empty : set2;

  APIPermissionSet apis;
  std::set_union(set1_safe->apis().begin(), set1_safe->apis().end(),
                 set2_safe->apis().begin(), set2_safe->apis().end(),
                 std::insert_iterator<APIPermissionSet>(
                     apis, apis.begin()));

  URLPatternSet explicit_hosts;
  URLPatternSet::CreateUnion(set1_safe->explicit_hosts(),
                             set2_safe->explicit_hosts(),
                             &explicit_hosts);

  URLPatternSet scriptable_hosts;
  URLPatternSet::CreateUnion(set1_safe->scriptable_hosts(),
                             set2_safe->scriptable_hosts(),
                             &scriptable_hosts);

  return new PermissionSet(apis, explicit_hosts, scriptable_hosts);
}

bool PermissionSet::operator==(
    const PermissionSet& rhs) const {
  return apis_ == rhs.apis_ &&
      scriptable_hosts_ == rhs.scriptable_hosts_ &&
      explicit_hosts_ == rhs.explicit_hosts_;
}

bool PermissionSet::Contains(const PermissionSet& set) const {
  // Every set includes the empty set.
  if (set.IsEmpty())
    return true;

  if (!std::includes(apis_.begin(), apis_.end(),
                     set.apis().begin(), set.apis().end()))
    return false;

  if (!explicit_hosts().Contains(set.explicit_hosts()))
    return false;

  if (!scriptable_hosts().Contains(set.scriptable_hosts()))
    return false;

  return true;
}

std::set<std::string> PermissionSet::GetAPIsAsStrings() const {
  PermissionsInfo* info = PermissionsInfo::GetInstance();
  std::set<std::string> apis_str;
  for (APIPermissionSet::const_iterator i = apis_.begin();
       i != apis_.end(); ++i) {
    APIPermission* permission = info->GetByID(*i);
    if (permission)
      apis_str.insert(permission->name());
  }
  return apis_str;
}

std::set<std::string> PermissionSet::
    GetAPIsWithAnyAccessAsStrings() const {
  std::set<std::string> result = GetAPIsAsStrings();
  for (size_t i = 0; i < kNumNonPermissionModuleNames; ++i)
    result.insert(kNonPermissionModuleNames[i]);
  for (size_t i = 0; i < kNumNonPermissionFunctionNames; ++i)
    result.insert(GetPermissionName(kNonPermissionFunctionNames[i]));
  return result;
}

bool PermissionSet::HasAnyAccessToAPI(
    const std::string& api_name) const {
  if (HasAccessToFunction(api_name))
    return true;

  for (size_t i = 0; i < kNumNonPermissionFunctionNames; ++i) {
    if (api_name == GetPermissionName(kNonPermissionFunctionNames[i]))
      return true;
  }

  return false;
}

std::set<std::string>
    PermissionSet::GetDistinctHostsForDisplay() const {
  return GetDistinctHosts(effective_hosts_, true, true);
}

PermissionMessages PermissionSet::GetPermissionMessages(
    Extension::Type extension_type) const {
  PermissionMessages messages;

  if (HasEffectiveFullAccess()) {
    messages.push_back(PermissionMessage(
        PermissionMessage::kFullAccess,
        l10n_util::GetStringUTF16(IDS_EXTENSION_PROMPT_WARNING_FULL_ACCESS)));
    return messages;
  }

  // Since platform apps always use isolated storage, they can't (silently)
  // access user data on other domains, so there's no need to prompt.
  if (extension_type != Extension::TYPE_PLATFORM_APP) {
    if (HasEffectiveAccessToAllHosts()) {
      messages.push_back(PermissionMessage(
          PermissionMessage::kHostsAll,
          l10n_util::GetStringUTF16(IDS_EXTENSION_PROMPT_WARNING_ALL_HOSTS)));
    } else {
      std::set<std::string> hosts = GetDistinctHostsForDisplay();
      if (!hosts.empty())
        messages.push_back(PermissionMessage::CreateFromHostList(hosts));
    }
  }

  std::set<PermissionMessage> simple_msgs =
      GetSimplePermissionMessages();
  messages.insert(messages.end(), simple_msgs.begin(), simple_msgs.end());

  return messages;
}

std::vector<string16> PermissionSet::GetWarningMessages(
    Extension::Type extension_type) const {
  std::vector<string16> messages;
  PermissionMessages permissions = GetPermissionMessages(extension_type);

  bool audio_capture = false;
  bool video_capture = false;
  for (PermissionMessages::const_iterator i = permissions.begin();
       i != permissions.end(); ++i) {
    if (i->id() == PermissionMessage::kAudioCapture)
      audio_capture = true;
    if (i->id() == PermissionMessage::kVideoCapture)
      video_capture = true;
  }

  for (PermissionMessages::const_iterator i = permissions.begin();
       i != permissions.end(); ++i) {
    if (audio_capture && video_capture) {
      if (i->id() == PermissionMessage::kAudioCapture) {
        messages.push_back(l10n_util::GetStringUTF16(
            IDS_EXTENSION_PROMPT_WARNING_AUDIO_AND_VIDEO_CAPTURE));
        continue;
      } else if (i->id() == PermissionMessage::kVideoCapture) {
        // The combined message will be pushed above.
        continue;
      }
    }

    messages.push_back(i->message());
  }

  return messages;
}

bool PermissionSet::IsEmpty() const {
  // Not default if any host permissions are present.
  if (!(explicit_hosts().is_empty() && scriptable_hosts().is_empty()))
    return false;

  // Or if it has no api permissions.
  return apis().empty();
}

bool PermissionSet::HasAPIPermission(
    APIPermission::ID permission) const {
  return apis().find(permission) != apis().end();
}

bool PermissionSet::HasAccessToFunction(
    const std::string& function_name) const {
  // TODO(jstritar): Embed this information in each permission and add a method
  // like GrantsAccess(function_name) to APIPermission. A "default"
  // permission can then handle the modules and functions that everyone can
  // access.
  for (size_t i = 0; i < kNumNonPermissionFunctionNames; ++i) {
    if (function_name == kNonPermissionFunctionNames[i])
      return true;
  }

  std::string permission_name = GetPermissionName(function_name);
  APIPermission* permission =
      PermissionsInfo::GetInstance()->GetByName(permission_name);
  if (permission && apis_.count(permission->id()))
    return true;

  for (size_t i = 0; i < kNumNonPermissionModuleNames; ++i) {
    if (permission_name == kNonPermissionModuleNames[i]) {
      return true;
    }
  }

  return false;
}

bool PermissionSet::HasExplicitAccessToOrigin(
    const GURL& origin) const {
  return explicit_hosts().MatchesURL(origin);
}

bool PermissionSet::HasScriptableAccessToURL(
    const GURL& origin) const {
  // We only need to check our host list to verify access. The host list should
  // already reflect any special rules (such as chrome://favicon, all hosts
  // access, etc.).
  return scriptable_hosts().MatchesURL(origin);
}

bool PermissionSet::HasEffectiveAccessToAllHosts() const {
  // There are two ways this set can have effective access to all hosts:
  //  1) it has an <all_urls> URL pattern.
  //  2) it has a named permission with implied full URL access.
  for (URLPatternSet::const_iterator host = effective_hosts().begin();
       host != effective_hosts().end(); ++host) {
    if (host->match_all_urls() ||
        (host->match_subdomains() && host->host().empty()))
      return true;
  }

  PermissionsInfo* info = PermissionsInfo::GetInstance();
  for (APIPermissionSet::const_iterator i = apis().begin();
       i != apis().end(); ++i) {
    APIPermission* permission = info->GetByID(*i);
    if (permission->implies_full_url_access())
      return true;
  }
  return false;
}

bool PermissionSet::HasEffectiveAccessToURL(
    const GURL& url) const {
  return effective_hosts().MatchesURL(url);
}

bool PermissionSet::HasEffectiveFullAccess() const {
  PermissionsInfo* info = PermissionsInfo::GetInstance();
  for (APIPermissionSet::const_iterator i = apis().begin();
       i != apis().end(); ++i) {
    APIPermission* permission = info->GetByID(*i);
    if (permission->implies_full_access())
      return true;
  }
  return false;
}

bool PermissionSet::HasLessPrivilegesThan(
    const PermissionSet* permissions) const {
  // Things can't get worse than native code access.
  if (HasEffectiveFullAccess())
    return false;

  // Otherwise, it's a privilege increase if the new one has full access.
  if (permissions->HasEffectiveFullAccess())
    return true;

  if (HasLessHostPrivilegesThan(permissions))
    return true;

  if (HasLessAPIPrivilegesThan(permissions))
    return true;

  return false;
}

PermissionSet::~PermissionSet() {}

// static
std::set<std::string> PermissionSet::GetDistinctHosts(
    const URLPatternSet& host_patterns,
    bool include_rcd,
    bool exclude_file_scheme) {
  // Use a vector to preserve order (also faster than a map on small sets).
  // Each item is a host split into two parts: host without RCDs and
  // current best RCD.
  typedef std::vector<std::pair<std::string, std::string> > HostVector;
  HostVector hosts_best_rcd;
  for (URLPatternSet::const_iterator i = host_patterns.begin();
       i != host_patterns.end(); ++i) {
    if (exclude_file_scheme && i->scheme() == chrome::kFileScheme)
      continue;

    std::string host = i->host();

    // Add the subdomain wildcard back to the host, if necessary.
    if (i->match_subdomains())
      host = "*." + host;

    // If the host has an RCD, split it off so we can detect duplicates.
    std::string rcd;
    size_t reg_len = net::RegistryControlledDomainService::GetRegistryLength(
        host, false);
    if (reg_len && reg_len != std::string::npos) {
      if (include_rcd)  // else leave rcd empty
        rcd = host.substr(host.size() - reg_len);
      host = host.substr(0, host.size() - reg_len);
    }

    // Check if we've already seen this host.
    HostVector::iterator it = hosts_best_rcd.begin();
    for (; it != hosts_best_rcd.end(); ++it) {
      if (it->first == host)
        break;
    }
    // If this host was found, replace the RCD if this one is better.
    if (it != hosts_best_rcd.end()) {
      if (include_rcd && RcdBetterThan(rcd, it->second))
        it->second = rcd;
    } else {  // Previously unseen host, append it.
      hosts_best_rcd.push_back(std::make_pair(host, rcd));
    }
  }

  // Build up the final vector by concatenating hosts and RCDs.
  std::set<std::string> distinct_hosts;
  for (HostVector::iterator it = hosts_best_rcd.begin();
       it != hosts_best_rcd.end(); ++it)
    distinct_hosts.insert(it->first + it->second);
  return distinct_hosts;
}

void PermissionSet::InitImplicitPermissions() {
  // The webRequest permission implies the internal version as well.
  if (apis_.find(APIPermission::kWebRequest) != apis_.end())
    apis_.insert(APIPermission::kWebRequestInternal);

  // The fileBrowserHandler permission implies the internal version as well.
  if (apis_.find(APIPermission::kFileBrowserHandler) != apis_.end())
    apis_.insert(APIPermission::kFileBrowserHandlerInternal);

  // mediaGalleriesRead implies the mediaGalleries permission.
  if (apis_.find(APIPermission::kMediaGalleriesRead) != apis_.end())
    apis_.insert(APIPermission::kMediaGalleries);
}

void PermissionSet::InitImplicitExtensionPermissions(
    const extensions::Extension* extension) {
  // Add the implied permissions.
  if (!extension->plugins().empty())
    apis_.insert(APIPermission::kPlugin);

  if (!extension->devtools_url().is_empty())
    apis_.insert(APIPermission::kDevtools);

  // Add the scriptable hosts.
  for (extensions::UserScriptList::const_iterator content_script =
           extension->content_scripts().begin();
       content_script != extension->content_scripts().end(); ++content_script) {
    URLPatternSet::const_iterator pattern =
        content_script->url_patterns().begin();
    for (; pattern != content_script->url_patterns().end(); ++pattern)
      scriptable_hosts_.AddPattern(*pattern);
  }
}

void PermissionSet::InitEffectiveHosts() {
  effective_hosts_.ClearPatterns();

  URLPatternSet::CreateUnion(
      explicit_hosts(), scriptable_hosts(), &effective_hosts_);
}

std::set<PermissionMessage>
    PermissionSet::GetSimplePermissionMessages() const {
  std::set<PermissionMessage> messages;
  PermissionsInfo* info = PermissionsInfo::GetInstance();
  for (APIPermissionSet::const_iterator i = apis_.begin();
       i != apis_.end(); ++i) {
    DCHECK_GT(PermissionMessage::kNone,
              PermissionMessage::kUnknown);
    APIPermission* perm = info->GetByID(*i);
    if (perm && perm->message_id() > PermissionMessage::kNone)
      messages.insert(perm->GetMessage_());
  }
  return messages;
}

bool PermissionSet::HasLessAPIPrivilegesThan(
    const PermissionSet* permissions) const {
  if (permissions == NULL)
    return false;

  std::set<PermissionMessage> current_warnings =
      GetSimplePermissionMessages();
  std::set<PermissionMessage> new_warnings =
      permissions->GetSimplePermissionMessages();
  std::set<PermissionMessage> delta_warnings;
  std::set_difference(new_warnings.begin(), new_warnings.end(),
                      current_warnings.begin(), current_warnings.end(),
                      std::inserter(delta_warnings, delta_warnings.begin()));

  // We have less privileges if there are additional warnings present.
  return !delta_warnings.empty();
}

bool PermissionSet::HasLessHostPrivilegesThan(
    const PermissionSet* permissions) const {
  // If this permission set can access any host, then it can't be elevated.
  if (HasEffectiveAccessToAllHosts())
    return false;

  // Likewise, if the other permission set has full host access, then it must be
  // a privilege increase.
  if (permissions->HasEffectiveAccessToAllHosts())
    return true;

  const URLPatternSet& old_list = effective_hosts();
  const URLPatternSet& new_list = permissions->effective_hosts();

  // TODO(jstritar): This is overly conservative with respect to subdomains.
  // For example, going from *.google.com to www.google.com will be
  // considered an elevation, even though it is not (http://crbug.com/65337).
  std::set<std::string> new_hosts_set(GetDistinctHosts(new_list, false, false));
  std::set<std::string> old_hosts_set(GetDistinctHosts(old_list, false, false));
  std::set<std::string> new_hosts_only;

  std::set_difference(new_hosts_set.begin(), new_hosts_set.end(),
                      old_hosts_set.begin(), old_hosts_set.end(),
                      std::inserter(new_hosts_only, new_hosts_only.begin()));

  return !new_hosts_only.empty();
}

}  // namespace extensions
