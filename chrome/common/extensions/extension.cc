// Copyright (c) 2012 The Chromium Authors. All rights reserved.
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
#include "base/stl_util.h"
#include "base/string16.h"
#include "base/string_number_conversions.h"
#include "base/string_piece.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "base/version.h"
#include "crypto/sha2.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/extensions/csp_validator.h"
#include "chrome/common/extensions/extension_action.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_error_utils.h"
#include "chrome/common/extensions/extension_l10n_util.h"
#include "chrome/common/extensions/extension_resource.h"
#include "chrome/common/extensions/file_browser_handler.h"
#include "chrome/common/extensions/manifest.h"
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
#include "webkit/glue/web_intent_service_data.h"

namespace keys = extension_manifest_keys;
namespace values = extension_manifest_values;
namespace errors = extension_manifest_errors;

using extensions::csp_validator::ContentSecurityPolicyIsLegal;
using extensions::csp_validator::ContentSecurityPolicyIsSecure;

namespace {

const int kModernManifestVersion = 1;
const int kPEMOutputColumns = 65;

// KEY MARKERS
const char kKeyBeginHeaderMarker[] = "-----BEGIN";
const char kKeyBeginFooterMarker[] = "-----END";
const char kKeyInfoEndMarker[] = "KEY-----";
const char kPublic[] = "PUBLIC";
const char kPrivate[] = "PRIVATE";

const int kRSAKeySize = 1024;

const char kDefaultContentSecurityPolicy[] =
    "script-src 'self'; object-src 'self'";

// Converts a normal hexadecimal string into the alphabet used by extensions.
// We use the characters 'a'-'p' instead of '0'-'f' to avoid ever having a
// completely numeric host, since some software interprets that as an IP
// address.
static void ConvertHexadecimalToIDAlphabet(std::string* id) {
  for (size_t i = 0; i < id->size(); ++i) {
    int val;
    if (base::HexStringToInt(base::StringPiece(id->begin() + i,
                                               id->begin() + i + 1),
                             &val)) {
      (*id)[i] = val + 'a';
    } else {
      (*id)[i] = 'a';
    }
  }
}

// A singleton object containing global data needed by the extension objects.
class ExtensionConfig {
 public:
  static ExtensionConfig* GetInstance() {
    return Singleton<ExtensionConfig>::get();
  }

  Extension::ScriptingWhitelist* whitelist() { return &scripting_whitelist_; }

 private:
  friend struct DefaultSingletonTraits<ExtensionConfig>;

  ExtensionConfig() {
    // Whitelist ChromeVox, an accessibility extension from Google that needs
    // the ability to script webui pages. This is temporary and is not
    // meant to be a general solution.
    // TODO(dmazzoni): remove this once we have an extension API that
    // allows any extension to request read-only access to webui pages.
    scripting_whitelist_.push_back("kgejglhpjiefppelpmljglcjbhoiplfn");
  }
  ~ExtensionConfig() { }

  // A whitelist of extensions that can script anywhere. Do not add to this
  // list (except in tests) without consulting the Extensions team first.
  // Note: Component extensions have this right implicitly and do not need to be
  // added to this list.
  Extension::ScriptingWhitelist scripting_whitelist_;
};

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

const int Extension::kValidWebExtentSchemes =
    URLPattern::SCHEME_HTTP | URLPattern::SCHEME_HTTPS;

const int Extension::kValidHostPermissionSchemes =
    UserScript::kValidUserScriptSchemes | URLPattern::SCHEME_CHROMEUI;

Extension::InputComponentInfo::InputComponentInfo()
    : type(INPUT_COMPONENT_TYPE_NONE),
      shortcut_alt(false),
      shortcut_ctrl(false),
      shortcut_shift(false) {
}

Extension::InputComponentInfo::~InputComponentInfo() {}

Extension::TtsVoice::TtsVoice() {}
Extension::TtsVoice::~TtsVoice() {}

//
// Extension
//

// static
scoped_refptr<Extension> Extension::Create(const FilePath& path,
                                           Location location,
                                           const DictionaryValue& value,
                                           int flags,
                                           std::string* utf8_error) {
  return Extension::Create(path,
                           location,
                           value,
                           flags,
                           std::string(),  // ID is ignored if empty.
                           utf8_error);
}

scoped_refptr<Extension> Extension::Create(const FilePath& path,
                                           Location location,
                                           const DictionaryValue& value,
                                           int flags,
                                           const std::string& explicit_id,
                                           std::string* utf8_error) {
  DCHECK(utf8_error);
  string16 error;
  scoped_refptr<Extension> extension = new Extension(path, location);
  extension->id_ = explicit_id;
  if (!extension->InitFromValue(new extensions::Manifest(value.DeepCopy()),
                                flags, &error)) {
    *utf8_error = UTF16ToUTF8(error);
    return NULL;
  }
  return extension;
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

  // Highest rank has highest priority.
  return (loc1_rank > loc2_rank ? loc1 : loc2 );
}

void Extension::OverrideLaunchUrl(const GURL& override_url) {
  GURL new_url(override_url);
  if (!new_url.is_valid()) {
    DLOG(WARNING) << "Invalid override url given for " << name();
  } else {
    if (new_url.has_port()) {
      DLOG(WARNING) << "Override URL passed for " << name()
                    << " should not contain a port.  Removing it.";

      GURL::Replacements remove_port;
      remove_port.ClearPort();
      new_url = new_url.ReplaceComponents(remove_port);
    }

    launch_web_url_ = new_url.spec();

    URLPattern pattern(kValidWebExtentSchemes);
    pattern.Parse(new_url.spec());
    pattern.SetPath(pattern.path() + '*');
    extent_.AddPattern(pattern);
  }
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
  if (is_platform_app())
    return TYPE_PLATFORM_APP;
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

GURL Extension::GetBackgroundURL() const {
  if (!background_scripts_.empty()) {
    return GetResourceURL(
        extension_filenames::kGeneratedBackgroundPageFilename);
  } else {
    return background_url_;
  }
}

bool Extension::IsResourceWebAccessible(const std::string& relative_path)
    const {
  // For old manifest versions which do not specify web_accessible_resources
  // we always allow resource loads.
  if (manifest_version() < 2 && !HasWebAccessibleResources())
    return true;

  if (web_accessible_resources_.find(relative_path) !=
      web_accessible_resources_.end())
    return true;

  return false;
}

bool Extension::HasWebAccessibleResources() const {
  if (web_accessible_resources_.size())
    return true;

  return false;
}

bool Extension::GenerateId(const std::string& input, std::string* output) {
  DCHECK(output);
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
                                     string16* error,
                                     UserScript* result) {
  // run_at
  if (content_script->HasKey(keys::kRunAt)) {
    std::string run_location;
    if (!content_script->GetString(keys::kRunAt, &run_location)) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidRunAt,
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
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidRunAt,
          base::IntToString(definition_index));
      return false;
    }
  }

