// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/extension_file_util.h"

#include <map>
#include <vector>

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/json/json_file_value_serializer.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/scoped_temp_dir.h"
#include "base/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "base/utf_string_conversions.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_action.h"
#include "chrome/common/extensions/extension_l10n_util.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/extensions/extension_resource.h"
#include "chrome/common/extensions/message_bundle.h"
#include "grit/generated_resources.h"
#include "net/base/escape.h"
#include "net/base/file_stream.h"
#include "ui/base/l10n/l10n_util.h"

using extensions::Extension;

namespace errors = extension_manifest_errors;

namespace extension_file_util {

// Validates locale info. Doesn't check if messages.json files are valid.
static bool ValidateLocaleInfo(const Extension& extension,
                               std::string* error);

// Returns false and sets the error if script file can't be loaded,
// or if it's not UTF-8 encoded.
static bool IsScriptValid(const FilePath& path, const FilePath& relative_path,
                          int message_id, std::string* error);

const char kInstallDirectoryName[] = "Extensions";

FilePath InstallExtension(const FilePath& unpacked_source_dir,
                          const std::string& id,
                          const std::string& version,
                          const FilePath& all_extensions_dir) {
  FilePath extension_dir = all_extensions_dir.AppendASCII(id);
  FilePath version_dir;

  // Create the extension directory if it doesn't exist already.
  if (!file_util::PathExists(extension_dir)) {
    if (!file_util::CreateDirectory(extension_dir))
      return FilePath();
  }

  FilePath profile_temp_dir = GetUserDataTempDir();
  // Move the extracted extension to a temp folder under the profile which will
  // then be moved to the final destination to ensure integrity of the installed
  // extension. The first move is actually a copy+delete to ensure proper
  // behavor in case we are moving a folder inside another folder on the same
  // level because Move will attempt rename in this case instead of proper move.
  // PLEASE NOTE: This issue has been observed in extension unit tests that try
  // to install user exnteions (not crx files but unpacked ones) from subfolder
  // of the temp folder. In that case a move will only rename the folder insted
  // of miving it into the destination folder as expected. That is the reason we
  // do copy+delete instead of a plain delete here! It can happen in the wild
  // with say autounpacked archive going to the temp folder and the user tries
  // to install it from there.
  ScopedTempDir extension_temp_dir;
  if (profile_temp_dir.empty() ||
      !extension_temp_dir.CreateUniqueTempDirUnderPath(profile_temp_dir)) {
    LOG(ERROR) << "Creating of temp dir under in the profile failed.";
    return FilePath();
  }
  if (!file_util::CopyDirectory(unpacked_source_dir,
                                extension_temp_dir.path(), true)) {
    LOG(ERROR) << "Moving extension from : " << unpacked_source_dir.value()
               << " to : " << extension_temp_dir.path().value() << " failed.";
    return FilePath();
  }
  file_util::Delete(unpacked_source_dir, true);
  FilePath crx_temp_source =
      extension_temp_dir.path().Append(unpacked_source_dir.BaseName());

  // Try to find a free directory. There can be legitimate conflicts in the case
  // of overinstallation of the same version.
  const int kMaxAttempts = 100;
  for (int i = 0; i < kMaxAttempts; ++i) {
    FilePath candidate = extension_dir.AppendASCII(
        base::StringPrintf("%s_%u", version.c_str(), i));
    if (!file_util::PathExists(candidate)) {
      version_dir = candidate;
      break;
    }
  }

  if (version_dir.empty()) {
    LOG(ERROR) << "Could not find a home for extension " << id << " with "
               << "version " << version << ".";
    return FilePath();
  }

  if (!file_util::Move(crx_temp_source, version_dir)) {
    LOG(ERROR) << "Installing extension from : " << crx_temp_source.value()
               << " into : " << version_dir.value() << " failed.";
    return FilePath();
  }

  return version_dir;
}

void UninstallExtension(const FilePath& extensions_dir,
                        const std::string& id) {
  // We don't care about the return value. If this fails (and it can, due to
  // plugins that aren't unloaded yet, it will get cleaned up by
  // ExtensionService::GarbageCollectExtensions).
  file_util::Delete(extensions_dir.AppendASCII(id), true);  // recursive.
}

scoped_refptr<Extension> LoadExtension(const FilePath& extension_path,
                                       Extension::Location location,
                                       int flags,
                                       std::string* error) {
  return LoadExtension(extension_path, std::string(), location, flags, error);
}

scoped_refptr<Extension> LoadExtension(const FilePath& extension_path,
                                       const std::string& extension_id,
                                       Extension::Location location,
                                       int flags,
                                       std::string* error) {
  scoped_ptr<DictionaryValue> manifest(LoadManifest(extension_path, error));
  if (!manifest.get())
    return NULL;
  if (!extension_l10n_util::LocalizeExtension(extension_path, manifest.get(),
                                              error)) {
    return NULL;
  }

  scoped_refptr<Extension> extension(Extension::Create(extension_path,
                                                       location,
                                                       *manifest,
                                                       flags,
                                                       extension_id,
                                                       error));
  if (!extension.get())
    return NULL;

  Extension::InstallWarningVector warnings;
  if (!ValidateExtension(extension.get(), error, &warnings))
    return NULL;
  extension->AddInstallWarnings(warnings);

  return extension;
}

DictionaryValue* LoadManifest(const FilePath& extension_path,
                              std::string* error) {
  FilePath manifest_path =
      extension_path.Append(Extension::kManifestFilename);
  if (!file_util::PathExists(manifest_path)) {
    *error = l10n_util::GetStringUTF8(IDS_EXTENSION_MANIFEST_UNREADABLE);
    return NULL;
  }

  JSONFileValueSerializer serializer(manifest_path);
  scoped_ptr<Value> root(serializer.Deserialize(NULL, error));
  if (!root.get()) {
    if (error->empty()) {
      // If |error| is empty, than the file could not be read.
      // It would be cleaner to have the JSON reader give a specific error
      // in this case, but other code tests for a file error with
      // error->empty().  For now, be consistent.
      *error = l10n_util::GetStringUTF8(IDS_EXTENSION_MANIFEST_UNREADABLE);
    } else {
      *error = base::StringPrintf("%s  %s",
                                  errors::kManifestParseError,
                                  error->c_str());
    }
    return NULL;
  }

  if (!root->IsType(Value::TYPE_DICTIONARY)) {
    *error = l10n_util::GetStringUTF8(IDS_EXTENSION_MANIFEST_INVALID);
    return NULL;
  }

  return static_cast<DictionaryValue*>(root.release());
}

std::vector<FilePath> FindPrivateKeyFiles(const FilePath& extension_dir) {
  std::vector<FilePath> result;
  // Pattern matching only works at the root level, so filter manually.
  file_util::FileEnumerator traversal(extension_dir, /*recursive=*/true,
                                      file_util::FileEnumerator::FILES);
  for (FilePath current = traversal.Next(); !current.empty();
       current = traversal.Next()) {
    if (!current.MatchesExtension(chrome::kExtensionKeyFileExtension))
      continue;

    std::string key_contents;
    if (!file_util::ReadFileToString(current, &key_contents)) {
      // If we can't read the file, assume it's not a private key.
      continue;
    }
    std::string key_bytes;
    if (!Extension::ParsePEMKeyBytes(key_contents, &key_bytes)) {
      // If we can't parse the key, assume it's ok too.
      continue;
    }

    result.push_back(current);
  }
  return result;
}

bool ValidateFilePath(const FilePath& path) {
  int64 size = 0;
  if (!file_util::PathExists(path) ||
      !file_util::GetFileSize(path, &size) ||
      size == 0) {
    return false;
  }

  return true;
}

bool ValidateExtension(const Extension* extension,
                       std::string* error,
                       Extension::InstallWarningVector* warnings) {
  // Validate icons exist.
  for (ExtensionIconSet::IconMap::const_iterator iter =
           extension->icons().map().begin();
       iter != extension->icons().map().end();
       ++iter) {
    const FilePath path = extension->GetResource(iter->second).GetFilePath();
    if (!ValidateFilePath(path)) {
      *error =
          l10n_util::GetStringFUTF8(IDS_EXTENSION_LOAD_ICON_FAILED,
                                    UTF8ToUTF16(iter->second));
      return false;
    }
  }

  // Theme resource validation.
  if (extension->is_theme()) {
    DictionaryValue* images_value = extension->GetThemeImages();
    if (images_value) {
      for (DictionaryValue::key_iterator iter = images_value->begin_keys();
           iter != images_value->end_keys(); ++iter) {
        std::string val;
        if (images_value->GetStringWithoutPathExpansion(*iter, &val)) {
          FilePath image_path = extension->path().Append(
              FilePath::FromUTF8Unsafe(val));
          if (!file_util::PathExists(image_path)) {
            *error =
                l10n_util::GetStringFUTF8(IDS_EXTENSION_INVALID_IMAGE_PATH,
                                          image_path.LossyDisplayName());
            return false;
          }
        }
      }
    }

    // Themes cannot contain other extension types.
    return true;
  }

  // Validate that claimed script resources actually exist,
  // and are UTF-8 encoded.
  ExtensionResource::SymlinkPolicy symlink_policy;
  if ((extension->creation_flags() &
       Extension::FOLLOW_SYMLINKS_ANYWHERE) != 0) {
    symlink_policy = ExtensionResource::FOLLOW_SYMLINKS_ANYWHERE;
  } else {
    symlink_policy = ExtensionResource::SYMLINKS_MUST_RESOLVE_WITHIN_ROOT;
  }

  for (size_t i = 0; i < extension->content_scripts().size(); ++i) {
    const extensions::UserScript& script = extension->content_scripts()[i];

    for (size_t j = 0; j < script.js_scripts().size(); j++) {
      const extensions::UserScript::File& js_script = script.js_scripts()[j];
      const FilePath& path = ExtensionResource::GetFilePath(
          js_script.extension_root(), js_script.relative_path(),
          symlink_policy);
      if (!IsScriptValid(path, js_script.relative_path(),
                         IDS_EXTENSION_LOAD_JAVASCRIPT_FAILED, error))
        return false;
    }

    for (size_t j = 0; j < script.css_scripts().size(); j++) {
      const extensions::UserScript::File& css_script = script.css_scripts()[j];
      const FilePath& path = ExtensionResource::GetFilePath(
          css_script.extension_root(), css_script.relative_path(),
          symlink_policy);
      if (!IsScriptValid(path, css_script.relative_path(),
                         IDS_EXTENSION_LOAD_CSS_FAILED, error))
        return false;
    }
  }

  // Validate claimed plugin paths.
  for (size_t i = 0; i < extension->plugins().size(); ++i) {
    const Extension::PluginInfo& plugin = extension->plugins()[i];
    if (!file_util::PathExists(plugin.path)) {
      *error =
          l10n_util::GetStringFUTF8(
              IDS_EXTENSION_LOAD_PLUGIN_PATH_FAILED,
              plugin.path.LossyDisplayName());
      return false;
    }
  }

  // Validate icon location and icon file size for page actions.
  ExtensionAction* page_action = extension->page_action();
  if (page_action) {
    std::vector<std::string> icon_paths(*page_action->icon_paths());
    if (!page_action->default_icon_path().empty())
      icon_paths.push_back(page_action->default_icon_path());
    for (std::vector<std::string>::iterator iter = icon_paths.begin();
         iter != icon_paths.end(); ++iter) {
      const FilePath path = extension->GetResource(*iter).GetFilePath();
      if (!ValidateFilePath(path)) {
        *error =
            l10n_util::GetStringFUTF8(
                IDS_EXTENSION_LOAD_ICON_FOR_PAGE_ACTION_FAILED,
                UTF8ToUTF16(*iter));
        return false;
      }
    }
  }

  // Validate icon location and icon file size for browser actions.
  // Note: browser actions don't use the icon_paths().
  ExtensionAction* browser_action = extension->browser_action();
  if (browser_action) {
    std::string path = browser_action->default_icon_path();
    if (!path.empty()) {
      const FilePath file_path = extension->GetResource(path).GetFilePath();
      if (!ValidateFilePath(file_path)) {
        *error =
            l10n_util::GetStringFUTF8(
                IDS_EXTENSION_LOAD_ICON_FOR_BROWSER_ACTION_FAILED,
                UTF8ToUTF16(path));
        return false;
      }
    }
  }

  // Validate that background scripts exist.
  for (size_t i = 0; i < extension->background_scripts().size(); ++i) {
    if (!file_util::PathExists(
            extension->GetResource(
                extension->background_scripts()[i]).GetFilePath())) {
      *error = l10n_util::GetStringFUTF8(
          IDS_EXTENSION_LOAD_BACKGROUND_SCRIPT_FAILED,
          UTF8ToUTF16(extension->background_scripts()[i]));
      return false;
    }
  }

  // Validate background page location, except for hosted apps, which should use
  // an external URL. Background page for hosted apps are verified when the
  // extension is created (in Extension::InitFromValue)
  if (extension->has_background_page() &&
      !extension->is_hosted_app() &&
      extension->background_scripts().empty()) {
    FilePath page_path = ExtensionURLToRelativeFilePath(
        extension->GetBackgroundURL());
    const FilePath path = extension->GetResource(page_path).GetFilePath();
    if (path.empty() || !file_util::PathExists(path)) {
      *error =
          l10n_util::GetStringFUTF8(
              IDS_EXTENSION_LOAD_BACKGROUND_PAGE_FAILED,
              page_path.LossyDisplayName());
      return false;
    }
  }

  // Validate path to the options page.  Don't check the URL for hosted apps,
  // because they are expected to refer to an external URL.
  if (!extension->options_url().is_empty() && !extension->is_hosted_app()) {
    const FilePath options_path = ExtensionURLToRelativeFilePath(
        extension->options_url());
    const FilePath path = extension->GetResource(options_path).GetFilePath();
    if (path.empty() || !file_util::PathExists(path)) {
      *error =
          l10n_util::GetStringFUTF8(
              IDS_EXTENSION_LOAD_OPTIONS_PAGE_FAILED,
              options_path.LossyDisplayName());
      return false;
    }
  }

  // Validate locale info.
  if (!ValidateLocaleInfo(*extension, error))
    return false;

  // Check children of extension root to see if any of them start with _ and is
  // not on the reserved list.
  if (!CheckForIllegalFilenames(extension->path(), error)) {
    return false;
  }

  // Check that extensions don't include private key files.
  std::vector<FilePath> private_keys = FindPrivateKeyFiles(extension->path());
  if (extension->creation_flags() & Extension::ERROR_ON_PRIVATE_KEY) {
    if (!private_keys.empty()) {
      // Only print one of the private keys because l10n_util doesn't have a way
      // to translate a list of strings.
      *error = l10n_util::GetStringFUTF8(
          IDS_EXTENSION_CONTAINS_PRIVATE_KEY,
          private_keys.front().LossyDisplayName());
      return false;
    }
  } else {
    for (size_t i = 0; i < private_keys.size(); ++i) {
      warnings->push_back(Extension::InstallWarning(
          Extension::InstallWarning::FORMAT_TEXT,
          l10n_util::GetStringFUTF8(
              IDS_EXTENSION_CONTAINS_PRIVATE_KEY,
              private_keys[i].LossyDisplayName())));
    }
    // Only warn; don't block loading the extension.
  }
  return true;
}

void GarbageCollectExtensions(
    const FilePath& install_directory,
    const std::map<std::string, FilePath>& extension_paths) {
  // Nothing to clean up if it doesn't exist.
  if (!file_util::DirectoryExists(install_directory))
    return;

  DVLOG(1) << "Garbage collecting extensions...";
  file_util::FileEnumerator enumerator(install_directory,
                                       false,  // Not recursive.
                                       file_util::FileEnumerator::DIRECTORIES);
  FilePath extension_path;
  for (extension_path = enumerator.Next(); !extension_path.value().empty();
       extension_path = enumerator.Next()) {
    std::string extension_id;

    FilePath basename = extension_path.BaseName();
    if (IsStringASCII(basename.value())) {
      extension_id = UTF16ToASCII(basename.LossyDisplayName());
      if (!Extension::IdIsValid(extension_id))
        extension_id.clear();
    }

    // Delete directories that aren't valid IDs.
    if (extension_id.empty()) {
      DLOG(WARNING) << "Invalid extension ID encountered in extensions "
                       "directory: " << basename.value();
      DVLOG(1) << "Deleting invalid extension directory "
               << extension_path.value() << ".";
      file_util::Delete(extension_path, true);  // Recursive.
      continue;
    }

    std::map<std::string, FilePath>::const_iterator iter =
        extension_paths.find(extension_id);

    // If there is no entry in the prefs file, just delete the directory and
    // move on. This can legitimately happen when an uninstall does not
    // complete, for example, when a plugin is in use at uninstall time.
    if (iter == extension_paths.end()) {
      DVLOG(1) << "Deleting unreferenced install for directory "
               << extension_path.LossyDisplayName() << ".";
      file_util::Delete(extension_path, true);  // Recursive.
      continue;
    }

    // Clean up old version directories.
    file_util::FileEnumerator versions_enumerator(
        extension_path,
        false,  // Not recursive.
        file_util::FileEnumerator::DIRECTORIES);
    for (FilePath version_dir = versions_enumerator.Next();
         !version_dir.value().empty();
         version_dir = versions_enumerator.Next()) {
      if (version_dir.BaseName() != iter->second.BaseName()) {
        DVLOG(1) << "Deleting old version for directory "
                 << version_dir.LossyDisplayName() << ".";
        file_util::Delete(version_dir, true);  // Recursive.
      }
    }
  }
}

extensions::MessageBundle* LoadMessageBundle(
    const FilePath& extension_path,
    const std::string& default_locale,
    std::string* error) {
  error->clear();
  // Load locale information if available.
  FilePath locale_path = extension_path.Append(
      Extension::kLocaleFolder);
  if (!file_util::PathExists(locale_path))
    return NULL;

  std::set<std::string> locales;
  if (!extension_l10n_util::GetValidLocales(locale_path, &locales, error))
    return NULL;

  if (default_locale.empty() ||
      locales.find(default_locale) == locales.end()) {
    *error = l10n_util::GetStringUTF8(
        IDS_EXTENSION_LOCALES_NO_DEFAULT_LOCALE_SPECIFIED);
    return NULL;
  }

  extensions::MessageBundle* message_bundle =
      extension_l10n_util::LoadMessageCatalogs(
          locale_path,
          default_locale,
          extension_l10n_util::CurrentLocaleOrDefault(),
          locales,
          error);

  return message_bundle;
}

SubstitutionMap* LoadMessageBundleSubstitutionMap(
    const FilePath& extension_path,
    const std::string& extension_id,
    const std::string& default_locale) {
  SubstitutionMap* returnValue = new SubstitutionMap();
  if (!default_locale.empty()) {
    // Touch disk only if extension is localized.
    std::string error;
    scoped_ptr<extensions::MessageBundle> bundle(
        LoadMessageBundle(extension_path, default_locale, &error));

    if (bundle.get())
      *returnValue = *bundle->dictionary();
  }

  // Add @@extension_id reserved message here, so it's available to
  // non-localized extensions too.
  returnValue->insert(
      std::make_pair(extensions::MessageBundle::kExtensionIdKey, extension_id));

  return returnValue;
}

static bool ValidateLocaleInfo(const Extension& extension,
                               std::string* error) {
  // default_locale and _locales have to be both present or both missing.
  const FilePath path = extension.path().Append(
      Extension::kLocaleFolder);
  bool path_exists = file_util::PathExists(path);
  std::string default_locale = extension.default_locale();

  // If both default locale and _locales folder are empty, skip verification.
  if (default_locale.empty() && !path_exists)
    return true;

  if (default_locale.empty() && path_exists) {
    *error = l10n_util::GetStringUTF8(
        IDS_EXTENSION_LOCALES_NO_DEFAULT_LOCALE_SPECIFIED);
    return false;
  } else if (!default_locale.empty() && !path_exists) {
    *error = errors::kLocalesTreeMissing;
    return false;
  }

  // Treat all folders under _locales as valid locales.
  file_util::FileEnumerator locales(path,
                                    false,
                                    file_util::FileEnumerator::DIRECTORIES);

  std::set<std::string> all_locales;
  extension_l10n_util::GetAllLocales(&all_locales);
  const FilePath default_locale_path = path.AppendASCII(default_locale);
  bool has_default_locale_message_file = false;

  FilePath locale_path;
  while (!(locale_path = locales.Next()).empty()) {
    if (extension_l10n_util::ShouldSkipValidation(path, locale_path,
                                                  all_locales))
      continue;

    FilePath messages_path =
        locale_path.Append(Extension::kMessagesFilename);

    if (!file_util::PathExists(messages_path)) {
      *error = base::StringPrintf(
          "%s %s", errors::kLocalesMessagesFileMissing,
          UTF16ToUTF8(messages_path.LossyDisplayName()).c_str());
      return false;
    }

    if (locale_path == default_locale_path)
      has_default_locale_message_file = true;
  }

  // Only message file for default locale has to exist.
  if (!has_default_locale_message_file) {
    *error = errors::kLocalesNoDefaultMessages;
    return false;
  }

  return true;
}

static bool IsScriptValid(const FilePath& path,
                          const FilePath& relative_path,
                          int message_id,
                          std::string* error) {
  std::string content;
  if (!file_util::PathExists(path) ||
      !file_util::ReadFileToString(path, &content)) {
    *error = l10n_util::GetStringFUTF8(
        message_id,
        relative_path.LossyDisplayName());
    return false;
  }

  if (!IsStringUTF8(content)) {
    *error = l10n_util::GetStringFUTF8(
        IDS_EXTENSION_BAD_FILE_ENCODING,
        relative_path.LossyDisplayName());
    return false;
  }

  return true;
}

bool CheckForIllegalFilenames(const FilePath& extension_path,
                              std::string* error) {
  // Reserved underscore names.
  static const FilePath::CharType* reserved_names[] = {
    Extension::kLocaleFolder,
    FILE_PATH_LITERAL("__MACOSX"),
  };
  CR_DEFINE_STATIC_LOCAL(
      std::set<FilePath::StringType>, reserved_underscore_names,
      (reserved_names, reserved_names + arraysize(reserved_names)));

  // Enumerate all files and directories in the extension root.
  // There is a problem when using pattern "_*" with FileEnumerator, so we have
  // to cheat with find_first_of and match all.
  file_util::FileEnumerator all_files(
    extension_path,
    false,
    static_cast<file_util::FileEnumerator::FileType>(
        file_util::FileEnumerator::DIRECTORIES |
          file_util::FileEnumerator::FILES));

  FilePath file;
  while (!(file = all_files.Next()).empty()) {
    FilePath::StringType filename = file.BaseName().value();
    // Skip all that don't start with "_".
    if (filename.find_first_of(FILE_PATH_LITERAL("_")) != 0) continue;
    if (reserved_underscore_names.find(filename) ==
        reserved_underscore_names.end()) {
      *error = base::StringPrintf(
          "Cannot load extension with file or directory name %s. "
          "Filenames starting with \"_\" are reserved for use by the system.",
          filename.c_str());
      return false;
    }
  }

  return true;
}

FilePath ExtensionURLToRelativeFilePath(const GURL& url) {
  std::string url_path = url.path();
  if (url_path.empty() || url_path[0] != '/')
    return FilePath();

  // Drop the leading slashes and convert %-encoded UTF8 to regular UTF8.
  std::string file_path = net::UnescapeURLComponent(url_path,
      net::UnescapeRule::SPACES | net::UnescapeRule::URL_SPECIAL_CHARS);
  size_t skip = file_path.find_first_not_of("/\\");
  if (skip != file_path.npos)
    file_path = file_path.substr(skip);

  FilePath path =
#if defined(OS_POSIX)
    FilePath(file_path);
#elif defined(OS_WIN)
    FilePath(UTF8ToWide(file_path));
#else
    FilePath();
    NOTIMPLEMENTED();
#endif

  // It's still possible for someone to construct an annoying URL whose path
  // would still wind up not being considered relative at this point.
  // For example: chrome-extension://id/c:////foo.html
  if (path.IsAbsolute())
    return FilePath();

  return path;
}

FilePath ExtensionResourceURLToFilePath(const GURL& url, const FilePath& root) {
  std::string host = net::UnescapeURLComponent(url.host(),
      net::UnescapeRule::SPACES | net::UnescapeRule::URL_SPECIAL_CHARS);
  if (host.empty())
    return FilePath();

  FilePath relative_path = ExtensionURLToRelativeFilePath(url);
  if (relative_path.empty())
    return FilePath();

  FilePath path = root.AppendASCII(host).Append(relative_path);
  if (!file_util::PathExists(path) ||
      !file_util::AbsolutePath(&path) ||
      !root.IsParent(path)) {
    return FilePath();
  }
  return path;
}

FilePath GetUserDataTempDir() {
  // We do file IO in this function, but only when the current profile's
  // Temp directory has never been used before, or in a rare error case.
  // Developers are not likely to see these situations often, so do an
  // explicit thread check.
  base::ThreadRestrictions::AssertIOAllowed();

  // The following enum used to be sent as a histogram to diagnose issues
  // accessing the temp path (crbug/70056).  The histogram is gone, but
  // the enum makes it clear exactly why the temp directory can not be
  // accessed, which may aid debugging in the future.
  enum DirectoryCreationResult {
    SUCCESS = 0,

    CANT_GET_PARENT_PATH,
    CANT_GET_UDT_PATH,
    NOT_A_DIRECTORY,
    CANT_CREATE_DIR,
    CANT_WRITE_TO_PATH,

    UNSET,
    NUM_DIRECTORY_CREATION_RESULTS
  };

  // All paths should set |result|.
  DirectoryCreationResult result = UNSET;

  FilePath temp_path;
  if (!PathService::Get(chrome::DIR_USER_DATA_TEMP, &temp_path)) {
    FilePath parent_path;
    if (!PathService::Get(chrome::DIR_USER_DATA, &parent_path))
      result = CANT_GET_PARENT_PATH;
    else
      result = CANT_GET_UDT_PATH;

  } else if (file_util::PathExists(temp_path)) {

    // Path exists.  Check that it is a directory we can write to.
    if (!file_util::DirectoryExists(temp_path)) {
      result = NOT_A_DIRECTORY;

    } else if (!file_util::PathIsWritable(temp_path)) {
      result = CANT_WRITE_TO_PATH;

    } else {
      // Temp is a writable directory.
      result = SUCCESS;
    }

  } else if (!file_util::CreateDirectory(temp_path)) {
    // Path doesn't exist, and we failed to create it.
    result = CANT_CREATE_DIR;

  } else {
    // Successfully created the Temp directory.
    result = SUCCESS;
  }

  if (result == SUCCESS)
    return temp_path;

  return FilePath();
}

void DeleteFile(const FilePath& path, bool recursive) {
  file_util::Delete(path, recursive);
}

}  // namespace extension_file_util
