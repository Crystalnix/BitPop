// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_EXTENSIONS_EXTENSION_RESOURCE_H_
#define CHROME_COMMON_EXTENSIONS_EXTENSION_RESOURCE_H_

#include <string>

#include "base/file_path.h"

// Represents a resource inside an extension. For example, an image, or a
// JavaScript file. This is more complicated than just a simple FilePath
// because extension resources can come from multiple physical file locations
// depending on locale.
class ExtensionResource {
 public:
  // SymlinkPolicy decides whether we'll allow resources to be a symlink to
  // anywhere, or whether they must end up within the extension root.
  enum SymlinkPolicy {
    SYMLINKS_MUST_RESOLVE_WITHIN_ROOT,
    FOLLOW_SYMLINKS_ANYWHERE,
  };

  ExtensionResource();

  ExtensionResource(const std::string& extension_id,
                    const FilePath& extension_root,
                    const FilePath& relative_path);

  ~ExtensionResource();

  // set_follow_symlinks_anywhere allows the resource to be a symlink to
  // anywhere in the filesystem. By default, resources have to be within
  // |extension_root| after resolving symlinks.
  void set_follow_symlinks_anywhere();

  // Returns actual path to the resource (default or locale specific). In the
  // browser process, this will DCHECK if not called on the file thread. To
  // easily load extension images on the UI thread, see ImageLoadingTracker.
  const FilePath& GetFilePath() const;

  // Gets the physical file path for the extension resource, taking into account
  // localization. In the browser process, this will DCHECK if not called on the
  // file thread. To easily load extension images on the UI thread, see
  // ImageLoadingTracker.
  //
  // The relative path must not resolve to a location outside of
  // |extension_root|. Iff |file_can_symlink_outside_root| is true, then the
  // file can be a symlink that links outside of |extension_root|.
  static FilePath GetFilePath(const FilePath& extension_root,
                              const FilePath& relative_path,
                              SymlinkPolicy symlink_policy);

  // Getters
  const std::string& extension_id() const { return extension_id_; }
  const FilePath& extension_root() const { return extension_root_; }
  const FilePath& relative_path() const { return relative_path_; }

  bool empty() { return extension_root().empty(); }

  // Unit test helpers.
  FilePath::StringType NormalizeSeperators(
      const FilePath::StringType& path) const;
  bool ComparePathWithDefault(const FilePath& path) const;

 private:
  // The id of the extension that this resource is associated with.
  std::string extension_id_;

  // Extension root.
  FilePath extension_root_;

  // Relative path to resource.
  FilePath relative_path_;

  // If |follow_symlinks_anywhere_| is true then the resource itself must be
  // within |extension_root|, but it can be a symlink to a file that is not.
  bool follow_symlinks_anywhere_;

  // Full path to extension resource. Starts empty.
  mutable FilePath full_resource_path_;
};

#endif  // CHROME_COMMON_EXTENSIONS_EXTENSION_RESOURCE_H_