  // all frames
  if (content_script->HasKey(keys::kAllFrames)) {
    bool all_frames = false;
    if (!content_script->GetBoolean(keys::kAllFrames, &all_frames)) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidAllFrames, base::IntToString(definition_index));
      return false;
    }
    result->set_match_all_frames(all_frames);
  }

  // matches (required)
  ListValue* matches = NULL;
  if (!content_script->GetList(keys::kMatches, &matches)) {
    *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
        errors::kInvalidMatches,
        base::IntToString(definition_index));
    return false;
  }

  if (matches->GetSize() == 0) {
    *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
        errors::kInvalidMatchCount,
        base::IntToString(definition_index));
    return false;
  }
  for (size_t j = 0; j < matches->GetSize(); ++j) {
    std::string match_str;
    if (!matches->GetString(j, &match_str)) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidMatch,
          base::IntToString(definition_index),
          base::IntToString(j),
          errors::kExpectString);
      return false;
    }

    URLPattern pattern(UserScript::kValidUserScriptSchemes);
    if (CanExecuteScriptEverywhere())
      pattern.SetValidSchemes(URLPattern::SCHEME_ALL);

    URLPattern::ParseResult parse_result = pattern.Parse(match_str);
    if (parse_result != URLPattern::PARSE_SUCCESS) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
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
        pattern.SetValidSchemes(
            pattern.valid_schemes() & ~URLPattern::SCHEME_FILE);
    }

    result->add_url_pattern(pattern);
  }

  // exclude_matches
  if (content_script->HasKey(keys::kExcludeMatches)) {  // optional
    ListValue* exclude_matches = NULL;
    if (!content_script->GetList(keys::kExcludeMatches, &exclude_matches)) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidExcludeMatches,
          base::IntToString(definition_index));
      return false;
    }

    for (size_t j = 0; j < exclude_matches->GetSize(); ++j) {
      std::string match_str;
      if (!exclude_matches->GetString(j, &match_str)) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidExcludeMatch,
            base::IntToString(definition_index),
            base::IntToString(j),
            errors::kExpectString);
        return false;
      }

      URLPattern pattern(UserScript::kValidUserScriptSchemes);
      if (CanExecuteScriptEverywhere())
        pattern.SetValidSchemes(URLPattern::SCHEME_ALL);
      URLPattern::ParseResult parse_result = pattern.Parse(match_str);
      if (parse_result != URLPattern::PARSE_SUCCESS) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidExcludeMatch,
            base::IntToString(definition_index), base::IntToString(j),
            URLPattern::GetParseResultString(parse_result));
        return false;
      }

      result->add_exclude_url_pattern(pattern);
    }
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
    *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
        errors::kInvalidJsList,
        base::IntToString(definition_index));
    return false;
  }

  ListValue* css = NULL;
  if (content_script->HasKey(keys::kCss) &&
      !content_script->GetList(keys::kCss, &css)) {
    *error = ExtensionErrorUtils::
        FormatErrorMessageUTF16(errors::kInvalidCssList,
        base::IntToString(definition_index));
    return false;
  }

  // The manifest needs to have at least one js or css user script definition.
  if (((js ? js->GetSize() : 0) + (css ? css->GetSize() : 0)) == 0) {
    *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
        errors::kMissingFile,
        base::IntToString(definition_index));
    return false;
  }

  if (js) {
    for (size_t script_index = 0; script_index < js->GetSize();
         ++script_index) {
      Value* value;
      std::string relative;
      if (!js->Get(script_index, &value) || !value->GetAsString(&relative)) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidJs,
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
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidCss,
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
    string16* error,
    void(UserScript::*add_method)(const std::string& glob),
    UserScript *instance) {
  if (!content_script->HasKey(globs_property_name))
    return true;  // they are optional

  ListValue* list = NULL;
  if (!content_script->GetList(globs_property_name, &list)) {
    *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
        errors::kInvalidGlobList,
        base::IntToString(content_script_index),
        globs_property_name);
    return false;
  }

  for (size_t i = 0; i < list->GetSize(); ++i) {
    std::string glob;
    if (!list->GetString(i, &glob)) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidGlob,
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
    const DictionaryValue* extension_action, string16* error) {
  scoped_ptr<ExtensionAction> result(new ExtensionAction());
  result->set_extension_id(id());

  // Page actions are hidden by default, and browser actions ignore
  // visibility.
  result->SetIsVisible(ExtensionAction::kDefaultTabId, false);

  if (manifest_version_ == 1) {
    ListValue* icons = NULL;
    if (extension_action->HasKey(keys::kPageActionIcons) &&
        extension_action->GetList(keys::kPageActionIcons, &icons)) {
      for (ListValue::const_iterator iter = icons->begin();
           iter != icons->end(); ++iter) {
        std::string path;
        if (!(*iter)->GetAsString(&path) || path.empty()) {
          *error = ASCIIToUTF16(errors::kInvalidPageActionIconPath);
          return NULL;
        }

        result->icon_paths()->push_back(path);
      }
    }

    std::string id;
    if (extension_action->HasKey(keys::kPageActionId)) {
      if (!extension_action->GetString(keys::kPageActionId, &id)) {
        *error = ASCIIToUTF16(errors::kInvalidPageActionId);
        return NULL;
      }
      result->set_id(id);
    }
  }

  std::string default_icon;
  // Read the page action |default_icon| (optional).
  if (extension_action->HasKey(keys::kPageActionDefaultIcon)) {
    if (!extension_action->GetString(keys::kPageActionDefaultIcon,
                                     &default_icon) ||
        default_icon.empty()) {
      *error = ASCIIToUTF16(errors::kInvalidPageActionIconPath);
      return NULL;
    }
    result->set_default_icon_path(default_icon);
  }

  // Read the page action title from |default_title| if present, |name| if not
  // (both optional).
  std::string title;
  if (extension_action->HasKey(keys::kPageActionDefaultTitle)) {
    if (!extension_action->GetString(keys::kPageActionDefaultTitle, &title)) {
      *error = ASCIIToUTF16(errors::kInvalidPageActionDefaultTitle);
      return NULL;
    }
  } else if (manifest_version_ == 1 && extension_action->HasKey(keys::kName)) {
    if (!extension_action->GetString(keys::kName, &title)) {
      *error = ASCIIToUTF16(errors::kInvalidPageActionName);
      return NULL;
    }
  }
  result->SetTitle(ExtensionAction::kDefaultTabId, title);

  // Read the action's |popup| (optional).
  const char* popup_key = NULL;
  if (extension_action->HasKey(keys::kPageActionDefaultPopup))
    popup_key = keys::kPageActionDefaultPopup;

  if (manifest_version_ == 1 &&
      extension_action->HasKey(keys::kPageActionPopup)) {
    if (popup_key) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
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
    } else if (manifest_version_ == 1 &&
               extension_action->GetDictionary(popup_key, &popup)) {
      if (!popup->GetString(keys::kPageActionPopupPath, &url_str)) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidPageActionPopupPath, "<missing>");
        return NULL;
      }
    } else {
      *error = ASCIIToUTF16(errors::kInvalidPageActionPopup);
      return NULL;
    }

    if (!url_str.empty()) {
      // An empty string is treated as having no popup.
      GURL url = GetResourceURL(url_str);
      if (!url.is_valid()) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidPageActionPopupPath, url_str);
        return NULL;
      }
      result->SetPopupUrl(ExtensionAction::kDefaultTabId, url);
    } else {
      DCHECK(!result->HasPopup(ExtensionAction::kDefaultTabId))
          << "Shouldn't be possible for the popup to be set.";
    }
  }

  return result.release();
}

Extension::FileBrowserHandlerList* Extension::LoadFileBrowserHandlers(
    const ListValue* extension_actions, string16* error) {
  scoped_ptr<FileBrowserHandlerList> result(
      new FileBrowserHandlerList());
  for (ListValue::const_iterator iter = extension_actions->begin();
       iter != extension_actions->end();
       ++iter) {
    if (!(*iter)->IsType(Value::TYPE_DICTIONARY)) {
      *error = ASCIIToUTF16(errors::kInvalidFileBrowserHandler);
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
    const DictionaryValue* file_browser_handler, string16* error) {
  scoped_ptr<FileBrowserHandler> result(
      new FileBrowserHandler());
  result->set_extension_id(id());

  std::string id;
  // Read the file action |id| (mandatory).
  if (!file_browser_handler->HasKey(keys::kPageActionId) ||
      !file_browser_handler->GetString(keys::kPageActionId, &id)) {
    *error = ASCIIToUTF16(errors::kInvalidPageActionId);
    return NULL;
  }
  result->set_id(id);

  // Read the page action title from |default_title| (mandatory).
  std::string title;
  if (!file_browser_handler->HasKey(keys::kPageActionDefaultTitle) ||
      !file_browser_handler->GetString(keys::kPageActionDefaultTitle, &title)) {
    *error = ASCIIToUTF16(errors::kInvalidPageActionDefaultTitle);
    return NULL;
  }
  result->set_title(title);

  // Initialize file filters (mandatory).
  ListValue* list_value = NULL;
  if (!file_browser_handler->HasKey(keys::kFileFilters) ||
      !file_browser_handler->GetList(keys::kFileFilters, &list_value) ||
      list_value->empty()) {
    *error = ASCIIToUTF16(errors::kInvalidFileFiltersList);
    return NULL;
  }
  for (size_t i = 0; i < list_value->GetSize(); ++i) {
    std::string filter;
    if (!list_value->GetString(i, &filter)) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidFileFilterValue, base::IntToString(i));
      return NULL;
    }
    StringToLowerASCII(&filter);
    URLPattern pattern(URLPattern::SCHEME_FILESYSTEM);
    if (pattern.Parse(filter) != URLPattern::PARSE_SUCCESS) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidURLPatternError, filter);
      return NULL;
    }
    std::string path = pattern.path();
    bool allowed = path == "*" || path == "*.*" ||
        (path.compare(0, 2, "*.") == 0 &&
         path.find_first_of('*', 2) == std::string::npos);
    if (!allowed) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
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
      *error = ASCIIToUTF16(errors::kInvalidPageActionIconPath);
      return NULL;
    }
    result->set_icon_path(default_icon);
  }

  return result.release();
}

bool Extension::LoadExtent(const extensions::Manifest* manifest,
                           const char* key,
                           URLPatternSet* extent,
                           const char* list_error,
                           const char* value_error,
                           string16* error) {
  Value* temp = NULL;
  if (!manifest->Get(key, &temp))
    return true;

  if (temp->GetType() != Value::TYPE_LIST) {
    *error = ASCIIToUTF16(list_error);
    return false;
  }

  ListValue* pattern_list = static_cast<ListValue*>(temp);
  for (size_t i = 0; i < pattern_list->GetSize(); ++i) {
    std::string pattern_string;
    if (!pattern_list->GetString(i, &pattern_string)) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(value_error,
                                                       base::UintToString(i),
                                                       errors::kExpectString);
      return false;
    }

    URLPattern pattern(kValidWebExtentSchemes);
    URLPattern::ParseResult parse_result = pattern.Parse(pattern_string);
    if (parse_result == URLPattern::PARSE_ERROR_EMPTY_PATH) {
      pattern_string += "/";
      parse_result = pattern.Parse(pattern_string);
    }

    if (parse_result != URLPattern::PARSE_SUCCESS) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          value_error,
          base::UintToString(i),
          URLPattern::GetParseResultString(parse_result));
      return false;
    }

    // Do not allow authors to claim "<all_urls>".
    if (pattern.match_all_urls()) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          value_error,
          base::UintToString(i),
          errors::kCannotClaimAllURLsInExtent);
      return false;
    }

    // Do not allow authors to claim "*" for host.
    if (pattern.host().empty()) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          value_error,
          base::UintToString(i),
          errors::kCannotClaimAllHostsInExtent);
      return false;
    }

    // We do not allow authors to put wildcards in their paths. Instead, we
    // imply one at the end.
    if (pattern.path().find('*') != std::string::npos) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
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

