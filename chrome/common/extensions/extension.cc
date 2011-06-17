// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/extension.h"

#include <algorithm>

#include "base/base64.h"
#include "base/basictypes.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/i18n/rtl.h"
#include "base/logging.h"
#include "base/memory/singleton.h"
#include "base/stl_util-inl.h"
#include "base/string16.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "base/version.h"
#include "crypto/sha2.h"
#include "crypto/third_party/nss/blapi.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/extensions/extension_action.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_error_utils.h"
#include "chrome/common/extensions/extension_l10n_util.h"
#include "chrome/common/extensions/extension_resource.h"
#include "chrome/common/extensions/extension_sidebar_defaults.h"
#include "chrome/common/extensions/extension_sidebar_utils.h"
#include "chrome/common/extensions/file_browser_handler.h"
#include "chrome/common/extensions/user_script.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/url_util.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "net/base/registry_controlled_domain.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "webkit/glue/image_decoder.h"

namespace keys = extension_manifest_keys;
namespace values = extension_manifest_values;
namespace errors = extension_manifest_errors;

namespace {

const int kPEMOutputColumns = 65;

// KEY MARKERS
const char kKeyBeginHeaderMarker[] = "-----BEGIN";
const char kKeyBeginFooterMarker[] = "-----END";
const char kKeyInfoEndMarker[] = "KEY-----";
const char kPublic[] = "PUBLIC";
const char kPrivate[] = "PRIVATE";

const int kRSAKeySize = 1024;

// Converts a normal hexadecimal string into the alphabet used by extensions.
// We use the characters 'a'-'p' instead of '0'-'f' to avoid ever having a
// completely numeric host, since some software interprets that as an IP
// address.
static void ConvertHexadecimalToIDAlphabet(std::string* id) {
  for (size_t i = 0; i < id->size(); ++i) {
    int val;
    if (base::HexStringToInt(id->begin() + i, id->begin() + i + 1, &val))
      (*id)[i] = val + 'a';
    else
      (*id)[i] = 'a';
  }
}

// These keys are allowed by all crx files (apps, extensions, themes, etc).
static const char* kBaseCrxKeys[] = {
  keys::kCurrentLocale,
  keys::kDefaultLocale,
  keys::kDescription,
  keys::kIcons,
  keys::kName,
  keys::kPublicKey,
  keys::kSignature,
  keys::kVersion,
  keys::kUpdateURL
};

bool IsBaseCrxKey(const std::string& key) {
  for (size_t i = 0; i < arraysize(kBaseCrxKeys); ++i) {
    if (key == kBaseCrxKeys[i])
      return true;
  }

  return false;
}

// Constant used to represent an undefined l10n message id.
const int kUndefinedMessageId = -1;

// Names of API modules that do not require a permission.
const char kBrowserActionModuleName[] = "browserAction";
const char kBrowserActionsModuleName[] = "browserActions";
const char kDevToolsModuleName[] = "devtools";
const char kExtensionModuleName[] = "extension";
const char kI18NModuleName[] = "i18n";
const char kOmniboxModuleName[] = "omnibox";
const char kPageActionModuleName[] = "pageAction";
const char kPageActionsModuleName[] = "pageActions";
const char kTestModuleName[] = "test";

// Names of modules that can be used without listing it in the permissions
// section of the manifest.
const char* kNonPermissionModuleNames[] = {
  kBrowserActionModuleName,
  kBrowserActionsModuleName,
  kDevToolsModuleName,
  kExtensionModuleName,
  kI18NModuleName,
  kOmniboxModuleName,
  kPageActionModuleName,
  kPageActionsModuleName,
  kTestModuleName
};
const size_t kNumNonPermissionModuleNames =
    arraysize(kNonPermissionModuleNames);

// Names of functions (within modules requiring permissions) that can be used
// without asking for the module permission. In other words, functions you can
// use with no permissions specified.
const char* kNonPermissionFunctionNames[] = {
  "tabs.create",
  "tabs.update"
};
const size_t kNumNonPermissionFunctionNames =
    arraysize(kNonPermissionFunctionNames);

// A singleton object containing global data needed by the extension objects.
class ExtensionConfig {
 public:
  static ExtensionConfig* GetInstance() {
    return Singleton<ExtensionConfig>::get();
  }

  Extension::PermissionMessage::MessageId GetPermissionMessageId(
      const std::string& permission) {
    return Extension::kPermissions[permission_map_[permission]].message_id;
  }

  Extension::ScriptingWhitelist* whitelist() { return &scripting_whitelist_; }

 private:
  friend struct DefaultSingletonTraits<ExtensionConfig>;

  ExtensionConfig() {
    for (size_t i = 0; i < Extension::kNumPermissions; ++i)
      permission_map_[Extension::kPermissions[i].name] = i;
  };

  ~ExtensionConfig() { }

  std::map<const std::string, size_t> permission_map_;

  // A whitelist of extensions that can script anywhere. Do not add to this
  // list (except in tests) without consulting the Extensions team first.
  // Note: Component extensions have this right implicitly and do not need to be
  // added to this list.
  Extension::ScriptingWhitelist scripting_whitelist_;
};

// Aliased to kTabPermission for purposes of API checks, but not allowed
// in the permissions field of the manifest.
static const char kWindowPermission[] = "windows";

// Rank extension locations in a way that allows
// Extension::GetHigherPriorityLocation() to compare locations.
// An extension installed from two locations will have the location
// with the higher rank, as returned by this function. The actual
// integer values may change, and should never be persisted.
int GetLocationRank(Extension::Location location) {
  const int kInvalidRank = -1;
  int rank = kInvalidRank;  // Will CHECK that rank is not kInvalidRank.

  switch (location) {
    // Component extensions can not be overriden by any other type.
    case Extension::COMPONENT:
      rank = 6;
      break;

    // Policy controlled extensions may not be overridden by any type
    // that is not part of chrome.
    case Extension::EXTERNAL_POLICY_DOWNLOAD:
      rank = 5;
      break;

    // A developer-loaded extension should override any installed type
    // that a user can disable.
    case Extension::LOAD:
      rank = 4;
      break;

    // The relative priority of various external sources is not important,
    // but having some order ensures deterministic behavior.
    case Extension::EXTERNAL_REGISTRY:
      rank = 3;
      break;

    case Extension::EXTERNAL_PREF:
      rank = 2;
      break;

    case Extension::EXTERNAL_PREF_DOWNLOAD:
      rank = 1;
      break;

    // User installed extensions are overridden by any external type.
    case Extension::INTERNAL:
      rank = 0;
      break;

    default:
      NOTREACHED() << "Need to add new extension locaton " << location;
  }

  CHECK(rank != kInvalidRank);
  return rank;
}

}  // namespace

const FilePath::CharType Extension::kManifestFilename[] =
    FILE_PATH_LITERAL("manifest.json");
const FilePath::CharType Extension::kLocaleFolder[] =
    FILE_PATH_LITERAL("_locales");
const FilePath::CharType Extension::kMessagesFilename[] =
    FILE_PATH_LITERAL("messages.json");

#if defined(OS_WIN)
const char Extension::kExtensionRegistryPath[] =
    "Software\\Google\\Chrome\\Extensions";
#endif

// first 16 bytes of SHA256 hashed public key.
const size_t Extension::kIdSize = 16;

const char Extension::kMimeType[] = "application/x-chrome-extension";

const int Extension::kIconSizes[] = {
  EXTENSION_ICON_LARGE,
  EXTENSION_ICON_MEDIUM,
  EXTENSION_ICON_SMALL,
  EXTENSION_ICON_SMALLISH,
  EXTENSION_ICON_BITTY
};

const int Extension::kPageActionIconMaxSize = 19;
const int Extension::kBrowserActionIconMaxSize = 19;
const int Extension::kSidebarIconMaxSize = 16;

// Explicit permissions -- permission declaration required.
const char Extension::kBackgroundPermission[] = "background";
const char Extension::kBookmarkPermission[] = "bookmarks";
const char Extension::kContextMenusPermission[] = "contextMenus";
const char Extension::kContentSettingsPermission[] = "contentSettings";
const char Extension::kCookiePermission[] = "cookies";
const char Extension::kChromeosInfoPrivatePermissions[] = "chromeosInfoPrivate";
const char Extension::kDebuggerPermission[] = "debugger";
const char Extension::kExperimentalPermission[] = "experimental";
const char Extension::kFileBrowserHandlerPermission[] = "fileBrowserHandler";
const char Extension::kFileBrowserPrivatePermission[] = "fileBrowserPrivate";
const char Extension::kGeolocationPermission[] = "geolocation";
const char Extension::kHistoryPermission[] = "history";
const char Extension::kIdlePermission[] = "idle";
const char Extension::kManagementPermission[] = "management";
const char Extension::kNotificationPermission[] = "notifications";
const char Extension::kProxyPermission[] = "proxy";
const char Extension::kTabPermission[] = "tabs";
const char Extension::kUnlimitedStoragePermission[] = "unlimitedStorage";
const char Extension::kWebstorePrivatePermission[] = "webstorePrivate";

// In general, all permissions should have an install message.
// See ExtensionsTest.PermissionMessages for an explanation of each
// exception.
const Extension::Permission Extension::kPermissions[] = {
  { kBackgroundPermission,           PermissionMessage::ID_NONE },
  { kBookmarkPermission,             PermissionMessage::ID_BOOKMARKS },
  { kChromeosInfoPrivatePermissions, PermissionMessage::ID_NONE },
  { kContentSettingsPermission,      PermissionMessage::ID_NONE },
  { kContextMenusPermission,         PermissionMessage::ID_NONE },
  { kCookiePermission,               PermissionMessage::ID_NONE },
  { kDebuggerPermission,             PermissionMessage::ID_DEBUGGER },
  { kExperimentalPermission,         PermissionMessage::ID_NONE },
  { kFileBrowserHandlerPermission,   PermissionMessage::ID_NONE },
  { kFileBrowserPrivatePermission,   PermissionMessage::ID_NONE },
  { kGeolocationPermission,          PermissionMessage::ID_GEOLOCATION },
  { kIdlePermission,                 PermissionMessage::ID_NONE },
  { kHistoryPermission,              PermissionMessage::ID_BROWSING_HISTORY },
  { kManagementPermission,           PermissionMessage::ID_MANAGEMENT },
  { kNotificationPermission,         PermissionMessage::ID_NONE },
  { kProxyPermission,                PermissionMessage::ID_NONE },
  { kTabPermission,                  PermissionMessage::ID_TABS },
  { kUnlimitedStoragePermission,     PermissionMessage::ID_NONE },
  { kWebstorePrivatePermission,      PermissionMessage::ID_NONE }
};
const size_t Extension::kNumPermissions =
    arraysize(Extension::kPermissions);

const char* const Extension::kHostedAppPermissionNames[] = {
  Extension::kBackgroundPermission,
  Extension::kGeolocationPermission,
  Extension::kNotificationPermission,
  Extension::kUnlimitedStoragePermission,
  Extension::kWebstorePrivatePermission,
};
const size_t Extension::kNumHostedAppPermissions =
    arraysize(Extension::kHostedAppPermissionNames);

const char* const Extension::kComponentPrivatePermissionNames[] = {
    Extension::kFileBrowserPrivatePermission,
    Extension::kWebstorePrivatePermission,
    Extension::kChromeosInfoPrivatePermissions,
};
const size_t Extension::kNumComponentPrivatePermissions =
    arraysize(Extension::kComponentPrivatePermissionNames);

// We purposefully don't put this into kPermissionNames.
const char Extension::kOldUnlimitedStoragePermission[] = "unlimited_storage";

const int Extension::kValidWebExtentSchemes =
    URLPattern::SCHEME_HTTP | URLPattern::SCHEME_HTTPS;

const int Extension::kValidHostPermissionSchemes =
    UserScript::kValidUserScriptSchemes | URLPattern::SCHEME_CHROMEUI;

//
// PermissionMessage
//

// static
Extension::PermissionMessage Extension::PermissionMessage::CreateFromMessageId(
    Extension::PermissionMessage::MessageId message_id) {
  DCHECK_GT(PermissionMessage::ID_NONE, PermissionMessage::ID_UNKNOWN);
  if (message_id <= ID_NONE)
    return PermissionMessage(message_id, string16());

  string16 message = l10n_util::GetStringUTF16(kMessageIds[message_id]);
  return PermissionMessage(message_id, message);
}

