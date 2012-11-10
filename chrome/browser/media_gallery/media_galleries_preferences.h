// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_GALLERY_MEDIA_GALLERIES_PREFERENCES_H_
#define CHROME_BROWSER_MEDIA_GALLERY_MEDIA_GALLERIES_PREFERENCES_H_

#include <list>
#include <string>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/string16.h"
#include "chrome/browser/profiles/profile_keyed_service.h"

class PrefService;
class Profile;

namespace base {
class DictionaryValue;
}

typedef uint64 MediaGalleryPrefId;

struct MediaGalleryPermission {
  MediaGalleryPrefId pref_id;
  bool has_permission;
};

struct MediaGallery {
  MediaGallery();
  ~MediaGallery();

  // The ID that uniquely, persistently identifies the gallery.
  uint64 id;

  // The directory where the gallery is located, relative to the base path
  // for the device.
  FilePath path;

  // The base path of the device.
  FilePath base_path;

  // A string which identifies the device that the gallery lives on.
  std::string identifier;

  // The user-visible name of this gallery.
  string16 display_name;
};

// A class to manage the media galleries that the user has added, explicitly or
// otherwise. There is one registry per user profile.
// TODO(estade): should MediaFileSystemRegistry be merged into this class?
class MediaGalleriesPreferences : public ProfileKeyedService {
 public:
  typedef std::list<MediaGallery> MediaGalleries;

  explicit MediaGalleriesPreferences(Profile* profile);
  virtual ~MediaGalleriesPreferences();

  // Builds |remembered_galleries_| from the persistent store.
  void InitFromPrefs();

  // Teaches the registry about a new gallery defined by |path| (which should
  // be a directory).
  void AddGalleryByPath(const FilePath& path);

  // Removes the gallery identified by |id| from the store.
  void ForgetGalleryById(uint64 id);

  const MediaGalleries& remembered_galleries() const {
    return remembered_galleries_;
  }

  // ProfileKeyedService implementation:
  virtual void Shutdown() OVERRIDE;

  static void RegisterUserPrefs(PrefService* prefs);

  // Returns true if the media gallery UI is turned on.
  static bool UserInteractionIsEnabled();

 private:
  // The profile that owns |this|.
  Profile* profile_;

  // An in-memory cache of known galleries.
  // TODO(estade): either actually use this, or remove it.
  MediaGalleries remembered_galleries_;

  DISALLOW_COPY_AND_ASSIGN(MediaGalleriesPreferences);
};

#endif  // CHROME_BROWSER_MEDIA_GALLERY_MEDIA_GALLERIES_PREFERENCES_H_