bool Extension::LoadLaunchURL(const extensions::Manifest* manifest,
                              string16* error) {
  Value* temp = NULL;

  // launch URL can be either local (to chrome-extension:// root) or an absolute
  // web URL.
  if (manifest->Get(keys::kLaunchLocalPath, &temp)) {
    if (manifest->Get(keys::kLaunchWebURL, NULL)) {
      *error = ASCIIToUTF16(errors::kLaunchPathAndURLAreExclusive);
      return false;
    }

    if (manifest->Get(keys::kWebURLs, NULL)) {
      *error = ASCIIToUTF16(errors::kLaunchPathAndExtentAreExclusive);
      return false;
    }

    std::string launch_path;
    if (!temp->GetAsString(&launch_path)) {
      *error = ASCIIToUTF16(errors::kInvalidLaunchLocalPath);
      return false;
    }

    // Ensure the launch path is a valid relative URL.
    GURL resolved = url().Resolve(launch_path);
    if (!resolved.is_valid() || resolved.GetOrigin() != url()) {
      *error = ASCIIToUTF16(errors::kInvalidLaunchLocalPath);
      return false;
    }

    launch_local_path_ = launch_path;
  } else if (manifest->Get(keys::kLaunchWebURL, &temp)) {
    std::string launch_url;
    if (!temp->GetAsString(&launch_url)) {
      *error = ASCIIToUTF16(errors::kInvalidLaunchWebURL);
      return false;
    }

    // Ensure the launch URL is a valid absolute URL and web extent scheme.
    GURL url(launch_url);
    URLPattern pattern(kValidWebExtentSchemes);
    if (!url.is_valid() || !pattern.SetScheme(url.scheme())) {
      *error = ASCIIToUTF16(errors::kInvalidLaunchWebURL);
      return false;
    }

    launch_web_url_ = launch_url;
  } else if (is_app()) {
    *error = ASCIIToUTF16(errors::kLaunchURLRequired);
    return false;
  }

  // If there is no extent, we default the extent based on the launch URL.
  if (web_extent().is_empty() && !launch_web_url().empty()) {
    GURL launch_url(launch_web_url());
    URLPattern pattern(kValidWebExtentSchemes);
    if (!pattern.SetScheme("*")) {
      *error = ASCIIToUTF16(errors::kInvalidLaunchWebURL);
      return false;
    }
    pattern.SetHost(launch_url.host());
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
      OverrideLaunchUrl(gallery_url);
    }
  } else if (id() == extension_misc::kCloudPrintAppId) {
    // In order for the --cloud-print-service switch to work, we must update
    // the launch URL and web extent.
    // TODO(sanjeevr): Ideally we want to use CloudPrintURL here but that is
    // currently under chrome/browser.
    const CommandLine& command_line = *CommandLine::ForCurrentProcess();
    GURL cloud_print_service_url = GURL(command_line.GetSwitchValueASCII(
        switches::kCloudPrintServiceURL));
    if (!cloud_print_service_url.is_empty()) {
      std::string path(
          cloud_print_service_url.path() + "/enable_chrome_connector");
      GURL::Replacements replacements;
      replacements.SetPathStr(path);
      GURL cloud_print_enable_connector_url =
          cloud_print_service_url.ReplaceComponents(replacements);
      OverrideLaunchUrl(cloud_print_enable_connector_url);
    }
  }
  return true;
}

bool Extension::LoadLaunchContainer(const extensions::Manifest* manifest,
                                    string16* error) {
  Value* temp = NULL;
  if (!manifest->Get(keys::kLaunchContainer, &temp))
    return true;

  std::string launch_container_string;
  if (!temp->GetAsString(&launch_container_string)) {
    *error = ASCIIToUTF16(errors::kInvalidLaunchContainer);
    return false;
  }

  if (launch_container_string == values::kLaunchContainerShell) {
    launch_container_ = extension_misc::LAUNCH_SHELL;
  } else if (launch_container_string == values::kLaunchContainerPanel) {
    launch_container_ = extension_misc::LAUNCH_PANEL;
  } else if (launch_container_string == values::kLaunchContainerTab) {
    launch_container_ = extension_misc::LAUNCH_TAB;
  } else {
    *error = ASCIIToUTF16(errors::kInvalidLaunchContainer);
    return false;
  }

  // Validate the container width if present.
  if (manifest->Get(keys::kLaunchWidth, &temp)) {
    if (launch_container() != extension_misc::LAUNCH_PANEL &&
        launch_container() != extension_misc::LAUNCH_WINDOW) {
      *error = ASCIIToUTF16(errors::kInvalidLaunchWidthContainer);
      return false;
    }
    if (!temp->GetAsInteger(&launch_width_) ||
        launch_width_ < 0) {
      launch_width_ = 0;
      *error = ASCIIToUTF16(errors::kInvalidLaunchWidth);
      return false;
    }
  }

  // Validate container height if present.
  if (manifest->Get(keys::kLaunchHeight, &temp)) {
    if (launch_container() != extension_misc::LAUNCH_PANEL &&
        launch_container() != extension_misc::LAUNCH_WINDOW) {
      *error = ASCIIToUTF16(errors::kInvalidLaunchHeightContainer);
      return false;
    }
    if (!temp->GetAsInteger(&launch_height_) || launch_height_ < 0) {
      launch_height_ = 0;
      *error = ASCIIToUTF16(errors::kInvalidLaunchHeight);
      return false;
    }
  }

  return true;
}

bool Extension::LoadAppIsolation(const extensions::Manifest* manifest,
                                 string16* error) {
  Value* temp = NULL;
  if (!manifest->Get(keys::kIsolation, &temp))
    return true;

  if (temp->GetType() != Value::TYPE_LIST) {
    *error = ASCIIToUTF16(errors::kInvalidIsolation);
    return false;
  }

  ListValue* isolation_list = static_cast<ListValue*>(temp);
  for (size_t i = 0; i < isolation_list->GetSize(); ++i) {
    std::string isolation_string;
    if (!isolation_list->GetString(i, &isolation_string)) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidIsolationValue,
          base::UintToString(i));
      return false;
    }

    // Check for isolated storage.
    if (isolation_string == values::kIsolatedStorage) {
      is_storage_isolated_ = true;
    } else {
      DLOG(WARNING) << "Did not recognize isolation type: "
                    << isolation_string;
    }
  }
  return true;
}

bool Extension::LoadWebIntentServices(const extensions::Manifest* manifest,
                                      string16* error) {
  DCHECK(error);

  if (!CommandLine::ForCurrentProcess()->HasSwitch(switches::kEnableWebIntents))
    return true;

  if (!manifest->HasKey(keys::kIntents))
    return true;

  DictionaryValue* all_services = NULL;
  if (!manifest->GetDictionary(keys::kIntents, &all_services)) {
    *error = ASCIIToUTF16(errors::kInvalidIntents);
    return false;
  }

  std::string value;
  for (DictionaryValue::key_iterator iter(all_services->begin_keys());
       iter != all_services->end_keys(); ++iter) {
    webkit_glue::WebIntentServiceData service;

    DictionaryValue* one_service = NULL;
    if (!all_services->GetDictionaryWithoutPathExpansion(*iter, &one_service)) {
      *error = ASCIIToUTF16(errors::kInvalidIntent);
      return false;
    }
    service.action = UTF8ToUTF16(*iter);

    ListValue* mime_types = NULL;
    if (!one_service->HasKey(keys::kIntentType) ||
        !one_service->GetList(keys::kIntentType, &mime_types) ||
        mime_types->GetSize() == 0) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidIntentType,*iter);
      return false;
    }

    if (one_service->HasKey(keys::kIntentPath)) {
      if (!one_service->GetString(keys::kIntentPath, &value)) {
        *error = ASCIIToUTF16(errors::kInvalidIntentPath);
        return false;
      }
      if (is_hosted_app()) {
        // Hosted apps require an absolute URL for intents.
        GURL service_url(value);
        if (!service_url.is_valid() ||
            !(web_extent().MatchesURL(service_url))) {
          *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
              errors::kInvalidIntentPageInHostedApp,*iter);
          return false;
        }
        service.service_url = service_url;
      } else {
        // We do not allow absolute intent URLs in non-hosted apps.
        if (GURL(value).is_valid()) {
          *error =ExtensionErrorUtils::FormatErrorMessageUTF16(
              errors::kCannotAccessPage,value.c_str());
          return false;
        }
        service.service_url = GetResourceURL(value);
      }
    }

    if (one_service->HasKey(keys::kIntentTitle) &&
        !one_service->GetString(keys::kIntentTitle, &service.title)) {
      *error = ASCIIToUTF16(errors::kInvalidIntentTitle);
      return false;
    }

    if (one_service->HasKey(keys::kIntentDisposition)) {
      if (!one_service->GetString(keys::kIntentDisposition, &value) ||
          (value != values::kIntentDispositionWindow &&
           value != values::kIntentDispositionInline)) {
        *error = ASCIIToUTF16(errors::kInvalidIntentDisposition);
        return false;
      }
      if (value == values::kIntentDispositionInline) {
        service.disposition =
            webkit_glue::WebIntentServiceData::DISPOSITION_INLINE;
      } else {
        service.disposition =
            webkit_glue::WebIntentServiceData::DISPOSITION_WINDOW;
      }
    }

    for (size_t i = 0; i < mime_types->GetSize(); ++i) {
      if (!mime_types->GetString(i, &service.type)) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidIntentTypeElement, *iter,
            std::string(base::IntToString(i)));
        return false;
      }
      intents_services_.push_back(service);
    }
  }
  return true;
}

bool Extension::LoadBackgroundScripts(const extensions::Manifest* manifest,
                                      string16* error) {
  Value* background_scripts_value = NULL;
  if (!manifest->Get(keys::kBackgroundScripts, &background_scripts_value))
    return true;

  CHECK(background_scripts_value);
  if (background_scripts_value->GetType() != Value::TYPE_LIST) {
    *error = ASCIIToUTF16(errors::kInvalidBackgroundScripts);
    return false;
  }

  ListValue* background_scripts =
      static_cast<ListValue*>(background_scripts_value);
  for (size_t i = 0; i < background_scripts->GetSize(); ++i) {
    std::string script;
    if (!background_scripts->GetString(i, &script)) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidBackgroundScript, base::IntToString(i));
      return false;
    }
    background_scripts_.push_back(script);
  }

  return true;
}