// static
Extension::PermissionMessage Extension::PermissionMessage::CreateFromHostList(
    const std::vector<std::string> hosts) {
  CHECK(hosts.size() > 0);

  MessageId message_id;
  string16 message;
  switch (hosts.size()) {
    case 1:
      message_id = ID_HOSTS_1;
      message = l10n_util::GetStringFUTF16(kMessageIds[message_id],
                                           UTF8ToUTF16(hosts[0]));
      break;
    case 2:
      message_id = ID_HOSTS_2;
      message = l10n_util::GetStringFUTF16(kMessageIds[message_id],
                                           UTF8ToUTF16(hosts[0]),
                                           UTF8ToUTF16(hosts[1]));
      break;
    case 3:
      message_id = ID_HOSTS_3;
      message = l10n_util::GetStringFUTF16(kMessageIds[message_id],
                                           UTF8ToUTF16(hosts[0]),
                                           UTF8ToUTF16(hosts[1]),
                                           UTF8ToUTF16(hosts[2]));
      break;
    default:
      message_id = ID_HOSTS_4_OR_MORE;
      message = l10n_util::GetStringFUTF16(
          kMessageIds[message_id],
          UTF8ToUTF16(hosts[0]),
          UTF8ToUTF16(hosts[1]),
          base::IntToString16(hosts.size() - 2));
      break;
  }

  return PermissionMessage(message_id, message);
}

Extension::PermissionMessage::PermissionMessage(
    Extension::PermissionMessage::MessageId message_id, string16 message)
    : message_id_(message_id),
      message_(message) {
}

const int Extension::PermissionMessage::kMessageIds[] = {
  kUndefinedMessageId,  // "unknown"
  kUndefinedMessageId,  // "none"
  IDS_EXTENSION_PROMPT_WARNING_BOOKMARKS,
  IDS_EXTENSION_PROMPT_WARNING_GEOLOCATION,
  IDS_EXTENSION_PROMPT_WARNING_BROWSING_HISTORY,
  IDS_EXTENSION_PROMPT_WARNING_TABS,
  IDS_EXTENSION_PROMPT_WARNING_MANAGEMENT,
  IDS_EXTENSION_PROMPT_WARNING_DEBUGGER,
  IDS_EXTENSION_PROMPT_WARNING_1_HOST,
  IDS_EXTENSION_PROMPT_WARNING_2_HOSTS,
  IDS_EXTENSION_PROMPT_WARNING_3_HOSTS,
  IDS_EXTENSION_PROMPT_WARNING_4_OR_MORE_HOSTS,
  IDS_EXTENSION_PROMPT_WARNING_ALL_HOSTS,
  IDS_EXTENSION_PROMPT_WARNING_FULL_ACCESS
};

//
// Extension
//

// static
scoped_refptr<Extension> Extension::Create(const FilePath& path,
                                           Location location,
                                           const DictionaryValue& value,
                                           int flags,
                                           std::string* error) {
  scoped_refptr<Extension> extension = new Extension(path, location);

  if (!extension->InitFromValue(value, flags, error))
    return NULL;
  return extension;
}

namespace {
const char* kGalleryUpdateHttpUrl =
    "http://clients2.google.com/service/update2/crx";
const char* kGalleryUpdateHttpsUrl =
    "https://clients2.google.com/service/update2/crx";
}  // namespace

// static
GURL Extension::GalleryUpdateUrl(bool secure) {
  CommandLine* cmdline = CommandLine::ForCurrentProcess();
  if (cmdline->HasSwitch(switches::kAppsGalleryUpdateURL))
    return GURL(cmdline->GetSwitchValueASCII(switches::kAppsGalleryUpdateURL));
  else
    return GURL(secure ? kGalleryUpdateHttpsUrl : kGalleryUpdateHttpUrl);
}

// static
Extension::Location Extension::GetHigherPriorityLocation(
    Extension::Location loc1, Extension::Location loc2) {
  if (loc1 == loc2)
    return loc1;

  int loc1_rank = GetLocationRank(loc1);
  int loc2_rank = GetLocationRank(loc2);

  // If two different locations have the same rank, then we can not
  // deterministicly choose a location.
  CHECK(loc1_rank != loc2_rank);

  // Lowest rank has highest priority.
  return (loc1_rank > loc2_rank ? loc1 : loc2 );
}

// static
Extension::PermissionMessage::MessageId Extension::GetPermissionMessageId(
    const std::string& permission) {
  return ExtensionConfig::GetInstance()->GetPermissionMessageId(permission);
}

Extension::PermissionMessages Extension::GetPermissionMessages() const {
  PermissionMessages messages;
  if (!plugins().empty()) {
    messages.push_back(PermissionMessage::CreateFromMessageId(
        PermissionMessage::ID_FULL_ACCESS));
    return messages;
  }

  if (HasEffectiveAccessToAllHosts()) {
    messages.push_back(PermissionMessage::CreateFromMessageId(
        PermissionMessage::ID_HOSTS_ALL));
  } else {
    std::vector<std::string> hosts = GetDistinctHostsForDisplay(
        GetEffectiveHostPermissions().patterns());
    if (!hosts.empty())
      messages.push_back(PermissionMessage::CreateFromHostList(hosts));
  }

  std::set<PermissionMessage> simple_msgs = GetSimplePermissionMessages();
  messages.insert(messages.end(), simple_msgs.begin(), simple_msgs.end());

  return messages;
}

std::vector<string16> Extension::GetPermissionMessageStrings() const {
  std::vector<string16> messages;
  PermissionMessages permissions = GetPermissionMessages();
  for (PermissionMessages::const_iterator i = permissions.begin();
       i != permissions.end(); ++i)
    messages.push_back(i->message());
  return messages;
}

std::set<Extension::PermissionMessage>
    Extension::GetSimplePermissionMessages() const {
  std::set<PermissionMessage> messages;
  std::set<std::string>::const_iterator i;
  for (i = api_permissions().begin(); i != api_permissions().end(); ++i) {
    PermissionMessage::MessageId message_id = GetPermissionMessageId(*i);
    DCHECK_GT(PermissionMessage::ID_NONE, PermissionMessage::ID_UNKNOWN);
    if (message_id > PermissionMessage::ID_NONE)
      messages.insert(PermissionMessage::CreateFromMessageId(message_id));
  }
  return messages;
}

// static
std::vector<std::string> Extension::GetDistinctHostsForDisplay(
    const URLPatternList& list) {
  return GetDistinctHosts(list, true);
}

// static
bool Extension::IsElevatedHostList(
    const URLPatternList& old_list, const URLPatternList& new_list) {
  // TODO(jstritar): This is overly conservative with respect to subdomains.
  // For example, going from *.google.com to www.google.com will be
  // considered an elevation, even though it is not (http://crbug.com/65337).

  std::vector<std::string> new_hosts = GetDistinctHosts(new_list, false);
  std::vector<std::string> old_hosts = GetDistinctHosts(old_list, false);

  std::set<std::string> old_hosts_set(old_hosts.begin(), old_hosts.end());
  std::set<std::string> new_hosts_set(new_hosts.begin(), new_hosts.end());
  std::set<std::string> new_hosts_only;

  std::set_difference(new_hosts_set.begin(), new_hosts_set.end(),
                      old_hosts_set.begin(), old_hosts_set.end(),
                      std::inserter(new_hosts_only, new_hosts_only.begin()));

  return !new_hosts_only.empty();
}

// Helper for GetDistinctHosts(): com > net > org > everything else.
static bool RcdBetterThan(const std::string& a, const std::string& b) {
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

// static
std::vector<std::string> Extension::GetDistinctHosts(
    const URLPatternList& host_patterns, bool include_rcd) {
  // Use a vector to preserve order (also faster than a map on small sets).
  // Each item is a host split into two parts: host without RCDs and
  // current best RCD.
  typedef std::vector<std::pair<std::string, std::string> > HostVector;
  HostVector hosts_best_rcd;
  for (size_t i = 0; i < host_patterns.size(); ++i) {
    std::string host = host_patterns[i].host();

    // Add the subdomain wildcard back to the host, if necessary.
    if (host_patterns[i].match_subdomains())
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
  std::vector<std::string> distinct_hosts;
  for (HostVector::iterator it = hosts_best_rcd.begin();
       it != hosts_best_rcd.end(); ++it)
    distinct_hosts.push_back(it->first + it->second);
  return distinct_hosts;
}

FilePath Extension::MaybeNormalizePath(const FilePath& path) {
#if defined(OS_WIN)
  // Normalize any drive letter to upper-case. We do this for consistency with
  // net_utils::FilePathToFileURL(), which does the same thing, to make string
  // comparisons simpler.
  std::wstring path_str = path.value();
  if (path_str.size() >= 2 && path_str[0] >= L'a' && path_str[0] <= L'z' &&
      path_str[1] == ':')
    path_str[0] += ('A' - 'a');

  return FilePath(path_str);
#else
  return path;
#endif
}

// static
bool Extension::IsHostedAppPermission(const std::string& str) {
  for (size_t i = 0; i < Extension::kNumHostedAppPermissions; ++i) {
    if (str == Extension::kHostedAppPermissionNames[i]) {
      return true;
    }
  }
  return false;
}

const std::string Extension::VersionString() const {
  return version()->GetString();
}

// static
bool Extension::IsExtension(const FilePath& file_name) {
  return file_name.MatchesExtension(chrome::kExtensionFileExtension);
}

// static
bool Extension::IdIsValid(const std::string& id) {
  // Verify that the id is legal.
  if (id.size() != (kIdSize * 2))
    return false;

  // We only support lowercase IDs, because IDs can be used as URL components
  // (where GURL will lowercase it).
  std::string temp = StringToLowerASCII(id);
  for (size_t i = 0; i < temp.size(); i++)
    if (temp[i] < 'a' || temp[i] > 'p')
      return false;

  return true;
}

// static
std::string Extension::GenerateIdForPath(const FilePath& path) {
  FilePath new_path = Extension::MaybeNormalizePath(path);
  std::string path_bytes =
      std::string(reinterpret_cast<const char*>(new_path.value().data()),
                  new_path.value().size() * sizeof(FilePath::CharType));
  std::string id;
  if (!GenerateId(path_bytes, &id))
    return "";
  return id;
}

Extension::Type Extension::GetType() const {
  if (is_theme())
    return TYPE_THEME;
  if (converted_from_user_script())
    return TYPE_USER_SCRIPT;
  if (is_hosted_app())
    return TYPE_HOSTED_APP;
  if (is_packaged_app())
    return TYPE_PACKAGED_APP;
  return TYPE_EXTENSION;
}

// static
GURL Extension::GetResourceURL(const GURL& extension_url,
                               const std::string& relative_path) {
  DCHECK(extension_url.SchemeIs(chrome::kExtensionScheme));
  DCHECK_EQ("/", extension_url.path());

  GURL ret_val = GURL(extension_url.spec() + relative_path);
  DCHECK(StartsWithASCII(ret_val.spec(), extension_url.spec(), false));

  return ret_val;
}

bool Extension::GenerateId(const std::string& input, std::string* output) {
  CHECK(output);
  uint8 hash[Extension::kIdSize];
  crypto::SHA256HashString(input, hash, sizeof(hash));
  *output = StringToLowerASCII(base::HexEncode(hash, sizeof(hash)));
  ConvertHexadecimalToIDAlphabet(output);

  return true;
}

// Helper method that loads a UserScript object from a dictionary in the
// content_script list of the manifest.
bool Extension::LoadUserScriptHelper(const DictionaryValue* content_script,
                                     int definition_index,
                                     int flags,
                                     std::string* error,
                                     UserScript* result) {
  // When strict error checks are enabled, make URL pattern parsing strict.
  URLPattern::ParseOption parse_strictness =
      (flags & STRICT_ERROR_CHECKS ? URLPattern::PARSE_STRICT
                                   : URLPattern::PARSE_LENIENT);

  // run_at
  if (content_script->HasKey(keys::kRunAt)) {
    std::string run_location;
    if (!content_script->GetString(keys::kRunAt, &run_location)) {
      *error = ExtensionErrorUtils::FormatErrorMessage(errors::kInvalidRunAt,
          base::IntToString(definition_index));
      return false;
    }

    if (run_location == values::kRunAtDocumentStart) {
      result->set_run_location(UserScript::DOCUMENT_START);
    } else if (run_location == values::kRunAtDocumentEnd) {
      result->set_run_location(UserScript::DOCUMENT_END);
    } else if (run_location == values::kRunAtDocumentIdle) {
      result->set_run_location(UserScript::DOCUMENT_IDLE);
    } else {
      *error = ExtensionErrorUtils::FormatErrorMessage(errors::kInvalidRunAt,
          base::IntToString(definition_index));
      return false;
    }
  }

  // all frames
  if (content_script->HasKey(keys::kAllFrames)) {
    bool all_frames = false;
    if (!content_script->GetBoolean(keys::kAllFrames, &all_frames)) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
            errors::kInvalidAllFrames, base::IntToString(definition_index));
      return false;
    }
    result->set_match_all_frames(all_frames);
  }

  // matches
  ListValue* matches = NULL;
  if (!content_script->GetList(keys::kMatches, &matches)) {
    *error = ExtensionErrorUtils::FormatErrorMessage(errors::kInvalidMatches,
        base::IntToString(definition_index));
    return false;
  }

  if (matches->GetSize() == 0) {
    *error = ExtensionErrorUtils::FormatErrorMessage(errors::kInvalidMatchCount,
        base::IntToString(definition_index));
    return false;
  }
  for (size_t j = 0; j < matches->GetSize(); ++j) {
    std::string match_str;
    if (!matches->GetString(j, &match_str)) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidMatch,
          base::IntToString(definition_index),
          base::IntToString(j),
          errors::kExpectString);
      return false;
    }

    URLPattern pattern(UserScript::kValidUserScriptSchemes);
    if (CanExecuteScriptEverywhere())
      pattern.set_valid_schemes(URLPattern::SCHEME_ALL);

    URLPattern::ParseResult parse_result = pattern.Parse(match_str,
                                                         parse_strictness);
    if (parse_result != URLPattern::PARSE_SUCCESS) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidMatch,
          base::IntToString(definition_index),
          base::IntToString(j),
          URLPattern::GetParseResultString(parse_result));
      return false;
    }

    if (pattern.MatchesScheme(chrome::kFileScheme) &&
        !CanExecuteScriptEverywhere()) {
      wants_file_access_ = true;
      if (!(flags & ALLOW_FILE_ACCESS))
        pattern.set_valid_schemes(
            pattern.valid_schemes() & ~URLPattern::SCHEME_FILE);
    }

    result->add_url_pattern(pattern);
  }

  // include/exclude globs (mostly for Greasemonkey compatibility)
  if (!LoadGlobsHelper(content_script, definition_index, keys::kIncludeGlobs,
                       error, &UserScript::add_glob, result)) {
      return false;
  }

  if (!LoadGlobsHelper(content_script, definition_index, keys::kExcludeGlobs,
                       error, &UserScript::add_exclude_glob, result)) {
      return false;
  }

  // js and css keys
  ListValue* js = NULL;
  if (content_script->HasKey(keys::kJs) &&
      !content_script->GetList(keys::kJs, &js)) {
    *error = ExtensionErrorUtils::FormatErrorMessage(errors::kInvalidJsList,
        base::IntToString(definition_index));
    return false;
  }

  ListValue* css = NULL;
  if (content_script->HasKey(keys::kCss) &&
      !content_script->GetList(keys::kCss, &css)) {
    *error = ExtensionErrorUtils::FormatErrorMessage(errors::kInvalidCssList,
        base::IntToString(definition_index));
    return false;
  }

  // The manifest needs to have at least one js or css user script definition.
  if (((js ? js->GetSize() : 0) + (css ? css->GetSize() : 0)) == 0) {
    *error = ExtensionErrorUtils::FormatErrorMessage(errors::kMissingFile,
        base::IntToString(definition_index));
    return false;
  }

  if (js) {
    for (size_t script_index = 0; script_index < js->GetSize();
         ++script_index) {
      Value* value;
      std::string relative;
      if (!js->Get(script_index, &value) || !value->GetAsString(&relative)) {
        *error = ExtensionErrorUtils::FormatErrorMessage(errors::kInvalidJs,
            base::IntToString(definition_index),
            base::IntToString(script_index));
        return false;
      }
      GURL url = GetResourceURL(relative);
      ExtensionResource resource = GetResource(relative);
      result->js_scripts().push_back(UserScript::File(
          resource.extension_root(), resource.relative_path(), url));
    }
  }

  if (css) {
    for (size_t script_index = 0; script_index < css->GetSize();
         ++script_index) {
      Value* value;
      std::string relative;
      if (!css->Get(script_index, &value) || !value->GetAsString(&relative)) {
        *error = ExtensionErrorUtils::FormatErrorMessage(errors::kInvalidCss,
            base::IntToString(definition_index),
            base::IntToString(script_index));
        return false;
      }
      GURL url = GetResourceURL(relative);
      ExtensionResource resource = GetResource(relative);
      result->css_scripts().push_back(UserScript::File(
          resource.extension_root(), resource.relative_path(), url));
    }
  }

  return true;
}

