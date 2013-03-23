// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_EXTENSIONS_EXTENSION_FILE_UTIL_H_
#define CHROME_COMMON_EXTENSIONS_EXTENSION_FILE_UTIL_H_

#include <string>
#include <map>

#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/message_bundle.h"

class FilePath;
class GURL;

namespace base {
class DictionaryValue;
}

namespace extensions {
class MessageBundle;
}

// Utilities for manipulating the on-disk storage of extensions.
namespace extension_file_util {

// Copies |unpacked_source_dir| into the right location under |extensions_dir|.
// The destination directory is returned on success, or empty path is returned
// on failure.
FilePath InstallExtension(const FilePath& unpacked_source_dir,
                          const std::string& id,
                          const std::string& version,
                          const FilePath& extensions_dir);

// Removes all versions of the extension with |id| from |extensions_dir|.
void UninstallExtension(const FilePath& extensions_dir,
                        const std::string& id);

// Loads and validates an extension from the specified directory. Returns NULL
// on failure, with a description of the error in |error|.
scoped_refptr<extensions::Extension> LoadExtension(
    const FilePath& extension_root,
    extensions::Extension::Location location,
    int flags,
    std::string* error);

// The same as LoadExtension except use the provided |extension_id|.
scoped_refptr<extensions::Extension> LoadExtension(
    const FilePath& extension_root,
    const std::string& extension_id,
    extensions::Extension::Location location,
    int flags,
    std::string* error);

// Loads an extension manifest from the specified directory. Returns NULL
// on failure, with a description of the error in |error|.
base::DictionaryValue* LoadManifest(const FilePath& extension_root,
                                    std::string* error);

// Returns true if the given file path exists and is not zero-length.
bool ValidateFilePath(const FilePath& path);

// Returns true if the given extension object is valid and consistent.
// May also append a series of warning messages to |warnings|, but they
// should not prevent the extension from running.
//
// Otherwise, returns false, and a description of the error is
// returned in |error|.
bool ValidateExtension(const extensions::Extension* extension,
                       std::string* error,
                       extensions::Extension::InstallWarningVector* warnings);

// Returns a list of files that contain private keys inside |extension_dir|.
std::vector<FilePath> FindPrivateKeyFiles(const FilePath& extension_dir);

// Cleans up the extension install directory. It can end up with garbage in it
// if extensions can't initially be removed when they are uninstalled (eg if a
// file is in use).
//
// |extensions_dir| is the install directory to look in. |extension_paths| is a
// map from extension id to full installation path.
//
// Obsolete version directories are removed, as are directories that aren't
// found in |extension_paths|.
void GarbageCollectExtensions(
    const FilePath& extensions_dir,
    const std::multimap<std::string, FilePath>& extension_paths);

// Loads extension message catalogs and returns message bundle.
// Returns NULL on error, or if extension is not localized.
extensions::MessageBundle* LoadMessageBundle(
    const FilePath& extension_path,
    const std::string& default_locale,
    std::string* error);

// Loads the extension message bundle substitution map. Contains at least
// extension_id item.
extensions::MessageBundle::SubstitutionMap* LoadMessageBundleSubstitutionMap(
    const FilePath& extension_path,
    const std::string& extension_id,
    const std::string& default_locale);

// We need to reserve the namespace of entries that start with "_" for future
// use by Chrome.
// If any files or directories are found using "_" prefix and are not on
// reserved list we return false, and set error message.
bool CheckForIllegalFilenames(const FilePath& extension_path,
                              std::string* error);

// Get a relative file path from a chrome-extension:// URL.
FilePath ExtensionURLToRelativeFilePath(const GURL& url);

// Get a full file path from a chrome-extension-resource:// URL, If the URL
// points a file outside of root, this function will return empty FilePath.
FilePath ExtensionResourceURLToFilePath(const GURL& url, const FilePath& root);

// Returns a path to a temporary directory for unpacking an extension that will
// be installed into |extensions_dir|. Creates the directory if necessary.
// The directory will be on the same file system as |extensions_dir| so
// that the extension directory can be efficiently renamed into place. Returns
// an empty file path on failure.
FilePath GetInstallTempDir(const FilePath& extensions_dir);

// Helper function to delete files. This is used to avoid ugly casts which
// would be necessary with PostMessage since file_util::Delete is overloaded.
// TODO(skerner): Make a version of Delete that is not overloaded in file_util.
void DeleteFile(const FilePath& path, bool recursive);

}  // namespace extension_file_util

#endif  // CHROME_COMMON_EXTENSIONS_EXTENSION_FILE_UTIL_H_