bool Extension::LoadBackgroundPage(
    const extensions::Manifest* manifest,
    const ExtensionAPIPermissionSet& api_permissions,
    string16* error) {
  base::Value* background_page_value = NULL;
  if (!manifest->Get(keys::kBackgroundPage, &background_page_value)) {
    if (manifest_version_ == 1)
      manifest->Get(keys::kBackgroundPageLegacy, &background_page_value);
  }

  if (!background_page_value)
    return true;

  std::string background_str;
  if (!background_page_value->GetAsString(&background_str)) {
    *error = ASCIIToUTF16(errors::kInvalidBackground);
    return false;
  }

  if (!background_scripts_.empty()) {
    *error = ASCIIToUTF16(errors::kInvalidBackgroundCombination);
    return false;
  }

  if (is_hosted_app()) {
    // Make sure "background" permission is set.
    if (!api_permissions.count(ExtensionAPIPermission::kBackground)) {
      *error = ASCIIToUTF16(errors::kBackgroundPermissionNeeded);
      return false;
    }
    // Hosted apps require an absolute URL.
    GURL bg_page(background_str);
    if (!bg_page.is_valid()) {
      *error = ASCIIToUTF16(errors::kInvalidBackgroundInHostedApp);
      return false;
    }

    if (!(bg_page.SchemeIs("https") ||
          (CommandLine::ForCurrentProcess()->HasSwitch(
              switches::kAllowHTTPBackgroundPage) &&
           bg_page.SchemeIs("http")))) {
      *error = ASCIIToUTF16(errors::kInvalidBackgroundInHostedApp);
      return false;
    }
    background_url_ = bg_page;
  } else {
    background_url_ = GetResourceURL(background_str);
  }

  return true;
}

bool Extension::LoadBackgroundPersistent(
    const extensions::Manifest* manifest,
    const ExtensionAPIPermissionSet& api_permissions,
    string16* error) {
  Value* background_persistent = NULL;
  if (!api_permissions.count(ExtensionAPIPermission::kExperimental) ||
      !manifest->Get(keys::kBackgroundPersistent, &background_persistent))
    return true;

  if (!background_persistent->IsType(Value::TYPE_BOOLEAN) ||
      !background_persistent->GetAsBoolean(&background_page_persists_)) {
    *error = ASCIIToUTF16(errors::kInvalidBackgroundPersistent);
    return false;
  }

  if (!has_background_page()) {
    *error = ASCIIToUTF16(errors::kInvalidBackgroundPersistentNoPage);
    return false;
  }

  return true;
}

// static
bool Extension::IsTrustedId(const std::string& id) {
  // See http://b/4946060 for more details.
  return id == std::string("nckgahadagoaajjgafhacjanaoiihapd");
}

Extension::Extension(const FilePath& path, Location location)
    : manifest_version_(0),
      incognito_split_mode_(false),
      offline_enabled_(false),
      location_(location),
      converted_from_user_script_(false),
      background_page_persists_(true),
      is_storage_isolated_(false),
      launch_container_(extension_misc::LAUNCH_TAB),
      launch_width_(0),
      launch_height_(0),
      wants_file_access_(false),
      creation_flags_(0) {
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
  DCHECK(output);
  if (input.length() == 0)
    return false;

  return base::Base64Encode(input, output);
}