bool Extension::LoadGlobsHelper(
    const DictionaryValue* content_script,
    int content_script_index,
    const char* globs_property_name,
    std::string* error,
    void(UserScript::*add_method)(const std::string& glob),
    UserScript *instance) {
  if (!content_script->HasKey(globs_property_name))
    return true;  // they are optional

  ListValue* list = NULL;
  if (!content_script->GetList(globs_property_name, &list)) {
    *error = ExtensionErrorUtils::FormatErrorMessage(errors::kInvalidGlobList,
        base::IntToString(content_script_index),
        globs_property_name);
    return false;
  }

  for (size_t i = 0; i < list->GetSize(); ++i) {
    std::string glob;
    if (!list->GetString(i, &glob)) {
      *error = ExtensionErrorUtils::FormatErrorMessage(errors::kInvalidGlob,
          base::IntToString(content_script_index),
          globs_property_name,
          base::IntToString(i));
      return false;
    }

    (instance->*add_method)(glob);
  }

  return true;
}

ExtensionAction* Extension::LoadExtensionActionHelper(
    const DictionaryValue* extension_action, std::string* error) {
  scoped_ptr<ExtensionAction> result(new ExtensionAction());
  result->set_extension_id(id());

  // Page actions are hidden by default, and browser actions ignore
  // visibility.
  result->SetIsVisible(ExtensionAction::kDefaultTabId, false);

  // TODO(EXTENSIONS_DEPRECATED): icons list is obsolete.
  ListValue* icons = NULL;
  if (extension_action->HasKey(keys::kPageActionIcons) &&
      extension_action->GetList(keys::kPageActionIcons, &icons)) {
    for (ListValue::const_iterator iter = icons->begin();
         iter != icons->end(); ++iter) {
      std::string path;
      if (!(*iter)->GetAsString(&path) || path.empty()) {
        *error = errors::kInvalidPageActionIconPath;
        return NULL;
      }

      result->icon_paths()->push_back(path);
    }
  }

  // TODO(EXTENSIONS_DEPRECATED): Read the page action |id| (optional).
  std::string id;
  if (extension_action->HasKey(keys::kPageActionId)) {
    if (!extension_action->GetString(keys::kPageActionId, &id)) {
      *error = errors::kInvalidPageActionId;
      return NULL;
    }
    result->set_id(id);
  }

  std::string default_icon;
  // Read the page action |default_icon| (optional).
  if (extension_action->HasKey(keys::kPageActionDefaultIcon)) {
    if (!extension_action->GetString(keys::kPageActionDefaultIcon,
                                     &default_icon) ||
        default_icon.empty()) {
      *error = errors::kInvalidPageActionIconPath;
      return NULL;
    }
    result->set_default_icon_path(default_icon);
  }

  // Read the page action title from |default_title| if present, |name| if not
  // (both optional).
  std::string title;
  if (extension_action->HasKey(keys::kPageActionDefaultTitle)) {
    if (!extension_action->GetString(keys::kPageActionDefaultTitle, &title)) {
      *error = errors::kInvalidPageActionDefaultTitle;
      return NULL;
    }
  } else if (extension_action->HasKey(keys::kName)) {
    if (!extension_action->GetString(keys::kName, &title)) {
      *error = errors::kInvalidPageActionName;
      return NULL;
    }
  }
  result->SetTitle(ExtensionAction::kDefaultTabId, title);

  // Read the action's |popup| (optional).
  const char* popup_key = NULL;
  if (extension_action->HasKey(keys::kPageActionDefaultPopup))
    popup_key = keys::kPageActionDefaultPopup;

  // For backward compatibility, alias old key "popup" to new
  // key "default_popup".
  if (extension_action->HasKey(keys::kPageActionPopup)) {
    if (popup_key) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidPageActionOldAndNewKeys,
          keys::kPageActionDefaultPopup,
          keys::kPageActionPopup);
      return NULL;
    }
    popup_key = keys::kPageActionPopup;
  }

  if (popup_key) {
    DictionaryValue* popup = NULL;
    std::string url_str;

    if (extension_action->GetString(popup_key, &url_str)) {
      // On success, |url_str| is set.  Nothing else to do.
    } else if (extension_action->GetDictionary(popup_key, &popup)) {
      // TODO(EXTENSIONS_DEPRECATED): popup is now a string only.
      // Support the old dictionary format for backward compatibility.
      if (!popup->GetString(keys::kPageActionPopupPath, &url_str)) {
        *error = ExtensionErrorUtils::FormatErrorMessage(
            errors::kInvalidPageActionPopupPath, "<missing>");
        return NULL;
      }
    } else {
      *error = errors::kInvalidPageActionPopup;
      return NULL;
    }

    if (!url_str.empty()) {
      // An empty string is treated as having no popup.
      GURL url = GetResourceURL(url_str);
      if (!url.is_valid()) {
        *error = ExtensionErrorUtils::FormatErrorMessage(
            errors::kInvalidPageActionPopupPath, url_str);
        return NULL;
      }
      result->SetPopupUrl(ExtensionAction::kDefaultTabId, url);
    } else {
      DCHECK(!result->HasPopup(ExtensionAction::kDefaultTabId))
          << "Shouldn't be posible for the popup to be set.";
    }
  }

  return result.release();
}

Extension::FileBrowserHandlerList* Extension::LoadFileBrowserHandlers(
    const ListValue* extension_actions, std::string* error) {
  scoped_ptr<FileBrowserHandlerList> result(
      new FileBrowserHandlerList());
  for (ListValue::const_iterator iter = extension_actions->begin();
       iter != extension_actions->end();
       ++iter) {
    if (!(*iter)->IsType(Value::TYPE_DICTIONARY)) {
      *error = errors::kInvalidFileBrowserHandler;
      return NULL;
    }
    scoped_ptr<FileBrowserHandler> action(
        LoadFileBrowserHandler(
            reinterpret_cast<DictionaryValue*>(*iter), error));
    if (!action.get())
      return NULL;  // Failed to parse file browser action definition.
    result->push_back(linked_ptr<FileBrowserHandler>(action.release()));
  }
  return result.release();
}

FileBrowserHandler* Extension::LoadFileBrowserHandler(
    const DictionaryValue* file_browser_handler, std::string* error) {
  scoped_ptr<FileBrowserHandler> result(
      new FileBrowserHandler());
  result->set_extension_id(id());

  std::string id;
  // Read the file action |id| (mandatory).
  if (!file_browser_handler->HasKey(keys::kPageActionId) ||
      !file_browser_handler->GetString(keys::kPageActionId, &id)) {
    *error = errors::kInvalidPageActionId;
    return NULL;
  }
  result->set_id(id);

  // Read the page action title from |default_title| (mandatory).
  std::string title;
  if (!file_browser_handler->HasKey(keys::kPageActionDefaultTitle) ||
      !file_browser_handler->GetString(keys::kPageActionDefaultTitle, &title)) {
    *error = errors::kInvalidPageActionDefaultTitle;
    return NULL;
  }
  result->set_title(title);

  // Initialize file filters (mandatory).
  ListValue* list_value = NULL;
  if (!file_browser_handler->HasKey(keys::kFileFilters) ||
      !file_browser_handler->GetList(keys::kFileFilters, &list_value) ||
      list_value->empty()) {
    *error = errors::kInvalidFileFiltersList;
    return NULL;
  }
  for (size_t i = 0; i < list_value->GetSize(); ++i) {
    std::string filter;
    if (!list_value->GetString(i, &filter)) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidFileFilterValue, base::IntToString(i));
      return NULL;
    }
    URLPattern pattern(URLPattern::SCHEME_FILESYSTEM);
    if (URLPattern::PARSE_SUCCESS != pattern.Parse(filter,
                                                   URLPattern::PARSE_STRICT)) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidURLPatternError, filter);
      return NULL;
    }
    result->AddPattern(pattern);
  }

  std::string default_icon;
  // Read the file browser action |default_icon| (optional).
  if (file_browser_handler->HasKey(keys::kPageActionDefaultIcon)) {
    if (!file_browser_handler->GetString(
            keys::kPageActionDefaultIcon,&default_icon) ||
        default_icon.empty()) {
      *error = errors::kInvalidPageActionIconPath;
      return NULL;
    }
    result->set_icon_path(default_icon);
  }

  return result.release();
}