bool Extension::FormatPEMForFileOutput(const std::string& input,
                                       std::string* output,
                                       bool is_public) {
  DCHECK(output);
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
    DLOG(ERROR) << "Could not read icon file: " << icon_path.LossyDisplayName();
    return;
  }

  // Decode the image using WebKit's image decoder.
  const unsigned char* data =
    reinterpret_cast<const unsigned char*>(file_contents.data());
  webkit_glue::ImageDecoder decoder;
  scoped_ptr<SkBitmap> decoded(new SkBitmap());
  *decoded = decoder.Decode(data, file_contents.length());
  if (decoded->empty()) {
    DLOG(ERROR) << "Could not decode icon file: "
                << icon_path.LossyDisplayName();
    return;
  }

  if (decoded->width() != icon_size || decoded->height() != icon_size) {
    DLOG(ERROR) << "Icon file has unexpected size: "
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

bool Extension::InitFromValue(extensions::Manifest* manifest, int flags,
                              string16* error) {
  DCHECK(error);
  base::AutoLock auto_lock(runtime_data_lock_);
  manifest_.reset(manifest);

  if (!manifest->ValidateManifest(error))
    return false;

  // Initialize permissions with an empty, default permission set.
  runtime_data_.SetActivePermissions(new ExtensionPermissionSet());
  optional_permission_set_ = new ExtensionPermissionSet();
  required_permission_set_ = new ExtensionPermissionSet();

  if (manifest->HasKey(keys::kManifestVersion)) {
    int manifest_version = 0;
    if (!manifest->GetInteger(keys::kManifestVersion, &manifest_version) ||
        manifest_version < 1) {
      *error = ASCIIToUTF16(errors::kInvalidManifestVersion);
      return false;
    }
    manifest_version_ = manifest_version;
  } else {
    // Version 1 was the original version, which lacked a version indicator.
    manifest_version_ = 1;
  }

  if (flags & REQUIRE_MODERN_MANIFEST_VERSION &&
      manifest_version() < kModernManifestVersion &&
      !CommandLine::ForCurrentProcess()->HasSwitch(
                switches::kAllowLegacyExtensionManifests)) {
    *error = ASCIIToUTF16(errors::kInvalidManifestVersion);
    return false;
  }

  if (manifest->HasKey(keys::kPublicKey)) {
    std::string public_key_bytes;
    if (!manifest->GetString(keys::kPublicKey,
                             &public_key_) ||
        !ParsePEMKeyBytes(public_key_,
                          &public_key_bytes) ||
        !GenerateId(public_key_bytes, &id_)) {
      *error = ASCIIToUTF16(errors::kInvalidKey);
      return false;
    }
  } else if (flags & REQUIRE_KEY) {
    *error = ASCIIToUTF16(errors::kInvalidKey);
    return false;
  } else if (id_.empty()) {
    // If there is a path, we generate the ID from it. This is useful for
    // development mode, because it keeps the ID stable across restarts and
    // reloading the extension.
    id_ = Extension::GenerateIdForPath(path());
    if (id_.empty()) {
      NOTREACHED() << "Could not create ID from path.";
      return false;
    }
  }

  creation_flags_ = flags;

  // Initialize the URL.
  extension_url_ = Extension::GetBaseURLFromExtensionId(id());

  // Initialize version.
  std::string version_str;
  if (!manifest->GetString(keys::kVersion, &version_str)) {
    *error = ASCIIToUTF16(errors::kInvalidVersion);
    return false;
  }
  version_.reset(Version::GetVersionFromString(version_str));
  if (!version_.get() ||
      version_->components().size() > 4) {
    *error = ASCIIToUTF16(errors::kInvalidVersion);
    return false;
  }

  // Initialize name.
  string16 localized_name;
  if (!manifest->GetString(keys::kName, &localized_name)) {
    *error = ASCIIToUTF16(errors::kInvalidName);
    return false;
  }
  base::i18n::AdjustStringForLocaleDirection(&localized_name);
  name_ = UTF16ToUTF8(localized_name);

  // Load App settings. LoadExtent at least has to be done before
  // ParsePermissions(), because the valid permissions depend on what type of
  // package this is.
  if (is_app() &&
      (!LoadExtent(manifest_.get(), keys::kWebURLs, &extent_,
                   errors::kInvalidWebURLs, errors::kInvalidWebURL, error) ||
       !LoadLaunchURL(manifest_.get(), error) ||
       !LoadLaunchContainer(manifest_.get(), error))) {
    return false;
  }

  if (is_platform_app()) {
    if (launch_container() != extension_misc::LAUNCH_SHELL) {
      *error = ASCIIToUTF16(errors::kInvalidLaunchContainerForPlatform);
      return false;
    }
  } else if (launch_container() == extension_misc::LAUNCH_SHELL) {
    *error = ASCIIToUTF16(errors::kInvalidLaunchContainerForNonPlatform);
    return false;
  }

  // Initialize the permissions (optional).
  ExtensionAPIPermissionSet api_permissions;
  URLPatternSet host_permissions;
  if (!ParsePermissions(manifest_.get(),
                        keys::kPermissions,
                        flags,
                        error,
                        &api_permissions,
                        &host_permissions)) {
    return false;
  }

  // Initialize the optional permissions (optional).
  ExtensionAPIPermissionSet optional_api_permissions;
  URLPatternSet optional_host_permissions;
  if (!ParsePermissions(manifest_.get(),
                        keys::kOptionalPermissions,
                        flags,
                        error,
                        &optional_api_permissions,
                        &optional_host_permissions)) {
    return false;
  }

  // Initialize description (if present).
  if (manifest->HasKey(keys::kDescription)) {
    if (!manifest->GetString(keys::kDescription,
                          &description_)) {
      *error = ASCIIToUTF16(errors::kInvalidDescription);
      return false;
    }
  }

  // Initialize homepage url (if present).
  if (manifest->HasKey(keys::kHomepageURL)) {
    std::string tmp;
    if (!manifest->GetString(keys::kHomepageURL, &tmp)) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidHomepageURL, "");
      return false;
    }
    homepage_url_ = GURL(tmp);
    if (!homepage_url_.is_valid() ||
        (!homepage_url_.SchemeIs("http") &&
            !homepage_url_.SchemeIs("https"))) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidHomepageURL, tmp);
      return false;
    }
  }

  // Initialize update url (if present).
  if (manifest->HasKey(keys::kUpdateURL)) {
    std::string tmp;
    if (!manifest->GetString(keys::kUpdateURL, &tmp)) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidUpdateURL, "");
      return false;
    }
    update_url_ = GURL(tmp);
    if (!update_url_.is_valid() ||
        update_url_.has_ref()) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidUpdateURL, tmp);
      return false;
    }
  }

  // Validate minimum Chrome version (if present). We don't need to store this,
  // since the extension is not valid if it is incorrect.
  if (manifest->HasKey(keys::kMinimumChromeVersion)) {
    std::string minimum_version_string;
    if (!manifest->GetString(keys::kMinimumChromeVersion,
                          &minimum_version_string)) {
      *error = ASCIIToUTF16(errors::kInvalidMinimumChromeVersion);
      return false;
    }

    scoped_ptr<Version> minimum_version(
        Version::GetVersionFromString(minimum_version_string));
    if (!minimum_version.get()) {
      *error = ASCIIToUTF16(errors::kInvalidMinimumChromeVersion);
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
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kChromeVersionTooLow,
          l10n_util::GetStringUTF8(IDS_PRODUCT_NAME),
          minimum_version_string);
      return false;
    }
  }

  // Initialize converted_from_user_script (if present)
  if (manifest->HasKey(keys::kConvertedFromUserScript))
    manifest->GetBoolean(keys::kConvertedFromUserScript,
                        &converted_from_user_script_);

  // Initialize icons (if present).
  if (manifest->HasKey(keys::kIcons)) {
    DictionaryValue* icons_value = NULL;
    if (!manifest->GetDictionary(keys::kIcons, &icons_value)) {
      *error = ASCIIToUTF16(errors::kInvalidIcons);
      return false;
    }

    for (size_t i = 0; i < arraysize(kIconSizes); ++i) {
      std::string key = base::IntToString(kIconSizes[i]);
      if (icons_value->HasKey(key)) {
        std::string icon_path;
        if (!icons_value->GetString(key, &icon_path)) {
          *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
              errors::kInvalidIconPath, key);
          return false;
        }

        if (!icon_path.empty() && icon_path[0] == '/')
          icon_path = icon_path.substr(1);

        if (icon_path.empty()) {
          *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
              errors::kInvalidIconPath, key);
          return false;
        }

        icons_.Add(kIconSizes[i], icon_path);
      }
    }
  }

  // Initialize themes (if present).
  if (manifest->HasKey(keys::kTheme)) {
    DictionaryValue* theme_value = NULL;
    if (!manifest->GetDictionary(keys::kTheme, &theme_value)) {
      *error = ASCIIToUTF16(errors::kInvalidTheme);
      return false;
    }

    DictionaryValue* images_value = NULL;
    if (theme_value->GetDictionary(keys::kThemeImages, &images_value)) {
      // Validate that the images are all strings
      for (DictionaryValue::key_iterator iter = images_value->begin_keys();
           iter != images_value->end_keys(); ++iter) {
        std::string val;
        if (!images_value->GetString(*iter, &val)) {
          *error = ASCIIToUTF16(errors::kInvalidThemeImages);
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
        int color = 0;
        // The color must be a list
        if (!colors_value->GetListWithoutPathExpansion(*iter, &color_list) ||
            // And either 3 items (RGB) or 4 (RGBA)
            ((color_list->GetSize() != 3) &&
             ((color_list->GetSize() != 4) ||
              // For RGBA, the fourth item must be a real or int alpha value.
              // Note that GetDouble() can get an integer value.
              !color_list->GetDouble(3, &alpha))) ||
            // For both RGB and RGBA, the first three items must be ints (R,G,B)
            !color_list->GetInteger(0, &color) ||
            !color_list->GetInteger(1, &color) ||
            !color_list->GetInteger(2, &color)) {
          *error = ASCIIToUTF16(errors::kInvalidThemeColors);
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
        if (!tints_value->GetListWithoutPathExpansion(*iter, &tint_list) ||
            tint_list->GetSize() != 3 ||
            !tint_list->GetDouble(0, &v) ||
            !tint_list->GetDouble(1, &v) ||
            !tint_list->GetDouble(2, &v)) {
          *error = ASCIIToUTF16(errors::kInvalidThemeTints);
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
  if (manifest->HasKey(keys::kPlugins)) {
    ListValue* list_value = NULL;
    if (!manifest->GetList(keys::kPlugins, &list_value)) {
      *error = ASCIIToUTF16(errors::kInvalidPlugins);
      return false;
    }

    for (size_t i = 0; i < list_value->GetSize(); ++i) {
      DictionaryValue* plugin_value = NULL;
      std::string path_str;
      bool is_public = false;

      if (!list_value->GetDictionary(i, &plugin_value)) {
        *error = ASCIIToUTF16(errors::kInvalidPlugins);
        return false;
      }

      // Get plugins[i].path.
      if (!plugin_value->GetString(keys::kPluginsPath, &path_str)) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidPluginsPath, base::IntToString(i));
        return false;
      }

      // Get plugins[i].content (optional).
      if (plugin_value->HasKey(keys::kPluginsPublic)) {
        if (!plugin_value->GetBoolean(keys::kPluginsPublic, &is_public)) {
          *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
              errors::kInvalidPluginsPublic, base::IntToString(i));
          return false;
        }
      }

      // We don't allow extension plugins to run on Chrome OS. We still
      // parse the manifest entry so that error messages are consistently
      // displayed across platforms.
#if !defined(OS_CHROMEOS)
      plugins_.push_back(PluginInfo());
      plugins_.back().path = path().Append(FilePath::FromUTF8Unsafe(path_str));
      plugins_.back().is_public = is_public;
#endif
    }
  }

  if (manifest->HasKey(keys::kNaClModules)) {
    ListValue* list_value = NULL;
    if (!manifest->GetList(keys::kNaClModules, &list_value)) {
      *error = ASCIIToUTF16(errors::kInvalidNaClModules);
      return false;
    }

    for (size_t i = 0; i < list_value->GetSize(); ++i) {
      DictionaryValue* module_value = NULL;
      std::string path_str;
      std::string mime_type;

      if (!list_value->GetDictionary(i, &module_value)) {
        *error = ASCIIToUTF16(errors::kInvalidNaClModules);
        return false;
      }

      // Get nacl_modules[i].path.
      if (!module_value->GetString(keys::kNaClModulesPath, &path_str)) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidNaClModulesPath, base::IntToString(i));
        return false;
      }

      // Get nacl_modules[i].mime_type.
      if (!module_value->GetString(keys::kNaClModulesMIMEType, &mime_type)) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidNaClModulesMIMEType, base::IntToString(i));
        return false;
      }

      nacl_modules_.push_back(NaClModuleInfo());
      nacl_modules_.back().url = GetResourceURL(path_str);
      nacl_modules_.back().mime_type = mime_type;
    }
  }

  // Initialize content scripts (optional).
  if (manifest->HasKey(keys::kContentScripts)) {
    ListValue* list_value;
    if (!manifest->GetList(keys::kContentScripts, &list_value)) {
      *error = ASCIIToUTF16(errors::kInvalidContentScriptsList);
      return false;
    }

    for (size_t i = 0; i < list_value->GetSize(); ++i) {
      DictionaryValue* content_script = NULL;
      if (!list_value->GetDictionary(i, &content_script)) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
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

  // Initialize web accessible resources (optional).
  if (manifest->HasKey(keys::kWebAccessibleResources)) {
    ListValue* list_value;
    if (!manifest->GetList(keys::kWebAccessibleResources, &list_value)) {
      *error = ASCIIToUTF16(errors::kInvalidWebAccessibleResourcesList);
      return false;
    }
    for (size_t i = 0; i < list_value->GetSize(); ++i) {
      std::string relative_path;
      if (!list_value->GetString(i, &relative_path)) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidWebAccessibleResource, base::IntToString(i));
        return false;
      }
      if (relative_path[0] != '/')
        relative_path = '/' + relative_path;
      web_accessible_resources_.insert(relative_path);
    }
  }

  // Initialize page action (optional).
  DictionaryValue* page_action_value = NULL;

  if (manifest_version_ == 1 && manifest->HasKey(keys::kPageActions)) {
    ListValue* list_value = NULL;
    if (!manifest->GetList(keys::kPageActions, &list_value)) {
      *error = ASCIIToUTF16(errors::kInvalidPageActionsList);
      return false;
    }

    size_t list_value_length = list_value->GetSize();

    if (list_value_length == 0u) {
      // A list with zero items is allowed, and is equivalent to not having
      // a page_actions key in the manifest.  Don't set |page_action_value|.
    } else if (list_value_length == 1u) {
      if (!list_value->GetDictionary(0, &page_action_value)) {
        *error = ASCIIToUTF16(errors::kInvalidPageAction);
        return false;
      }
    } else {  // list_value_length > 1u.
      *error = ASCIIToUTF16(errors::kInvalidPageActionsListSize);
      return false;
    }
  } else if (manifest->HasKey(keys::kPageAction)) {
    if (!manifest->GetDictionary(keys::kPageAction, &page_action_value)) {
      *error = ASCIIToUTF16(errors::kInvalidPageAction);
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
  if (manifest->HasKey(keys::kBrowserAction)) {
    DictionaryValue* browser_action_value = NULL;
    if (!manifest->GetDictionary(keys::kBrowserAction, &browser_action_value)) {
      *error = ASCIIToUTF16(errors::kInvalidBrowserAction);
      return false;
    }

    browser_action_.reset(
        LoadExtensionActionHelper(browser_action_value, error));
    if (!browser_action_.get())
      return false;  // Failed to parse browser action definition.
  }

  // Initialize file browser actions (optional).
  if (manifest->HasKey(keys::kFileBrowserHandlers)) {
    ListValue* file_browser_handlers_value = NULL;
    if (!manifest->GetList(keys::kFileBrowserHandlers,
                              &file_browser_handlers_value)) {
      *error = ASCIIToUTF16(errors::kInvalidFileBrowserHandler);
      return false;
    }

    file_browser_handlers_.reset(
        LoadFileBrowserHandlers(file_browser_handlers_value, error));
    if (!file_browser_handlers_.get())
      return false;  // Failed to parse file browser actions definition.
  }

  // App isolation.
  if (api_permissions.count(ExtensionAPIPermission::kExperimental)) {
    if (is_app() && !LoadAppIsolation(manifest_.get(), error))
      return false;
  }

  // Initialize options page url (optional).
  if (manifest->HasKey(keys::kOptionsPage)) {
    std::string options_str;
    if (!manifest->GetString(keys::kOptionsPage, &options_str)) {
      *error = ASCIIToUTF16(errors::kInvalidOptionsPage);
      return false;
    }

    if (is_hosted_app()) {
      // hosted apps require an absolute URL.
      GURL options_url(options_str);
      if (!options_url.is_valid() ||
          !(options_url.SchemeIs("http") || options_url.SchemeIs("https"))) {
        *error = ASCIIToUTF16(errors::kInvalidOptionsPageInHostedApp);
        return false;
      }
      options_url_ = options_url;
    } else {
      GURL absolute(options_str);
      if (absolute.is_valid()) {
        *error = ASCIIToUTF16(errors::kInvalidOptionsPageExpectUrlInPackage);
        return false;
      }
      options_url_ = GetResourceURL(options_str);
      if (!options_url_.is_valid()) {
        *error = ASCIIToUTF16(errors::kInvalidOptionsPage);
        return false;
      }
    }
  }

  if (!LoadBackgroundScripts(manifest, error))
    return false;

  if (!LoadBackgroundPage(manifest, api_permissions, error))
    return false;

  if (!LoadBackgroundPersistent(manifest, api_permissions, error))
    return false;

  if (manifest->HasKey(keys::kDefaultLocale)) {
    if (!manifest->GetString(keys::kDefaultLocale, &default_locale_) ||
        !l10n_util::IsValidLocaleSyntax(default_locale_)) {
      *error = ASCIIToUTF16(errors::kInvalidDefaultLocale);
      return false;
    }
  }

  // Chrome URL overrides (optional)
  if (manifest->HasKey(keys::kChromeURLOverrides)) {
    DictionaryValue* overrides = NULL;
    if (!manifest->GetDictionary(keys::kChromeURLOverrides, &overrides)) {
      *error = ASCIIToUTF16(errors::kInvalidChromeURLOverrides);
      return false;
    }

    // Validate that the overrides are all strings
    for (DictionaryValue::key_iterator iter = overrides->begin_keys();
         iter != overrides->end_keys(); ++iter) {
      std::string page = *iter;
      std::string val;
      // Restrict override pages to a list of supported URLs.
      if ((page != chrome::kChromeUINewTabHost &&
#if defined(USE_VIRTUAL_KEYBOARD)
           page != chrome::kChromeUIKeyboardHost &&
#endif
#if defined(OS_CHROMEOS)
           page != chrome::kChromeUIActivationMessageHost &&
#endif
           page != chrome::kChromeUIBookmarksHost &&
           page != chrome::kChromeUIHistoryHost
#if defined(FILE_MANAGER_EXTENSION)
               &&
           !(location() == COMPONENT &&
             page == chrome::kChromeUIFileManagerHost)
#endif
          ) ||
          !overrides->GetStringWithoutPathExpansion(*iter, &val)) {
        *error = ASCIIToUTF16(errors::kInvalidChromeURLOverrides);
        return false;
      }
      // Replace the entry with a fully qualified chrome-extension:// URL.
      chrome_url_overrides_[page] = GetResourceURL(val);
    }

    // An extension may override at most one page.
    if (overrides->size() > 1) {
      *error = ASCIIToUTF16(errors::kMultipleOverrides);
      return false;
    }
  }

  if (manifest->HasKey(keys::kInputComponents)) {
    ListValue* list_value = NULL;
    if (!manifest->GetList(keys::kInputComponents, &list_value)) {
      *error = ASCIIToUTF16(errors::kInvalidInputComponents);
      return false;
    }

    for (size_t i = 0; i < list_value->GetSize(); ++i) {
      DictionaryValue* module_value = NULL;
      std::string name_str;
      InputComponentType type;
      std::string id_str;
      std::string description_str;
      std::string language_str;
      std::set<std::string> layouts;
      std::string shortcut_keycode_str;
      bool shortcut_alt = false;
      bool shortcut_ctrl = false;
      bool shortcut_shift = false;

      if (!list_value->GetDictionary(i, &module_value)) {
        *error = ASCIIToUTF16(errors::kInvalidInputComponents);
        return false;
      }

      // Get input_components[i].name.
      if (!module_value->GetString(keys::kName, &name_str)) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidInputComponentName, base::IntToString(i));
        return false;
      }

      // Get input_components[i].type.
      std::string type_str;
      if (module_value->GetString(keys::kType, &type_str)) {
        if (type_str == "ime") {
          type = INPUT_COMPONENT_TYPE_IME;
        } else if (type_str == "virtual_keyboard") {
          if (api_permissions.count(ExtensionAPIPermission::kExperimental)) {
            // Virtual Keyboards require the experimental flag.
            *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
                errors::kInvalidInputComponentType, base::IntToString(i));
            return false;
          }
          type = INPUT_COMPONENT_TYPE_VIRTUAL_KEYBOARD;
        } else {
          *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
              errors::kInvalidInputComponentType, base::IntToString(i));
          return false;
        }
      } else {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidInputComponentType, base::IntToString(i));
        return false;
      }

      // Get input_components[i].id.
      if (!module_value->GetString(keys::kId, &id_str)) {
        id_str = "";
      }

      // Get input_components[i].description.
      if (!module_value->GetString(keys::kDescription, &description_str)) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidInputComponentDescription, base::IntToString(i));
        return false;
      }

      // Get input_components[i].language.
      if (!module_value->GetString(keys::kLanguage, &language_str)) {
        language_str = "";
      }

      // Get input_components[i].layouts.
      ListValue* layouts_value = NULL;
      if (!module_value->GetList(keys::kLayouts, &layouts_value)) {
        *error = ASCIIToUTF16(errors::kInvalidInputComponentLayouts);
        return false;
      }

      for (size_t j = 0; j < layouts_value->GetSize(); ++j) {
        std::string layout_name_str;
        if (!layouts_value->GetString(j, &layout_name_str)) {
          *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
              errors::kInvalidInputComponentLayoutName, base::IntToString(i),
              base::IntToString(j));
          return false;
        }
        layouts.insert(layout_name_str);
      }

      if (module_value->HasKey(keys::kShortcutKey)) {
        DictionaryValue* shortcut_value = NULL;
        if (!module_value->GetDictionary(keys::kShortcutKey, &shortcut_value)) {
          *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
              errors::kInvalidInputComponentShortcutKey, base::IntToString(i));
          return false;
        }

        // Get input_components[i].shortcut_keycode.
        if (!shortcut_value->GetString(keys::kKeycode, &shortcut_keycode_str)) {
          *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
              errors::kInvalidInputComponentShortcutKeycode,
              base::IntToString(i));
          return false;
        }

        // Get input_components[i].shortcut_alt.
        if (!shortcut_value->GetBoolean(keys::kAltKey, &shortcut_alt)) {
          shortcut_alt = false;
        }

        // Get input_components[i].shortcut_ctrl.
        if (!shortcut_value->GetBoolean(keys::kCtrlKey, &shortcut_ctrl)) {
          shortcut_ctrl = false;
        }

        // Get input_components[i].shortcut_shift.
        if (!shortcut_value->GetBoolean(keys::kShiftKey, &shortcut_shift)) {
          shortcut_shift = false;
        }
      }

      input_components_.push_back(InputComponentInfo());
      input_components_.back().name = name_str;
      input_components_.back().type = type;
      input_components_.back().id = id_str;
      input_components_.back().description = description_str;
      input_components_.back().language = language_str;
      input_components_.back().layouts.insert(layouts.begin(), layouts.end());
      input_components_.back().shortcut_keycode = shortcut_keycode_str;
      input_components_.back().shortcut_alt = shortcut_alt;
      input_components_.back().shortcut_ctrl = shortcut_ctrl;
      input_components_.back().shortcut_shift = shortcut_shift;
    }
  }

  if (manifest->HasKey(keys::kOmnibox)) {
    if (!manifest->GetString(keys::kOmniboxKeyword, &omnibox_keyword_) ||
        omnibox_keyword_.empty()) {
      *error = ASCIIToUTF16(errors::kInvalidOmniboxKeyword);
      return false;
    }
  }

  if (manifest->HasKey(keys::kContentSecurityPolicy)) {
    std::string content_security_policy;
    if (!manifest->GetString(keys::kContentSecurityPolicy,
                          &content_security_policy)) {
      *error = ASCIIToUTF16(errors::kInvalidContentSecurityPolicy);
      return false;
    }
    if (!ContentSecurityPolicyIsLegal(content_security_policy)) {
      *error = ASCIIToUTF16(errors::kInvalidContentSecurityPolicy);
      return false;
    }
    if (manifest_version_ >= 2 &&
        !ContentSecurityPolicyIsSecure(content_security_policy)) {
      *error = ASCIIToUTF16(errors::kInvalidContentSecurityPolicy);
      return false;
    }

    content_security_policy_ = content_security_policy;
  } else if (manifest_version_ >= 2) {
    // Manifest version 2 introduced a default Content-Security-Policy.
    // TODO(abarth): Should we continue to let extensions override the
    //               default Content-Security-Policy?
    content_security_policy_ = kDefaultContentSecurityPolicy;
    CHECK(ContentSecurityPolicyIsSecure(content_security_policy_));
  }

  // Initialize devtools page url (optional).
  if (manifest->HasKey(keys::kDevToolsPage)) {
    std::string devtools_str;
    if (!manifest->GetString(keys::kDevToolsPage, &devtools_str)) {
      *error = ASCIIToUTF16(errors::kInvalidDevToolsPage);
      return false;
    }
    devtools_url_ = GetResourceURL(devtools_str);
  }

  // Initialize text-to-speech voices (optional).
  if (manifest->HasKey(keys::kTtsEngine)) {
    DictionaryValue* tts_dict = NULL;
    if (!manifest->GetDictionary(keys::kTtsEngine, &tts_dict)) {
      *error = ASCIIToUTF16(errors::kInvalidTts);
      return false;
    }

    if (tts_dict->HasKey(keys::kTtsVoices)) {
      ListValue* tts_voices = NULL;
      if (!tts_dict->GetList(keys::kTtsVoices, &tts_voices)) {
        *error = ASCIIToUTF16(errors::kInvalidTtsVoices);
        return false;
      }

      for (size_t i = 0; i < tts_voices->GetSize(); i++) {
        DictionaryValue* one_tts_voice = NULL;
        if (!tts_voices->GetDictionary(i, &one_tts_voice)) {
          *error = ASCIIToUTF16(errors::kInvalidTtsVoices);
          return false;
        }

        TtsVoice voice_data;
        if (one_tts_voice->HasKey(keys::kTtsVoicesVoiceName)) {
          if (!one_tts_voice->GetString(
                  keys::kTtsVoicesVoiceName, &voice_data.voice_name)) {
            *error = ASCIIToUTF16(errors::kInvalidTtsVoicesVoiceName);
            return false;
          }
        }
        if (one_tts_voice->HasKey(keys::kTtsVoicesLang)) {
          if (!one_tts_voice->GetString(
                  keys::kTtsVoicesLang, &voice_data.lang) ||
              !l10n_util::IsValidLocaleSyntax(voice_data.lang)) {
            *error = ASCIIToUTF16(errors::kInvalidTtsVoicesLang);
            return false;
          }
        }
        if (one_tts_voice->HasKey(keys::kTtsVoicesGender)) {
          if (!one_tts_voice->GetString(
                  keys::kTtsVoicesGender, &voice_data.gender) ||
              (voice_data.gender != keys::kTtsGenderMale &&
               voice_data.gender != keys::kTtsGenderFemale)) {
            *error = ASCIIToUTF16(errors::kInvalidTtsVoicesGender);
            return false;
          }
        }
        if (one_tts_voice->HasKey(keys::kTtsVoicesEventTypes)) {
          ListValue* event_types_list;
          if (!one_tts_voice->GetList(
                  keys::kTtsVoicesEventTypes, &event_types_list)) {
            *error = ASCIIToUTF16(errors::kInvalidTtsVoicesEventTypes);
            return false;
          }
          for (size_t i = 0; i < event_types_list->GetSize(); i++) {
            std::string event_type;
            if (!event_types_list->GetString(i, &event_type)) {
              *error = ASCIIToUTF16(errors::kInvalidTtsVoicesEventTypes);
              return false;
            }
            if (event_type != keys::kTtsVoicesEventTypeEnd &&
                event_type != keys::kTtsVoicesEventTypeError &&
                event_type != keys::kTtsVoicesEventTypeMarker &&
                event_type != keys::kTtsVoicesEventTypeSentence &&
                event_type != keys::kTtsVoicesEventTypeStart &&
                event_type != keys::kTtsVoicesEventTypeWord) {
              *error = ASCIIToUTF16(errors::kInvalidTtsVoicesEventTypes);
              return false;
            }
            if (voice_data.event_types.find(event_type) !=
                voice_data.event_types.end()) {
              *error = ASCIIToUTF16(errors::kInvalidTtsVoicesEventTypes);
              return false;
            }
            voice_data.event_types.insert(event_type);
          }
        }

        tts_voices_.push_back(voice_data);
      }
    }
  }

  // Initialize web intents (optional).
  if (!LoadWebIntentServices(manifest, error))
    return false;

  // Initialize incognito behavior. Apps default to split mode, extensions
  // default to spanning.
  incognito_split_mode_ = is_app();
  if (manifest->HasKey(keys::kIncognito)) {
    std::string value;
    if (!manifest->GetString(keys::kIncognito, &value)) {
      *error = ASCIIToUTF16(errors::kInvalidIncognitoBehavior);
      return false;
    }
    if (value == values::kIncognitoSpanning) {
      incognito_split_mode_ = false;
    } else if (value == values::kIncognitoSplit) {
      incognito_split_mode_ = true;
    } else {
      *error = ASCIIToUTF16(errors::kInvalidIncognitoBehavior);
      return false;
    }
  }

  // Initialize offline-enabled status. Defaults to false.
  if (manifest->HasKey(keys::kOfflineEnabled)) {
    if (!manifest->GetBoolean(keys::kOfflineEnabled, &offline_enabled_)) {
      *error = ASCIIToUTF16(errors::kInvalidOfflineEnabled);
      return false;
    }
  }

  // Initialize requirements (optional). Not actually persisted (they're only
  // used by the store), but still validated.
  if (manifest->HasKey(keys::kRequirements)) {
    DictionaryValue* requirements_value = NULL;
    if (!manifest->GetDictionary(keys::kRequirements, &requirements_value)) {
      *error = ASCIIToUTF16(errors::kInvalidRequirements);
      return false;
    }

    for (DictionaryValue::key_iterator it = requirements_value->begin_keys();
         it != requirements_value->end_keys(); ++it) {
      DictionaryValue* requirement_value;
      if (!requirements_value->GetDictionaryWithoutPathExpansion(
          *it, &requirement_value)) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidRequirement, *it);
        return false;
      }
    }
  }

  if (HasMultipleUISurfaces()) {
    *error = ASCIIToUTF16(errors::kOneUISurfaceOnly);
    return false;
  }

  runtime_data_.SetActivePermissions(new ExtensionPermissionSet(
      this, api_permissions, host_permissions));
  required_permission_set_ = new ExtensionPermissionSet(
      this, api_permissions, host_permissions);
  optional_permission_set_ = new ExtensionPermissionSet(
      optional_api_permissions, optional_host_permissions, URLPatternSet());

  return true;
}