ExtensionSidebarDefaults* Extension::LoadExtensionSidebarDefaults(
    const DictionaryValue* extension_sidebar, std::string* error) {
  scoped_ptr<ExtensionSidebarDefaults> result(new ExtensionSidebarDefaults());

  std::string default_icon;
  // Read sidebar's |default_icon| (optional).
  if (extension_sidebar->HasKey(keys::kSidebarDefaultIcon)) {
    if (!extension_sidebar->GetString(keys::kSidebarDefaultIcon,
                                      &default_icon) ||
        default_icon.empty()) {
      *error = errors::kInvalidSidebarDefaultIconPath;
      return NULL;
    }
    result->set_default_icon_path(default_icon);
  }

  // Read sidebar's |default_title| (optional).
  string16 default_title;
  if (extension_sidebar->HasKey(keys::kSidebarDefaultTitle)) {
    if (!extension_sidebar->GetString(keys::kSidebarDefaultTitle,
                                      &default_title)) {
      *error = errors::kInvalidSidebarDefaultTitle;
      return NULL;
    }
  }
  result->set_default_title(default_title);

  // Read sidebar's |default_page| (optional).
  std::string default_page;
  if (extension_sidebar->HasKey(keys::kSidebarDefaultPage)) {
    if (!extension_sidebar->GetString(keys::kSidebarDefaultPage,
                                      &default_page) ||
        default_page.empty()) {
      *error = errors::kInvalidSidebarDefaultPage;
      return NULL;
    }
    GURL url = extension_sidebar_utils::ResolveRelativePath(
        default_page, this, error);
    if (!url.is_valid())
      return NULL;
    result->set_default_page(url);
  }

  return result.release();
}

bool Extension::ContainsNonThemeKeys(const DictionaryValue& source) const {
  for (DictionaryValue::key_iterator key = source.begin_keys();
       key != source.end_keys(); ++key) {
    if (!IsBaseCrxKey(*key) && *key != keys::kTheme)
      return true;
  }
  return false;
}

bool Extension::LoadIsApp(const DictionaryValue* manifest,
                          std::string* error) {
  if (manifest->HasKey(keys::kApp))
    is_app_ = true;

  return true;
}

bool Extension::LoadExtent(const DictionaryValue* manifest,
                           const char* key,
                           ExtensionExtent* extent,
                           const char* list_error,
                           const char* value_error,
                           URLPattern::ParseOption parse_strictness,
                           std::string* error) {
  Value* temp = NULL;
  if (!manifest->Get(key, &temp))
    return true;

  if (temp->GetType() != Value::TYPE_LIST) {
    *error = list_error;
    return false;
  }

  ListValue* pattern_list = static_cast<ListValue*>(temp);
  for (size_t i = 0; i < pattern_list->GetSize(); ++i) {
    std::string pattern_string;
    if (!pattern_list->GetString(i, &pattern_string)) {
      *error = ExtensionErrorUtils::FormatErrorMessage(value_error,
                                                       base::UintToString(i),
                                                       errors::kExpectString);
      return false;
    }

    URLPattern pattern(kValidWebExtentSchemes);
    URLPattern::ParseResult parse_result = pattern.Parse(pattern_string,
                                                         parse_strictness);
    if (parse_result == URLPattern::PARSE_ERROR_EMPTY_PATH) {
      pattern_string += "/";
      parse_result = pattern.Parse(pattern_string, parse_strictness);
    }

    if (parse_result != URLPattern::PARSE_SUCCESS) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          value_error,
          base::UintToString(i),
          URLPattern::GetParseResultString(parse_result));
      return false;
    }

    // Do not allow authors to claim "<all_urls>".
    if (pattern.match_all_urls()) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          value_error,
          base::UintToString(i),
          errors::kCannotClaimAllURLsInExtent);
      return false;
    }

    // Do not allow authors to claim "*" for host.
    if (pattern.host().empty()) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          value_error,
          base::UintToString(i),
          errors::kCannotClaimAllHostsInExtent);
      return false;
    }

    // We do not allow authors to put wildcards in their paths. Instead, we
    // imply one at the end.
    if (pattern.path().find('*') != std::string::npos) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          value_error,
          base::UintToString(i),
          errors::kNoWildCardsInPaths);
      return false;
    }
    pattern.SetPath(pattern.path() + '*');

    extent->AddPattern(pattern);
  }

  return true;
}

bool Extension::LoadLaunchURL(const DictionaryValue* manifest,
                              std::string* error) {
  Value* temp = NULL;

  // launch URL can be either local (to chrome-extension:// root) or an absolute
  // web URL.
  if (manifest->Get(keys::kLaunchLocalPath, &temp)) {
    if (manifest->Get(keys::kLaunchWebURL, NULL)) {
      *error = errors::kLaunchPathAndURLAreExclusive;
      return false;
    }

    std::string launch_path;
    if (!temp->GetAsString(&launch_path)) {
      *error = errors::kInvalidLaunchLocalPath;
      return false;
    }

    // Ensure the launch path is a valid relative URL.
    GURL resolved = url().Resolve(launch_path);
    if (!resolved.is_valid() || resolved.GetOrigin() != url()) {
      *error = errors::kInvalidLaunchLocalPath;
      return false;
    }

    launch_local_path_ = launch_path;
  } else if (manifest->Get(keys::kLaunchWebURL, &temp)) {
    std::string launch_url;
    if (!temp->GetAsString(&launch_url)) {
      *error = errors::kInvalidLaunchWebURL;
      return false;
    }

    // Ensure the launch URL is a valid absolute URL and web extent scheme.
    GURL url(launch_url);
    URLPattern pattern(kValidWebExtentSchemes);
    if (!url.is_valid() || !pattern.SetScheme(url.scheme())) {
      *error = errors::kInvalidLaunchWebURL;
      return false;
    }

    launch_web_url_ = launch_url;
  } else if (is_app()) {
    *error = errors::kLaunchURLRequired;
    return false;
  }

  // If there is no extent, we default the extent based on the launch URL.
  if (web_extent().is_empty() && !launch_web_url().empty()) {
    GURL launch_url(launch_web_url());
    URLPattern pattern(kValidWebExtentSchemes);
    if (!pattern.SetScheme("*")) {
      *error = errors::kInvalidLaunchWebURL;
      return false;
    }
    pattern.set_host(launch_url.host());
    pattern.SetPath("/*");
    extent_.AddPattern(pattern);
  }

  // In order for the --apps-gallery-url switch to work with the gallery
  // process isolation, we must insert any provided value into the component
  // app's launch url and web extent.
  if (id() == extension_misc::kWebStoreAppId) {
    std::string gallery_url_str = CommandLine::ForCurrentProcess()->
        GetSwitchValueASCII(switches::kAppsGalleryURL);

    // Empty string means option was not used.
    if (!gallery_url_str.empty()) {
      GURL gallery_url(gallery_url_str);
      if (!gallery_url.is_valid()) {
        LOG(WARNING) << "Invalid url given in switch "
                     << switches::kAppsGalleryURL;
      } else {
        if (gallery_url.has_port()) {
          LOG(WARNING) << "URLs passed to switch " << switches::kAppsGalleryURL
                       << " should not contain a port.  Removing it.";

          GURL::Replacements remove_port;
          remove_port.ClearPort();
          gallery_url = gallery_url.ReplaceComponents(remove_port);
        }

        launch_web_url_ = gallery_url.spec();

        URLPattern pattern(kValidWebExtentSchemes);
        pattern.Parse(gallery_url.spec(), URLPattern::PARSE_STRICT);
        pattern.SetPath(pattern.path() + '*');
        extent_.AddPattern(pattern);
      }
    }
  }

  return true;
}

bool Extension::LoadLaunchContainer(const DictionaryValue* manifest,
                                    std::string* error) {
  Value* temp = NULL;
  if (!manifest->Get(keys::kLaunchContainer, &temp))
    return true;

  std::string launch_container_string;
  if (!temp->GetAsString(&launch_container_string)) {
    *error = errors::kInvalidLaunchContainer;
    return false;
  }

  if (launch_container_string == values::kLaunchContainerPanel) {
    launch_container_ = extension_misc::LAUNCH_PANEL;
  } else if (launch_container_string == values::kLaunchContainerTab) {
    launch_container_ = extension_misc::LAUNCH_TAB;
  } else {
    *error = errors::kInvalidLaunchContainer;
    return false;
  }

  // Validate the container width if present.
  if (manifest->Get(keys::kLaunchWidth, &temp)) {
    if (launch_container() != extension_misc::LAUNCH_PANEL &&
        launch_container() != extension_misc::LAUNCH_WINDOW) {
      *error = errors::kInvalidLaunchWidthContainer;
      return false;
    }
    if (!temp->GetAsInteger(&launch_width_) ||
        launch_width_ < 0) {
      launch_width_ = 0;
      *error = errors::kInvalidLaunchWidth;
      return false;
    }
  }

  // Validate container height if present.
  if (manifest->Get(keys::kLaunchHeight, &temp)) {
    if (launch_container() != extension_misc::LAUNCH_PANEL &&
        launch_container() != extension_misc::LAUNCH_WINDOW) {
      *error = errors::kInvalidLaunchHeightContainer;
      return false;
    }
    if (!temp->GetAsInteger(&launch_height_) || launch_height_ < 0) {
      launch_height_ = 0;
      *error = errors::kInvalidLaunchHeight;
      return false;
    }
  }

  return true;
}

bool Extension::LoadAppIsolation(const DictionaryValue* manifest,
                                 std::string* error) {
  // Only parse app isolation features if this switch is present.
  if (!CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableExperimentalAppManifests))
    return true;

  Value* temp = NULL;
  if (!manifest->Get(keys::kIsolation, &temp))
    return true;

  if (temp->GetType() != Value::TYPE_LIST) {
    *error = errors::kInvalidIsolation;
    return false;
  }

  ListValue* isolation_list = static_cast<ListValue*>(temp);
  for (size_t i = 0; i < isolation_list->GetSize(); ++i) {
    std::string isolation_string;
    if (!isolation_list->GetString(i, &isolation_string)) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidIsolationValue,
          base::UintToString(i));
      return false;
    }

    // Check for isolated storage.
    if (isolation_string == values::kIsolatedStorage) {
      is_storage_isolated_ = true;
    } else {
      LOG(WARNING) << "Did not recognize isolation type: "
                   << isolation_string;
    }
  }
  return true;
}

bool Extension::EnsureNotHybridApp(const DictionaryValue* manifest,
                                   std::string* error) {
  if (web_extent().is_empty())
    return true;

  for (DictionaryValue::key_iterator key = manifest->begin_keys();
       key != manifest->end_keys(); ++key) {
    if (!IsBaseCrxKey(*key) &&
        *key != keys::kApp &&
        *key != keys::kPermissions &&
        *key != keys::kOptionsPage &&
        *key != keys::kBackground) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          errors::kHostedAppsCannotIncludeExtensionFeatures, *key);
      return false;
    }
  }

  return true;
}

Extension::Extension(const FilePath& path, Location location)
    : incognito_split_mode_(false),
      location_(location),
      converted_from_user_script_(false),
      is_theme_(false),
      is_app_(false),
      is_storage_isolated_(false),
      launch_container_(extension_misc::LAUNCH_TAB),
      launch_width_(0),
      launch_height_(0),
      wants_file_access_(false) {
  DCHECK(path.empty() || path.IsAbsolute());
  path_ = MaybeNormalizePath(path);
}

Extension::~Extension() {
}