GURL Extension::GetHomepageURL() const {
  if (homepage_url_.is_valid())
    return homepage_url_;

  if (!UpdatesFromGallery())
    return GURL();

  GURL url(extension_urls::GetWebstoreItemDetailURLPrefix() + id());
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

bool Extension::ParsePermissions(const extensions::Manifest* source,
                                 const char* key,
                                 int flags,
                                 string16* error,
                                 ExtensionAPIPermissionSet* api_permissions,
                                 URLPatternSet* host_permissions) {
  if (source->HasKey(key)) {
    ListValue* permissions = NULL;
    if (!source->GetList(key, &permissions)) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidPermissions, "");
      return false;
    }

    for (size_t i = 0; i < permissions->GetSize(); ++i) {
      std::string permission_str;
      if (!permissions->GetString(i, &permission_str)) {
        *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidPermission, base::IntToString(i));
        return false;
      }

      ExtensionAPIPermission* permission =
          ExtensionPermissionsInfo::GetInstance()->GetByName(permission_str);

      if (permission != NULL) {
        if (CanSpecifyAPIPermission(permission, error))
          api_permissions->insert(permission->id());
        if (!error->empty())
          return false;
        continue;
      }

      // Check if it's a host pattern permission.
      const int kAllowedSchemes = CanExecuteScriptEverywhere() ?
          URLPattern::SCHEME_ALL : kValidHostPermissionSchemes;

      URLPattern pattern = URLPattern(kAllowedSchemes);
      URLPattern::ParseResult parse_result = pattern.Parse(permission_str);
      if (parse_result == URLPattern::PARSE_SUCCESS) {
        if (!CanSpecifyHostPermission(pattern, *api_permissions)) {
          *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
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
            pattern.SetValidSchemes(
                pattern.valid_schemes() & ~URLPattern::SCHEME_FILE);
        }

        host_permissions->AddPattern(pattern);
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
  return true;
}

bool Extension::CanSilentlyIncreasePermissions() const {
  return location() != INTERNAL;
}

bool Extension::CanSpecifyHostPermission(const URLPattern& pattern,
    const ExtensionAPIPermissionSet& permissions) const {
  if (!pattern.match_all_urls() &&
      pattern.MatchesScheme(chrome::kChromeUIScheme)) {
    // Regular extensions are only allowed access to chrome://favicon.
    if (pattern.host() == chrome::kChromeUIFaviconHost)
      return true;

    // Experimental extensions are also allowed chrome://thumb.
    if (pattern.host() == chrome::kChromeUIThumbnailHost) {
      return permissions.find(ExtensionAPIPermission::kExperimental) !=
          permissions.end();
    }

    // Component extensions can have access to all of chrome://*.
    if (CanExecuteScriptEverywhere())
      return true;

    return false;
  }

  // Otherwise, the valid schemes were handled by URLPattern.
  return true;
}

bool Extension::HasAPIPermission(
    ExtensionAPIPermission::ID permission) const {
  base::AutoLock auto_lock(runtime_data_lock_);
  return runtime_data_.GetActivePermissions()->HasAPIPermission(permission);
}

bool Extension::HasAPIPermission(
    const std::string& function_name) const {
  base::AutoLock auto_lock(runtime_data_lock_);
  return runtime_data_.GetActivePermissions()->
      HasAccessToFunction(function_name);
}

const URLPatternSet& Extension::GetEffectiveHostPermissions() const {
  base::AutoLock auto_lock(runtime_data_lock_);
  return runtime_data_.GetActivePermissions()->effective_hosts();
}

bool Extension::HasHostPermission(const GURL& url) const {
  if (url.SchemeIs(chrome::kChromeUIScheme) &&
      url.host() != chrome::kChromeUIFaviconHost &&
      url.host() != chrome::kChromeUIThumbnailHost &&
      location() != Extension::COMPONENT) {
    return false;
  }

  base::AutoLock auto_lock(runtime_data_lock_);
  return runtime_data_.GetActivePermissions()->
      HasExplicitAccessToOrigin(url);
}

bool Extension::HasEffectiveAccessToAllHosts() const {
  base::AutoLock auto_lock(runtime_data_lock_);
  return runtime_data_.GetActivePermissions()->HasEffectiveAccessToAllHosts();
}

bool Extension::HasFullPermissions() const {
  base::AutoLock auto_lock(runtime_data_lock_);
  return runtime_data_.GetActivePermissions()->HasEffectiveFullAccess();
}

ExtensionPermissionMessages Extension::GetPermissionMessages() const {
  base::AutoLock auto_lock(runtime_data_lock_);
  if (IsTrustedId(id_))
    return ExtensionPermissionMessages();
  else
    return runtime_data_.GetActivePermissions()->GetPermissionMessages();
}

std::vector<string16> Extension::GetPermissionMessageStrings() const {
  base::AutoLock auto_lock(runtime_data_lock_);
  if (IsTrustedId(id_))
    return std::vector<string16>();
  else
    return runtime_data_.GetActivePermissions()->GetWarningMessages();
}

void Extension::SetActivePermissions(
    const ExtensionPermissionSet* permissions) const {
  base::AutoLock auto_lock(runtime_data_lock_);
  runtime_data_.SetActivePermissions(permissions);
}

scoped_refptr<const ExtensionPermissionSet>
    Extension::GetActivePermissions() const {
  base::AutoLock auto_lock(runtime_data_lock_);
  return runtime_data_.GetActivePermissions();
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
  base::AutoLock auto_lock(runtime_data_lock_);
  // The gallery is special-cased as a restricted URL for scripting to prevent
  // access to special JS bindings we expose to the gallery (and avoid things
  // like extensions removing the "report abuse" link).
  // TODO(erikkay): This seems like the wrong test.  Shouldn't we we testing
  // against the store app extent?
  GURL store_url(extension_urls::GetWebstoreLaunchURL());
  if ((page_url.host() == store_url.host()) &&
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
    return script->MatchesURL(page_url);

  // Otherwise, see if this extension has permission to execute script
  // programmatically on pages.
  if (runtime_data_.GetActivePermissions()->HasExplicitAccessToOrigin(
          page_url))
    return true;

  if (error) {
    *error = ExtensionErrorUtils::FormatErrorMessage(errors::kCannotAccessPage,
                                                     page_url.spec());
  }

  return false;
}

bool Extension::ShowConfigureContextMenus() const {
  // Don't show context menu for component extensions. We might want to show
  // options for component extension button but now there is no component
  // extension with options. All other menu items like uninstall have
  // no sense for component extensions.
  return location() != Extension::COMPONENT;
}

bool Extension::ImplicitlyDelaysNetworkStartup() const {
  // Network requests should be deferred until any extensions that might want
  // to observe and modify them are loaded.
  return HasAPIPermission(ExtensionAPIPermission::kWebRequestBlocking);
}

bool Extension::CanSpecifyAPIPermission(
    const ExtensionAPIPermission* permission,
    string16* error) const {
  if (location_ == Extension::COMPONENT)
    return true;

  bool access_denied = false;
  if (permission->HasWhitelist()) {
    if (permission->IsWhitelisted(id_))
      return true;
    else
      access_denied = true;
  } else if (permission->is_component_only()) {
    access_denied = true;
  }

  if (access_denied) {
    *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
        errors::kPermissionNotAllowed, permission->name());
    return false;
  }

  if (permission->id() == ExtensionAPIPermission::kExperimental) {
    if (!CanSpecifyExperimentalPermission()) {
      *error = ASCIIToUTF16(errors::kExperimentalFlagRequired);
      return false;
    }
  }

  bool supports_type = false;
  switch (GetType()) {
    case TYPE_USER_SCRIPT: // Pass through.
    case TYPE_EXTENSION:
      supports_type = permission->supports_extensions();
      break;
    case TYPE_HOSTED_APP:
      supports_type = permission->supports_hosted_apps();
      break;
    case TYPE_PACKAGED_APP:
      supports_type = permission->supports_packaged_apps();
      break;
    case TYPE_PLATFORM_APP:
      supports_type = permission->supports_platform_apps();
      break;
    default:
      supports_type = false;
      break;
  }

  if (!supports_type) {
    // We special case hosted apps because some old versions of Chrome did not
    // return errors here and we ended up with extensions in the store
    // containing bad data: crbug.com/101993.
    //
    // TODO(aa): Consider just being a lot looser when loading and installing
    // extensions. We can be strict when packing and in development mode. Then
    // we won't have to maintain all these tricky backward compat issues:
    // crbug.com/102328.
    if (!is_hosted_app() || creation_flags_ & STRICT_ERROR_CHECKS) {
      *error = ExtensionErrorUtils::FormatErrorMessageUTF16(
          errors::kPermissionNotAllowed, permission->name());
    }
    return false;
  }

  return true;
}

bool Extension::CanSpecifyExperimentalPermission() const {
  if (location_ == Extension::COMPONENT)
    return true;

  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableExperimentalExtensionApis)) {
    return true;
  }

  // We rely on the webstore to check access to experimental. This way we can
  // whitelist extensions to have access to experimental in just the store, and
  // not have to push a new version of the client.
  if (from_webstore())
    return true;

  return false;
}