ExtensionResource Extension::GetResource(
    const std::string& relative_path) const {
#if defined(OS_POSIX)
  FilePath relative_file_path(relative_path);
#elif defined(OS_WIN)
  FilePath relative_file_path(UTF8ToWide(relative_path));
#endif
  return ExtensionResource(id(), path(), relative_file_path);
}

ExtensionResource Extension::GetResource(
    const FilePath& relative_file_path) const {
  return ExtensionResource(id(), path(), relative_file_path);
}

// TODO(rafaelw): Move ParsePEMKeyBytes, ProducePEM & FormatPEMForOutput to a
// util class in base:
// http://code.google.com/p/chromium/issues/detail?id=13572
bool Extension::ParsePEMKeyBytes(const std::string& input,
                                 std::string* output) {
  DCHECK(output);
  if (!output)
    return false;
  if (input.length() == 0)
    return false;

  std::string working = input;
  if (StartsWithASCII(working, kKeyBeginHeaderMarker, true)) {
    working = CollapseWhitespaceASCII(working, true);
    size_t header_pos = working.find(kKeyInfoEndMarker,
      sizeof(kKeyBeginHeaderMarker) - 1);
    if (header_pos == std::string::npos)
      return false;
    size_t start_pos = header_pos + sizeof(kKeyInfoEndMarker) - 1;
    size_t end_pos = working.rfind(kKeyBeginFooterMarker);
    if (end_pos == std::string::npos)
      return false;
    if (start_pos >= end_pos)
      return false;

    working = working.substr(start_pos, end_pos - start_pos);
    if (working.length() == 0)
      return false;
  }

  return base::Base64Decode(working, output);
}

bool Extension::ProducePEM(const std::string& input, std::string* output) {
  CHECK(output);
  if (input.length() == 0)
    return false;

  return base::Base64Encode(input, output);
}

bool Extension::FormatPEMForFileOutput(const std::string& input,
                                       std::string* output,
                                       bool is_public) {
  CHECK(output);
  if (input.length() == 0)
    return false;
  *output = "";
  output->append(kKeyBeginHeaderMarker);
  output->append(" ");
  output->append(is_public ? kPublic : kPrivate);
  output->append(" ");
  output->append(kKeyInfoEndMarker);
  output->append("\n");
  for (size_t i = 0; i < input.length(); ) {
    int slice = std::min<int>(input.length() - i, kPEMOutputColumns);
    output->append(input.substr(i, slice));
    output->append("\n");
    i += slice;
  }
  output->append(kKeyBeginFooterMarker);
  output->append(" ");
  output->append(is_public ? kPublic : kPrivate);
  output->append(" ");
  output->append(kKeyInfoEndMarker);
  output->append("\n");

  return true;
}

// static
bool Extension::IsPrivilegeIncrease(const bool granted_full_access,
                                    const std::set<std::string>& granted_apis,
                                    const ExtensionExtent& granted_extent,
                                    const Extension* new_extension) {
  // If the extension had native code access, we don't need to go any further.
  // Things can't get any worse.
  if (granted_full_access)
    return false;

  // Otherwise, if the new extension has a plugin, it's a privilege increase.
  if (new_extension->HasFullPermissions())
    return true;

  // If the extension hadn't been granted access to all hosts in the past, then
  // see if the extension requires more host permissions.
  if (!HasEffectiveAccessToAllHosts(granted_extent, granted_apis)) {
    if (new_extension->HasEffectiveAccessToAllHosts())
      return true;

    const ExtensionExtent new_extent =
        new_extension->GetEffectiveHostPermissions();

    if (IsElevatedHostList(granted_extent.patterns(), new_extent.patterns()))
      return true;
  }

  std::set<std::string> new_apis = new_extension->api_permissions();
  std::set<std::string> new_apis_only;
  std::set_difference(new_apis.begin(), new_apis.end(),
                      granted_apis.begin(), granted_apis.end(),
                      std::inserter(new_apis_only, new_apis_only.begin()));

  // Ignore API permissions that don't require user approval when deciding if
  // an extension has increased its privileges.
  size_t new_api_count = 0;
  for (std::set<std::string>::iterator i = new_apis_only.begin();
       i != new_apis_only.end(); ++i) {
    DCHECK_GT(PermissionMessage::ID_NONE, PermissionMessage::ID_UNKNOWN);
    if (GetPermissionMessageId(*i) > PermissionMessage::ID_NONE)
      new_api_count++;
  }

  if (new_api_count)
    return true;

  return false;
}

// static
void Extension::DecodeIcon(const Extension* extension,
                           Icons icon_size,
                           scoped_ptr<SkBitmap>* result) {
  FilePath icon_path = extension->GetIconResource(
      icon_size, ExtensionIconSet::MATCH_EXACTLY).GetFilePath();
  DecodeIconFromPath(icon_path, icon_size, result);
}

// static
void Extension::DecodeIconFromPath(const FilePath& icon_path,
                                   Icons icon_size,
                                   scoped_ptr<SkBitmap>* result) {
  if (icon_path.empty())
    return;

  std::string file_contents;
  if (!file_util::ReadFileToString(icon_path, &file_contents)) {
    LOG(ERROR) << "Could not read icon file: " << icon_path.LossyDisplayName();
    return;
  }

  // Decode the image using WebKit's image decoder.
  const unsigned char* data =
    reinterpret_cast<const unsigned char*>(file_contents.data());
  webkit_glue::ImageDecoder decoder;
  scoped_ptr<SkBitmap> decoded(new SkBitmap());
  *decoded = decoder.Decode(data, file_contents.length());
  if (decoded->empty()) {
    LOG(ERROR) << "Could not decode icon file: "
               << icon_path.LossyDisplayName();
    return;
  }

  if (decoded->width() != icon_size || decoded->height() != icon_size) {
    LOG(ERROR) << "Icon file has unexpected size: "
               << base::IntToString(decoded->width()) << "x"
               << base::IntToString(decoded->height());
    return;
  }

  result->swap(decoded);
}

// static
const SkBitmap& Extension::GetDefaultIcon(bool is_app) {
  if (is_app) {
    return *ResourceBundle::GetSharedInstance().GetBitmapNamed(
        IDR_APP_DEFAULT_ICON);
  } else {
    return *ResourceBundle::GetSharedInstance().GetBitmapNamed(
        IDR_EXTENSION_DEFAULT_ICON);
  }
}

GURL Extension::GetBaseURLFromExtensionId(const std::string& extension_id) {
  return GURL(std::string(chrome::kExtensionScheme) +
              chrome::kStandardSchemeSeparator + extension_id + "/");
}

bool Extension::InitFromValue(const DictionaryValue& source, int flags,
                              std::string* error) {
  // When strict error checks are enabled, make URL pattern parsing strict.
  URLPattern::ParseOption parse_strictness =
      (flags & STRICT_ERROR_CHECKS ? URLPattern::PARSE_STRICT
                                   : URLPattern::PARSE_LENIENT);

  if (source.HasKey(keys::kPublicKey)) {
    std::string public_key_bytes;
    if (!source.GetString(keys::kPublicKey,
                          &public_key_) ||
        !ParsePEMKeyBytes(public_key_,
                          &public_key_bytes) ||
        !GenerateId(public_key_bytes, &id_)) {
      *error = errors::kInvalidKey;
      return false;
    }
  } else if (flags & REQUIRE_KEY) {
    *error = errors::kInvalidKey;
    return false;
  } else {
    // If there is a path, we generate the ID from it. This is useful for
    // development mode, because it keeps the ID stable across restarts and
    // reloading the extension.
    id_ = Extension::GenerateIdForPath(path());
    if (id_.empty()) {
      NOTREACHED() << "Could not create ID from path.";
      return false;
    }
  }

  // Make a copy of the manifest so we can store it in prefs.
  manifest_value_.reset(source.DeepCopy());

  // Initialize the URL.
  extension_url_ = Extension::GetBaseURLFromExtensionId(id());

  // Initialize version.
  std::string version_str;
  if (!source.GetString(keys::kVersion, &version_str)) {
    *error = errors::kInvalidVersion;
    return false;
  }
  version_.reset(Version::GetVersionFromString(version_str));
  if (!version_.get() ||
      version_->components().size() > 4) {
    *error = errors::kInvalidVersion;
    return false;
  }

  // Initialize name.
  string16 localized_name;
  if (!source.GetString(keys::kName, &localized_name)) {
    *error = errors::kInvalidName;
    return false;
  }
  base::i18n::AdjustStringForLocaleDirection(&localized_name);
  name_ = UTF16ToUTF8(localized_name);

  // Initialize description (if present).
  if (source.HasKey(keys::kDescription)) {
    if (!source.GetString(keys::kDescription,
                          &description_)) {
      *error = errors::kInvalidDescription;
      return false;
    }
  }

  // Initialize homepage url (if present).
  if (source.HasKey(keys::kHomepageURL)) {
    std::string tmp;
    if (!source.GetString(keys::kHomepageURL, &tmp)) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidHomepageURL, "");
      return false;
    }
    homepage_url_ = GURL(tmp);
    if (!homepage_url_.is_valid()) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidHomepageURL, tmp);
      return false;
    }
  }

  // Initialize update url (if present).
  if (source.HasKey(keys::kUpdateURL)) {
    std::string tmp;
    if (!source.GetString(keys::kUpdateURL, &tmp)) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidUpdateURL, "");
      return false;
    }
    update_url_ = GURL(tmp);
    if (!update_url_.is_valid() ||
        update_url_.has_ref()) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidUpdateURL, tmp);
      return false;
    }
  }

  // Validate minimum Chrome version (if present). We don't need to store this,
  // since the extension is not valid if it is incorrect.
  if (source.HasKey(keys::kMinimumChromeVersion)) {
    std::string minimum_version_string;
    if (!source.GetString(keys::kMinimumChromeVersion,
                          &minimum_version_string)) {
      *error = errors::kInvalidMinimumChromeVersion;
      return false;
    }

    scoped_ptr<Version> minimum_version(
        Version::GetVersionFromString(minimum_version_string));
    if (!minimum_version.get()) {
      *error = errors::kInvalidMinimumChromeVersion;
      return false;
    }

    chrome::VersionInfo current_version_info;
    if (!current_version_info.is_valid()) {
      NOTREACHED();
      return false;
    }

    scoped_ptr<Version> current_version(
        Version::GetVersionFromString(current_version_info.Version()));
    if (!current_version.get()) {
      DCHECK(false);
      return false;
    }

    if (current_version->CompareTo(*minimum_version) < 0) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          errors::kChromeVersionTooLow,
          l10n_util::GetStringUTF8(IDS_PRODUCT_NAME),
          minimum_version_string);
      return false;
    }
  }

  // Initialize converted_from_user_script (if present)
  source.GetBoolean(keys::kConvertedFromUserScript,
                    &converted_from_user_script_);

  // Initialize icons (if present).
  if (source.HasKey(keys::kIcons)) {
    DictionaryValue* icons_value = NULL;
    if (!source.GetDictionary(keys::kIcons, &icons_value)) {
      *error = errors::kInvalidIcons;
      return false;
    }

    for (size_t i = 0; i < arraysize(kIconSizes); ++i) {
      std::string key = base::IntToString(kIconSizes[i]);
      if (icons_value->HasKey(key)) {
        std::string icon_path;
        if (!icons_value->GetString(key, &icon_path)) {
          *error = ExtensionErrorUtils::FormatErrorMessage(
              errors::kInvalidIconPath, key);
          return false;
        }

        if (!icon_path.empty() && icon_path[0] == '/')
          icon_path = icon_path.substr(1);

        if (icon_path.empty()) {
          *error = ExtensionErrorUtils::FormatErrorMessage(
              errors::kInvalidIconPath, key);
          return false;
        }

        icons_.Add(kIconSizes[i], icon_path);
      }
    }
  }

  // Initialize themes (if present).
  is_theme_ = false;
  if (source.HasKey(keys::kTheme)) {
    // Themes cannot contain extension keys.
    if (ContainsNonThemeKeys(source)) {
      *error = errors::kThemesCannotContainExtensions;
      return false;
    }

    DictionaryValue* theme_value = NULL;
    if (!source.GetDictionary(keys::kTheme, &theme_value)) {
      *error = errors::kInvalidTheme;
      return false;
    }
    is_theme_ = true;

    DictionaryValue* images_value = NULL;
    if (theme_value->GetDictionary(keys::kThemeImages, &images_value)) {
      // Validate that the images are all strings
      for (DictionaryValue::key_iterator iter = images_value->begin_keys();
           iter != images_value->end_keys(); ++iter) {
        std::string val;
        if (!images_value->GetString(*iter, &val)) {
          *error = errors::kInvalidThemeImages;
          return false;
        }
      }
      theme_images_.reset(images_value->DeepCopy());
    }

    DictionaryValue* colors_value = NULL;
    if (theme_value->GetDictionary(keys::kThemeColors, &colors_value)) {
      // Validate that the colors are RGB or RGBA lists
      for (DictionaryValue::key_iterator iter = colors_value->begin_keys();
           iter != colors_value->end_keys(); ++iter) {
        ListValue* color_list = NULL;
        double alpha = 0.0;
        int alpha_int = 0;
        int color = 0;
        // The color must be a list
        if (!colors_value->GetListWithoutPathExpansion(*iter, &color_list) ||
            // And either 3 items (RGB) or 4 (RGBA)
            ((color_list->GetSize() != 3) &&
             ((color_list->GetSize() != 4) ||
              // For RGBA, the fourth item must be a real or int alpha value
              (!color_list->GetDouble(3, &alpha) &&
               !color_list->GetInteger(3, &alpha_int)))) ||
            // For both RGB and RGBA, the first three items must be ints (R,G,B)
            !color_list->GetInteger(0, &color) ||
            !color_list->GetInteger(1, &color) ||
            !color_list->GetInteger(2, &color)) {
          *error = errors::kInvalidThemeColors;
          return false;
        }
      }
      theme_colors_.reset(colors_value->DeepCopy());
    }

    DictionaryValue* tints_value = NULL;
    if (theme_value->GetDictionary(keys::kThemeTints, &tints_value)) {
      // Validate that the tints are all reals.
      for (DictionaryValue::key_iterator iter = tints_value->begin_keys();
           iter != tints_value->end_keys(); ++iter) {
        ListValue* tint_list = NULL;
        double v = 0.0;
        int vi = 0;
        if (!tints_value->GetListWithoutPathExpansion(*iter, &tint_list) ||
            tint_list->GetSize() != 3 ||
            !(tint_list->GetDouble(0, &v) || tint_list->GetInteger(0, &vi)) ||
            !(tint_list->GetDouble(1, &v) || tint_list->GetInteger(1, &vi)) ||
            !(tint_list->GetDouble(2, &v) || tint_list->GetInteger(2, &vi))) {
          *error = errors::kInvalidThemeTints;
          return false;
        }
      }
      theme_tints_.reset(tints_value->DeepCopy());
    }

    DictionaryValue* display_properties_value = NULL;
    if (theme_value->GetDictionary(keys::kThemeDisplayProperties,
        &display_properties_value)) {
      theme_display_properties_.reset(
          display_properties_value->DeepCopy());
    }

    return true;
  }

  // Initialize plugins (optional).
  if (source.HasKey(keys::kPlugins)) {
    ListValue* list_value = NULL;
    if (!source.GetList(keys::kPlugins, &list_value)) {
      *error = errors::kInvalidPlugins;
      return false;
    }

    for (size_t i = 0; i < list_value->GetSize(); ++i) {
      DictionaryValue* plugin_value = NULL;
      std::string path_str;
      bool is_public = false;

      if (!list_value->GetDictionary(i, &plugin_value)) {
        *error = errors::kInvalidPlugins;
        return false;
      }

      // Get plugins[i].path.
      if (!plugin_value->GetString(keys::kPluginsPath, &path_str)) {
        *error = ExtensionErrorUtils::FormatErrorMessage(
            errors::kInvalidPluginsPath, base::IntToString(i));
        return false;
      }

      // Get plugins[i].content (optional).
      if (plugin_value->HasKey(keys::kPluginsPublic)) {
        if (!plugin_value->GetBoolean(keys::kPluginsPublic, &is_public)) {
          *error = ExtensionErrorUtils::FormatErrorMessage(
              errors::kInvalidPluginsPublic, base::IntToString(i));
          return false;
        }
      }

      // We don't allow extension plugins to run on Chrome OS. We still
      // parse the manifest entry so that error messages are consistently
      // displayed across platforms.
#if !defined(OS_CHROMEOS)
      plugins_.push_back(PluginInfo());
      plugins_.back().path = path().AppendASCII(path_str);
      plugins_.back().is_public = is_public;
#endif
    }
  }

  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableExperimentalExtensionApis) &&
      source.HasKey(keys::kNaClModules)) {
    ListValue* list_value = NULL;
    if (!source.GetList(keys::kNaClModules, &list_value)) {
      *error = errors::kInvalidNaClModules;
      return false;
    }

    for (size_t i = 0; i < list_value->GetSize(); ++i) {
      DictionaryValue* module_value = NULL;
      std::string path_str;
      std::string mime_type;

      if (!list_value->GetDictionary(i, &module_value)) {
        *error = errors::kInvalidNaClModules;
        return false;
      }

      // Get nacl_modules[i].path.
      if (!module_value->GetString(keys::kNaClModulesPath, &path_str)) {
        *error = ExtensionErrorUtils::FormatErrorMessage(
            errors::kInvalidNaClModulesPath, base::IntToString(i));
        return false;
      }

      // Get nacl_modules[i].mime_type.
      if (!module_value->GetString(keys::kNaClModulesMIMEType, &mime_type)) {
        *error = ExtensionErrorUtils::FormatErrorMessage(
            errors::kInvalidNaClModulesMIMEType, base::IntToString(i));
        return false;
      }

      nacl_modules_.push_back(NaClModuleInfo());
      nacl_modules_.back().url = GetResourceURL(path_str);
      nacl_modules_.back().mime_type = mime_type;
    }
  }

  // Initialize toolstrips.  This is deprecated for public use.
  // NOTE(erikkay) Although deprecated, we intend to preserve this parsing
  // code indefinitely.  Please contact me or Joi for details as to why.
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableExperimentalExtensionApis) &&
      source.HasKey(keys::kToolstrips)) {
    ListValue* list_value = NULL;
    if (!source.GetList(keys::kToolstrips, &list_value)) {
      *error = errors::kInvalidToolstrips;
      return false;
    }

    for (size_t i = 0; i < list_value->GetSize(); ++i) {
      GURL toolstrip;
      DictionaryValue* toolstrip_value = NULL;
      std::string toolstrip_path;
      if (list_value->GetString(i, &toolstrip_path)) {
        // Support a simple URL value for backwards compatibility.
        toolstrip = GetResourceURL(toolstrip_path);
      } else if (list_value->GetDictionary(i, &toolstrip_value)) {
        if (!toolstrip_value->GetString(keys::kToolstripPath,
                                        &toolstrip_path)) {
          *error = ExtensionErrorUtils::FormatErrorMessage(
              errors::kInvalidToolstrip, base::IntToString(i));
          return false;
        }
        toolstrip = GetResourceURL(toolstrip_path);
      } else {
        *error = ExtensionErrorUtils::FormatErrorMessage(
            errors::kInvalidToolstrip, base::IntToString(i));
        return false;
      }
      toolstrips_.push_back(toolstrip);
    }
  }

  // Initialize content scripts (optional).
  if (source.HasKey(keys::kContentScripts)) {
    ListValue* list_value;
    if (!source.GetList(keys::kContentScripts, &list_value)) {
      *error = errors::kInvalidContentScriptsList;
      return false;
    }

    for (size_t i = 0; i < list_value->GetSize(); ++i) {
      DictionaryValue* content_script = NULL;
      if (!list_value->GetDictionary(i, &content_script)) {
        *error = ExtensionErrorUtils::FormatErrorMessage(
            errors::kInvalidContentScript, base::IntToString(i));
        return false;
      }

      UserScript script;
      if (!LoadUserScriptHelper(content_script, i, flags, error, &script))
        return false;  // Failed to parse script context definition.
      script.set_extension_id(id());
      if (converted_from_user_script_) {
        script.set_emulate_greasemonkey(true);
        script.set_match_all_frames(true);  // Greasemonkey matches all frames.
      }
      content_scripts_.push_back(script);
    }
  }

  // Initialize page action (optional).
  DictionaryValue* page_action_value = NULL;

  if (source.HasKey(keys::kPageActions)) {
    ListValue* list_value = NULL;
    if (!source.GetList(keys::kPageActions, &list_value)) {
      *error = errors::kInvalidPageActionsList;
      return false;
    }

    size_t list_value_length = list_value->GetSize();

    if (list_value_length == 0u) {
      // A list with zero items is allowed, and is equivalent to not having
      // a page_actions key in the manifest.  Don't set |page_action_value|.
    } else if (list_value_length == 1u) {
      if (!list_value->GetDictionary(0, &page_action_value)) {
        *error = errors::kInvalidPageAction;
        return false;
      }
    } else {  // list_value_length > 1u.
      *error = errors::kInvalidPageActionsListSize;
      return false;
    }
  } else if (source.HasKey(keys::kPageAction)) {
    if (!source.GetDictionary(keys::kPageAction, &page_action_value)) {
      *error = errors::kInvalidPageAction;
      return false;
    }
  }

  // If page_action_value is not NULL, then there was a valid page action.
  if (page_action_value) {
    page_action_.reset(
        LoadExtensionActionHelper(page_action_value, error));
    if (!page_action_.get())
      return false;  // Failed to parse page action definition.
  }

  // Initialize browser action (optional).
  if (source.HasKey(keys::kBrowserAction)) {
    DictionaryValue* browser_action_value = NULL;
    if (!source.GetDictionary(keys::kBrowserAction, &browser_action_value)) {
      *error = errors::kInvalidBrowserAction;
      return false;
    }

    browser_action_.reset(
        LoadExtensionActionHelper(browser_action_value, error));
    if (!browser_action_.get())
      return false;  // Failed to parse browser action definition.
  }

  // Initialize file browser actions (optional).
  if (source.HasKey(keys::kFileBrowserHandlers)) {
    ListValue* file_browser_handlers_value = NULL;
    if (!source.GetList(keys::kFileBrowserHandlers,
                              &file_browser_handlers_value)) {
      *error = errors::kInvalidFileBrowserHandler;
      return false;
    }

    file_browser_handlers_.reset(
        LoadFileBrowserHandlers(file_browser_handlers_value, error));
    if (!file_browser_handlers_.get())
      return false;  // Failed to parse file browser actions definition.
  }

  // Load App settings.
  if (!LoadIsApp(manifest_value_.get(), error) ||
      !LoadExtent(manifest_value_.get(), keys::kWebURLs,
                  &extent_,
                  errors::kInvalidWebURLs, errors::kInvalidWebURL,
                  parse_strictness, error) ||
      !EnsureNotHybridApp(manifest_value_.get(), error) ||
      !LoadLaunchURL(manifest_value_.get(), error) ||
      !LoadLaunchContainer(manifest_value_.get(), error) ||
      !LoadAppIsolation(manifest_value_.get(), error)) {
    return false;
  }

  // Initialize options page url (optional).
  // Funtion LoadIsApp() set is_app_ above.
  if (source.HasKey(keys::kOptionsPage)) {
    std::string options_str;
    if (!source.GetString(keys::kOptionsPage, &options_str)) {
      *error = errors::kInvalidOptionsPage;
      return false;
    }

    if (is_hosted_app()) {
      // hosted apps require an absolute URL.
      GURL options_url(options_str);
      if (!options_url.is_valid() ||
          !(options_url.SchemeIs("http") || options_url.SchemeIs("https"))) {
        *error = errors::kInvalidOptionsPageInHostedApp;
        return false;
      }
      options_url_ = options_url;
    } else {
      GURL absolute(options_str);
      if (absolute.is_valid()) {
        *error = errors::kInvalidOptionsPageExpectUrlInPackage;
        return false;
      }
      options_url_ = GetResourceURL(options_str);
      if (!options_url_.is_valid()) {
        *error = errors::kInvalidOptionsPage;
        return false;
      }
    }
  }

  // Initialize the permissions (optional).
  if (source.HasKey(keys::kPermissions)) {
    ListValue* permissions = NULL;
    if (!source.GetList(keys::kPermissions, &permissions)) {
      *error = ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidPermissions, "");
      return false;
    }

    for (size_t i = 0; i < permissions->GetSize(); ++i) {
      std::string permission_str;
      if (!permissions->GetString(i, &permission_str)) {
        *error = ExtensionErrorUtils::FormatErrorMessage(
            errors::kInvalidPermission, base::IntToString(i));
        return false;
      }

      // Only COMPONENT extensions can use private APIs.
      // TODO(asargent) - We want a more general purpose mechanism for this,
      // and better error messages. (http://crbug.com/54013)
      if (!IsComponentOnlyPermission(permission_str)
#ifndef NDEBUG
           && !CommandLine::ForCurrentProcess()->HasSwitch(
                 switches::kExposePrivateExtensionApi)
#endif
          ) {
        continue;
      }

      // Remap the old unlimited storage permission name.
      if (permission_str == kOldUnlimitedStoragePermission)
        permission_str = kUnlimitedStoragePermission;

      if (web_extent().is_empty() || location() == Extension::COMPONENT) {
        // Check if it's a module permission.  If so, enable that permission.
        if (IsAPIPermission(permission_str)) {
          // Only allow the experimental API permission if the command line
          // flag is present, or if the extension is a component of Chrome.
          if (permission_str == Extension::kExperimentalPermission &&
              !CommandLine::ForCurrentProcess()->HasSwitch(
                switches::kEnableExperimentalExtensionApis) &&
              location() != Extension::COMPONENT) {
            *error = errors::kExperimentalFlagRequired;
            return false;
          }
          api_permissions_.insert(permission_str);
          continue;
        }
      } else {
        // Hosted apps only get access to a subset of the valid permissions.
        if (IsHostedAppPermission(permission_str)) {
          api_permissions_.insert(permission_str);
          continue;
        }
      }

      // Check if it's a host pattern permission.
      URLPattern pattern = URLPattern(CanExecuteScriptEverywhere() ?
          URLPattern::SCHEME_ALL : kValidHostPermissionSchemes);

      URLPattern::ParseResult parse_result = pattern.Parse(permission_str,
                                                           parse_strictness);
      if (parse_result == URLPattern::PARSE_SUCCESS) {
        if (!CanSpecifyHostPermission(pattern)) {
          *error = ExtensionErrorUtils::FormatErrorMessage(
              errors::kInvalidPermissionScheme, base::IntToString(i));
          return false;
        }

        // The path component is not used for host permissions, so we force it
        // to match all paths.
        pattern.SetPath("/*");

        if (pattern.MatchesScheme(chrome::kFileScheme) &&
            !CanExecuteScriptEverywhere()) {
          wants_file_access_ = true;
          if (!(flags & ALLOW_FILE_ACCESS))
            pattern.set_valid_schemes(
                pattern.valid_schemes() & ~URLPattern::SCHEME_FILE);
        }

        host_permissions_.push_back(pattern);
      }

      // If it's not a host permission, then it's probably an unknown API
      // permission. Do not throw an error so extensions can retain
      // backwards compatability (http://crbug.com/42742).
      // TODO(jstritar): We can improve error messages by adding better
      // validation of API permissions here.
      // TODO(skerner): Consider showing the reason |permission_str| is not
      // a valid URL pattern if it is almost valid.  For example, if it has
      // a valid scheme, and failed to parse because it has a port, show an
      // error.
    }
  }

  // Initialize background url (optional).
  if (source.HasKey(keys::kBackground)) {
    std::string background_str;
    if (!source.GetString(keys::kBackground, &background_str)) {
      *error = errors::kInvalidBackground;
      return false;
    }

    if (is_hosted_app()) {
      // Make sure "background" permission is set.
      if (api_permissions_.find(kBackgroundPermission) ==
          api_permissions_.end()) {
        *error = errors::kBackgroundPermissionNeeded;
        return false;
      }
      // Hosted apps require an absolute URL.
      GURL bg_page(background_str);
      if (!bg_page.is_valid()) {
        *error = errors::kInvalidBackgroundInHostedApp;
        return false;
      }

      if (!(bg_page.SchemeIs("https") ||
           (CommandLine::ForCurrentProcess()->HasSwitch(
                switches::kAllowHTTPBackgroundPage) &&
            bg_page.SchemeIs("http")))) {
        *error = errors::kInvalidBackgroundInHostedApp;
        return false;
      }
      background_url_ = bg_page;
    } else {
      background_url_ = GetResourceURL(background_str);
    }
  }

  if (source.HasKey(keys::kDefaultLocale)) {
    if (!source.GetString(keys::kDefaultLocale, &default_locale_) ||
        !l10n_util::IsValidLocaleSyntax(default_locale_)) {
      *error = errors::kInvalidDefaultLocale;
      return false;
    }
  }

  // Chrome URL overrides (optional)
  if (source.HasKey(keys::kChromeURLOverrides)) {
    DictionaryValue* overrides = NULL;
    if (!source.GetDictionary(keys::kChromeURLOverrides, &overrides)) {
      *error = errors::kInvalidChromeURLOverrides;
      return false;
    }

    // Validate that the overrides are all strings
    for (DictionaryValue::key_iterator iter = overrides->begin_keys();
         iter != overrides->end_keys(); ++iter) {
      std::string page = *iter;
      std::string val;
      // Restrict override pages to a list of supported URLs.
      if ((page != chrome::kChromeUINewTabHost &&
#if defined(TOUCH_UI)
           page != chrome::kChromeUIKeyboardHost &&
#endif
#if defined(OS_CHROMEOS)
           page != chrome::kChromeUIActivationMessageHost &&
#endif
           page != chrome::kChromeUIBookmarksHost &&
           page != chrome::kChromeUIHistoryHost) ||
          !overrides->GetStringWithoutPathExpansion(*iter, &val)) {
        *error = errors::kInvalidChromeURLOverrides;
        return false;
      }
      // Replace the entry with a fully qualified chrome-extension:// URL.
      chrome_url_overrides_[page] = GetResourceURL(val);
    }

    // An extension may override at most one page.
    if (overrides->size() > 1) {
      *error = errors::kMultipleOverrides;
      return false;
    }
  }

  if (source.HasKey(keys::kOmnibox)) {
    if (!source.GetString(keys::kOmniboxKeyword, &omnibox_keyword_) ||
        omnibox_keyword_.empty()) {
      *error = errors::kInvalidOmniboxKeyword;
      return false;
    }
  }

  // Initialize devtools page url (optional).
  if (source.HasKey(keys::kDevToolsPage)) {
    std::string devtools_str;
    if (!source.GetString(keys::kDevToolsPage, &devtools_str)) {
      *error = errors::kInvalidDevToolsPage;
      return false;
    }
    if (!HasApiPermission(Extension::kExperimentalPermission)) {
      *error = errors::kDevToolsExperimental;
      return false;
    }
    devtools_url_ = GetResourceURL(devtools_str);
  }

  // Initialize sidebar action (optional).
  if (source.HasKey(keys::kSidebar)) {
    DictionaryValue* sidebar_value = NULL;
    if (!source.GetDictionary(keys::kSidebar, &sidebar_value)) {
      *error = errors::kInvalidSidebar;
      return false;
    }
    if (!HasApiPermission(Extension::kExperimentalPermission)) {
      *error = errors::kSidebarExperimental;
      return false;
    }
    sidebar_defaults_.reset(LoadExtensionSidebarDefaults(sidebar_value, error));
    if (!sidebar_defaults_.get())
      return false;  // Failed to parse sidebar definition.
  }

  // Initialize text-to-speech voices (optional).
  if (source.HasKey(keys::kTts)) {
    DictionaryValue* tts_dict = NULL;
    if (!source.GetDictionary(keys::kTts, &tts_dict)) {
      *error = errors::kInvalidTts;
      return false;
    }

    if (tts_dict->HasKey(keys::kTtsVoices)) {
      ListValue* tts_voices = NULL;
      if (!tts_dict->GetList(keys::kTtsVoices, &tts_voices)) {
        *error = errors::kInvalidTtsVoices;
        return false;
      }

      for (size_t i = 0; i < tts_voices->GetSize(); i++) {
        DictionaryValue* one_tts_voice = NULL;
        if (!tts_voices->GetDictionary(i, &one_tts_voice)) {
          *error = errors::kInvalidTtsVoices;
          return false;
        }

        TtsVoice voice_data;
        if (one_tts_voice->HasKey(keys::kTtsVoicesVoiceName)) {
          if (!one_tts_voice->GetString(
                  keys::kTtsVoicesVoiceName, &voice_data.voice_name)) {
            *error = errors::kInvalidTtsVoicesVoiceName;
            return false;
          }
        }
        if (one_tts_voice->HasKey(keys::kTtsVoicesLocale)) {
          if (!one_tts_voice->GetString(
                  keys::kTtsVoicesLocale, &voice_data.locale) ||
              !l10n_util::IsValidLocaleSyntax(voice_data.locale)) {
            *error = errors::kInvalidTtsVoicesLocale;
            return false;
          }
        }
        if (one_tts_voice->HasKey(keys::kTtsVoicesGender)) {
          if (!one_tts_voice->GetString(
                  keys::kTtsVoicesGender, &voice_data.gender) ||
              (voice_data.gender != keys::kTtsGenderMale &&
               voice_data.gender != keys::kTtsGenderFemale)) {
            *error = errors::kInvalidTtsVoicesGender;
            return false;
          }
        }

        tts_voices_.push_back(voice_data);
      }
    }
  }

  // Initialize incognito behavior. Apps default to split mode, extensions
  // default to spanning.
  incognito_split_mode_ = is_app();
  if (source.HasKey(keys::kIncognito)) {
    std::string value;
    if (!source.GetString(keys::kIncognito, &value)) {
      *error = errors::kInvalidIncognitoBehavior;
      return false;
    }
    if (value == values::kIncognitoSpanning) {
      incognito_split_mode_ = false;
    } else if (value == values::kIncognitoSplit) {
      incognito_split_mode_ = true;
    } else {
      *error = errors::kInvalidIncognitoBehavior;
      return false;
    }
  }

  if (HasMultipleUISurfaces()) {
    *error = errors::kOneUISurfaceOnly;
    return false;
  }

  InitEffectiveHostPermissions();

  // Although |source| is passed in as a const, it's still possible to modify
  // it.  This is dangerous since the utility process re-uses |source| after
  // it calls InitFromValue, passing it up to the browser process which calls
  // InitFromValue again.  As a result, we need to make sure that nobody
  // accidentally modifies it.
  DCHECK(source.Equals(manifest_value_.get()));

  return true;
}