bool Extension::CanExecuteScriptEverywhere() const {
  if (location() == Extension::COMPONENT)
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
  return extension_urls::IsWebstoreUpdateUrl(update_url());
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
  origin_only_pattern.SetHost(origin.host());
  origin_only_pattern.SetPath("/*");

  URLPatternSet origin_only_pattern_list;
  origin_only_pattern_list.AddPattern(origin_only_pattern);

  return web_extent().OverlapsWith(origin_only_pattern_list);
}

Extension::SyncType Extension::GetSyncType() const {
  if (!IsSyncable()) {
    // We have a non-standard location.
    return SYNC_TYPE_NONE;
  }

  // Disallow extensions with non-gallery auto-update URLs for now.
  //
  // TODO(akalin): Relax this restriction once we've put in UI to
  // approve synced extensions.
  if (!update_url().is_empty() && !UpdatesFromGallery())
    return SYNC_TYPE_NONE;

  // Disallow extensions with native code plugins.
  //
  // TODO(akalin): Relax this restriction once we've put in UI to
  // approve synced extensions.
  if (!plugins().empty()) {
    return SYNC_TYPE_NONE;
  }

  switch (GetType()) {
    case Extension::TYPE_EXTENSION:
      return SYNC_TYPE_EXTENSION;

    case Extension::TYPE_USER_SCRIPT:
      // We only want to sync user scripts with gallery update URLs.
      if (UpdatesFromGallery())
        return SYNC_TYPE_EXTENSION;
      else
        return SYNC_TYPE_NONE;

    case Extension::TYPE_HOSTED_APP:
    case Extension::TYPE_PACKAGED_APP:
        return SYNC_TYPE_APP;

    default:
      return SYNC_TYPE_NONE;
  }
}