// static
std::string Extension::ChromeStoreLaunchURL() {
  std::string gallery_prefix = extension_urls::kGalleryBrowsePrefix;
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAppsGalleryURL))
    gallery_prefix = CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
        switches::kAppsGalleryURL);
  if (EndsWith(gallery_prefix, "/", true))
    gallery_prefix = gallery_prefix.substr(0, gallery_prefix.length() - 1);
  return gallery_prefix;
}

GURL Extension::GetHomepageURL() const {
  if (homepage_url_.is_valid())
    return homepage_url_;

  if (!UpdatesFromGallery())
    return GURL();

  // TODO(erikkay): This may not be entirely correct with the webstore.
  // I think it will have a mixture of /extensions/detail and /webstore/detail
  // URLs.  Perhaps they'll handle this nicely with redirects?
  GURL url(ChromeStoreLaunchURL() + std::string("/detail/") + id());
  return url;
}

std::set<FilePath> Extension::GetBrowserImages() const {
  std::set<FilePath> image_paths;
  // TODO(viettrungluu): These |FilePath::FromWStringHack(UTF8ToWide())|
  // indicate that we're doing something wrong.

  // Extension icons.
  for (ExtensionIconSet::IconMap::const_iterator iter = icons().map().begin();
       iter != icons().map().end(); ++iter) {
    image_paths.insert(FilePath::FromWStringHack(UTF8ToWide(iter->second)));
  }

  // Theme images.
  DictionaryValue* theme_images = GetThemeImages();
  if (theme_images) {
    for (DictionaryValue::key_iterator it = theme_images->begin_keys();
         it != theme_images->end_keys(); ++it) {
      std::string val;
      if (theme_images->GetStringWithoutPathExpansion(*it, &val))
        image_paths.insert(FilePath::FromWStringHack(UTF8ToWide(val)));
    }
  }

  // Page action icons.
  if (page_action()) {
    std::vector<std::string>* icon_paths = page_action()->icon_paths();
    for (std::vector<std::string>::iterator iter = icon_paths->begin();
         iter != icon_paths->end(); ++iter) {
      image_paths.insert(FilePath::FromWStringHack(UTF8ToWide(*iter)));
    }
  }

  // Browser action icons.
  if (browser_action()) {
    std::vector<std::string>* icon_paths = browser_action()->icon_paths();
    for (std::vector<std::string>::iterator iter = icon_paths->begin();
         iter != icon_paths->end(); ++iter) {
      image_paths.insert(FilePath::FromWStringHack(UTF8ToWide(*iter)));
    }
  }

  return image_paths;
}

GURL Extension::GetFullLaunchURL() const {
  if (!launch_local_path().empty())
    return url().Resolve(launch_local_path());
  else
    return GURL(launch_web_url());
}

static std::string SizeToString(const gfx::Size& max_size) {
  return base::IntToString(max_size.width()) + "x" +
         base::IntToString(max_size.height());
}

// static
void Extension::SetScriptingWhitelist(
    const Extension::ScriptingWhitelist& whitelist) {
  ScriptingWhitelist* current_whitelist =
      ExtensionConfig::GetInstance()->whitelist();
  current_whitelist->clear();
  for (ScriptingWhitelist::const_iterator it = whitelist.begin();
       it != whitelist.end(); ++it) {
    current_whitelist->push_back(*it);
  }
}

// static
const Extension::ScriptingWhitelist* Extension::GetScriptingWhitelist() {
  return ExtensionConfig::GetInstance()->whitelist();
}

void Extension::SetCachedImage(const ExtensionResource& source,
                               const SkBitmap& image,
                               const gfx::Size& original_size) const {
  DCHECK(source.extension_root() == path());  // The resource must come from
                                              // this extension.
  const FilePath& path = source.relative_path();
  gfx::Size actual_size(image.width(), image.height());
  if (actual_size == original_size) {
    image_cache_[ImageCacheKey(path, std::string())] = image;
  } else {
    image_cache_[ImageCacheKey(path, SizeToString(actual_size))] = image;
  }
}

bool Extension::HasCachedImage(const ExtensionResource& source,
                               const gfx::Size& max_size) const {
  DCHECK(source.extension_root() == path());  // The resource must come from
                                              // this extension.
  return GetCachedImageImpl(source, max_size) != NULL;
}

SkBitmap Extension::GetCachedImage(const ExtensionResource& source,
                                   const gfx::Size& max_size) const {
  DCHECK(source.extension_root() == path());  // The resource must come from
                                              // this extension.
  SkBitmap* image = GetCachedImageImpl(source, max_size);
  return image ? *image : SkBitmap();
}

SkBitmap* Extension::GetCachedImageImpl(const ExtensionResource& source,
                                        const gfx::Size& max_size) const {
  const FilePath& path = source.relative_path();

  // Look for exact size match.
  ImageCache::iterator i = image_cache_.find(
      ImageCacheKey(path, SizeToString(max_size)));
  if (i != image_cache_.end())
    return &(i->second);

  // If we have the original size version cached, return that if it's small
  // enough.
  i = image_cache_.find(ImageCacheKey(path, std::string()));
  if (i != image_cache_.end()) {
    SkBitmap& image = i->second;
    if (image.width() <= max_size.width() &&
        image.height() <= max_size.height())
      return &(i->second);
  }

  return NULL;
}

ExtensionResource Extension::GetIconResource(
    int size, ExtensionIconSet::MatchType match_type) const {
  std::string path = icons().Get(size, match_type);
  if (path.empty())
    return ExtensionResource();
  return GetResource(path);
}

GURL Extension::GetIconURL(int size,
                           ExtensionIconSet::MatchType match_type) const {
  std::string path = icons().Get(size, match_type);
  if (path.empty())
    return GURL();
  else
    return GetResourceURL(path);
}

bool Extension::CanSpecifyHostPermission(const URLPattern& pattern) const {
  if (!pattern.match_all_urls() &&
      pattern.MatchesScheme(chrome::kChromeUIScheme)) {
    // Only allow access to chrome://favicon to regular extensions. Component
    // extensions can have access to all of chrome://*.
    return (pattern.host() == chrome::kChromeUIFaviconHost ||
            CanExecuteScriptEverywhere());
  }

  // Otherwise, the valid schemes were handled by URLPattern.
  return true;
}

// static
bool Extension::HasApiPermission(
    const std::set<std::string>& api_permissions,
    const std::string& function_name) {
  std::string permission_name = function_name;

  for (size_t i = 0; i < kNumNonPermissionFunctionNames; ++i) {
    if (permission_name == kNonPermissionFunctionNames[i])
      return true;
  }

  // See if this is a function or event name first and strip out the package.
  // Functions will be of the form package.function
  // Events will be of the form package/id or package.optional.stuff
  size_t separator = function_name.find_first_of("./");
  if (separator != std::string::npos)
    permission_name = function_name.substr(0, separator);

  // windows and tabs are the same permission.
  if (permission_name == kWindowPermission)
    permission_name = Extension::kTabPermission;

  if (api_permissions.count(permission_name))
    return true;

  for (size_t i = 0; i < kNumNonPermissionModuleNames; ++i) {
    if (permission_name == kNonPermissionModuleNames[i]) {
      return true;
    }
  }

  return false;
}

bool Extension::HasHostPermission(const GURL& url) const {
  for (URLPatternList::const_iterator host = host_permissions().begin();
       host != host_permissions().end(); ++host) {
    // Non-component extensions can only access chrome://favicon and no other
    // chrome:// scheme urls.
    if (url.SchemeIs(chrome::kChromeUIScheme) &&
        url.host() != chrome::kChromeUIFaviconHost &&
        location() != Extension::COMPONENT)
      return false;

    if (host->MatchesUrl(url))
      return true;
  }
  return false;
}

void Extension::InitEffectiveHostPermissions() {
  // Some APIs effectively grant access to every site.  New ones should be
  // added here.  (I'm looking at you, network API)
  if (HasApiPermission(api_permissions_, kProxyPermission) ||
      !devtools_url_.is_empty()) {
    URLPattern all_urls(URLPattern::SCHEME_ALL);
    all_urls.set_match_all_urls(true);
    effective_host_permissions_.AddPattern(all_urls);
    return;
  }

  for (URLPatternList::const_iterator host = host_permissions().begin();
       host != host_permissions().end(); ++host)
    effective_host_permissions_.AddPattern(*host);

  for (UserScriptList::const_iterator content_script =
           content_scripts().begin();
       content_script != content_scripts().end(); ++content_script) {
    UserScript::PatternList::const_iterator pattern =
        content_script->url_patterns().begin();
    for (; pattern != content_script->url_patterns().end(); ++pattern)
      effective_host_permissions_.AddPattern(*pattern);
  }
}

bool Extension::IsComponentOnlyPermission
    (const std::string& permission) const {
  if (location() == Extension::COMPONENT)
    return true;

  // Non-component extensions are not allowed to access private apis.
  for (size_t i = 0; i < Extension::kNumComponentPrivatePermissions; ++i) {
    if (permission == Extension::kComponentPrivatePermissionNames[i])
      return false;
  }
  return true;
}

bool Extension::HasMultipleUISurfaces() const {
  int num_surfaces = 0;

  if (page_action())
    ++num_surfaces;

  if (browser_action())
    ++num_surfaces;

  if (is_app())
    ++num_surfaces;

  return num_surfaces > 1;
}

bool Extension::CanExecuteScriptOnPage(const GURL& page_url,
                                       const UserScript* script,
                                       std::string* error) const {
  // The gallery is special-cased as a restricted URL for scripting to prevent
  // access to special JS bindings we expose to the gallery (and avoid things
  // like extensions removing the "report abuse" link).
  // TODO(erikkay): This seems like the wrong test.  Shouldn't we we testing
  // against the store app extent?
  if ((page_url.host() == GURL(Extension::ChromeStoreLaunchURL()).host()) &&
      !CanExecuteScriptEverywhere() &&
      !CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kAllowScriptingGallery)) {
    if (error)
      *error = errors::kCannotScriptGallery;
    return false;
  }

  if (page_url.SchemeIs(chrome::kChromeUIScheme) &&
      !CanExecuteScriptEverywhere())
    return false;

  // If a script is specified, use its matches.
  if (script)
    return script->MatchesUrl(page_url);

  // Otherwise, see if this extension has permission to execute script
  // programmatically on pages.
  for (size_t i = 0; i < host_permissions_.size(); ++i) {
    if (host_permissions_[i].MatchesUrl(page_url))
      return true;
  }

  if (error) {
    *error = ExtensionErrorUtils::FormatErrorMessage(errors::kCannotAccessPage,
                                                     page_url.spec());
  }

  return false;
}

// static
bool Extension::HasEffectiveAccessToAllHosts(
    const ExtensionExtent& effective_host_permissions,
    const std::set<std::string>& api_permissions) {
  const URLPatternList patterns = effective_host_permissions.patterns();
  for (URLPatternList::const_iterator host = patterns.begin();
       host != patterns.end(); ++host) {
    if (host->match_all_urls() ||
        (host->match_subdomains() && host->host().empty()))
      return true;
  }

  return false;
}

bool Extension::HasEffectiveAccessToAllHosts() const {
  return HasEffectiveAccessToAllHosts(GetEffectiveHostPermissions(),
                                      api_permissions());
}

bool Extension::HasFullPermissions() const {
  return !plugins().empty();
}

bool Extension::ShowConfigureContextMenus() const {
  // Don't show context menu for component extensions. We might want to show
  // options for component extension button but now there is no component
  // extension with options. All other menu items like uninstall have
  // no sense for component extensions.
  return location() != Extension::COMPONENT;
}

bool Extension::IsAPIPermission(const std::string& str) const {
  for (size_t i = 0; i < Extension::kNumPermissions; ++i) {
    if (str == Extension::kPermissions[i].name) {
      return true;
    }
  }
  return false;
}

bool Extension::CanExecuteScriptEverywhere() const {
  if (location() == Extension::COMPONENT
#ifndef NDEBUG
      || CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kExposePrivateExtensionApi)
#endif
      )
    return true;

  ScriptingWhitelist* whitelist =
      ExtensionConfig::GetInstance()->whitelist();

  for (ScriptingWhitelist::const_iterator it = whitelist->begin();
       it != whitelist->end(); ++it) {
    if (id() == *it) {
      return true;
    }
  }

  return false;
}

bool Extension::CanCaptureVisiblePage(const GURL& page_url,
                                      std::string *error) const {
  if (HasHostPermission(page_url) || page_url.GetOrigin() == url())
    return true;

  if (error) {
    *error = ExtensionErrorUtils::FormatErrorMessage(errors::kCannotAccessPage,
                                                     page_url.spec());
  }
  return false;
}

bool Extension::UpdatesFromGallery() const {
  return update_url() == GalleryUpdateUrl(false) ||
         update_url() == GalleryUpdateUrl(true);
}

bool Extension::OverlapsWithOrigin(const GURL& origin) const {
  if (url() == origin)
    return true;

  if (web_extent().is_empty())
    return false;

  // Note: patterns and extents ignore port numbers.
  URLPattern origin_only_pattern(kValidWebExtentSchemes);
  if (!origin_only_pattern.SetScheme(origin.scheme()))
    return false;
  origin_only_pattern.set_host(origin.host());
  origin_only_pattern.SetPath("/*");

  ExtensionExtent origin_only_pattern_list;
  origin_only_pattern_list.AddPattern(origin_only_pattern);

  return web_extent().OverlapsWith(origin_only_pattern_list);
}

ExtensionInfo::ExtensionInfo(const DictionaryValue* manifest,
                             const std::string& id,
                             const FilePath& path,
                             Extension::Location location)
    : extension_id(id),
      extension_path(path),
      extension_location(location) {
  if (manifest)
    extension_manifest.reset(manifest->DeepCopy());
}

ExtensionInfo::~ExtensionInfo() {}

UninstalledExtensionInfo::UninstalledExtensionInfo(
    const Extension& extension)
    : extension_id(extension.id()),
      extension_api_permissions(extension.api_permissions()),
      extension_type(extension.GetType()),
      update_url(extension.update_url()) {}

UninstalledExtensionInfo::~UninstalledExtensionInfo() {}


UnloadedExtensionInfo::UnloadedExtensionInfo(
    const Extension* extension,
    Reason reason)
  : reason(reason),
    already_disabled(false),
    extension(extension) {}