bool Extension::IsSyncable() const {
  // TODO(akalin): Figure out if we need to allow some other types.

  // We want to sync any extensions that are shown in the luancher because
  // their positions should sync.
  return location_ == Extension::INTERNAL ||
      ShouldDisplayInLauncher();

}

bool Extension::ShouldDisplayInLauncher() const {
  // All apps should be displayed on the NTP except for the Cloud Print App.
  return is_app() && id() != extension_misc::kCloudPrintAppId;
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

Extension::RuntimeData::RuntimeData() {}
Extension::RuntimeData::RuntimeData(const ExtensionPermissionSet* active)
    : active_permissions_(active) {}
Extension::RuntimeData::~RuntimeData() {}

scoped_refptr<const ExtensionPermissionSet>
    Extension::RuntimeData::GetActivePermissions() const {
  return active_permissions_;
}

void Extension::RuntimeData::SetActivePermissions(
    const ExtensionPermissionSet* active) {
  active_permissions_ = active;
}

UnloadedExtensionInfo::UnloadedExtensionInfo(
    const Extension* extension,
    extension_misc::UnloadedExtensionReason reason)
  : reason(reason),
    already_disabled(false),
    extension(extension) {}

UpdatedExtensionPermissionsInfo::UpdatedExtensionPermissionsInfo(
    const Extension* extension,
    const ExtensionPermissionSet* permissions,
    Reason reason)
    : reason(reason),
      extension(extension),
      permissions(permissions) {}
