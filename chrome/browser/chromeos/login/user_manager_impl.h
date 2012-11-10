// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_USER_MANAGER_IMPL_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_USER_MANAGER_IMPL_H_

#include <string>

#include "base/basictypes.h"
#include "base/hash_tables.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/synchronization/lock.h"
#include "base/time.h"
#include "base/timer.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/chromeos/login/user_image_loader.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/wallpaper_manager.h"
#include "chrome/browser/profiles/profile_downloader_delegate.h"
#include "chrome/browser/sync/profile_sync_service_observer.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class SkBitmap;
class FilePath;
class PrefService;
class ProfileDownloader;
class ProfileSyncService;

namespace chromeos {

class RemoveUserDelegate;
class UserImage;

// Implementation of the UserManager.
class UserManagerImpl : public UserManager,
                        public ProfileDownloaderDelegate,
                        public ProfileSyncServiceObserver,
                        public content::NotificationObserver {
 public:
  // UserManager implementation:
  virtual ~UserManagerImpl();

  virtual const UserList& GetUsers() const OVERRIDE;
  virtual void UserLoggedIn(const std::string& email,
                            bool browser_restart) OVERRIDE;
  virtual void DemoUserLoggedIn() OVERRIDE;
  virtual void GuestUserLoggedIn() OVERRIDE;
  virtual void EphemeralUserLoggedIn(const std::string& email) OVERRIDE;
  virtual void SessionStarted() OVERRIDE;
  virtual void RemoveUser(const std::string& email,
                          RemoveUserDelegate* delegate) OVERRIDE;
  virtual void RemoveUserFromList(const std::string& email) OVERRIDE;
  virtual bool IsKnownUser(const std::string& email) const OVERRIDE;
  virtual const User* FindUser(const std::string& email) const OVERRIDE;
  virtual const User& GetLoggedInUser() const OVERRIDE;
  virtual User& GetLoggedInUser() OVERRIDE;
  virtual void SaveUserOAuthStatus(
      const std::string& username,
      User::OAuthTokenStatus oauth_token_status) OVERRIDE;
  virtual void SaveUserDisplayName(const std::string& username,
                                   const string16& display_name) OVERRIDE;
  virtual string16 GetUserDisplayName(
      const std::string& username) const OVERRIDE;
  virtual void SaveUserDisplayEmail(const std::string& username,
                                    const std::string& display_email) OVERRIDE;
  virtual std::string GetUserDisplayEmail(
      const std::string& username) const OVERRIDE;
  virtual void SaveLoggedInUserWallpaperProperties(User::WallpaperType type,
                                                   int index) OVERRIDE;
  virtual void SaveUserDefaultImageIndex(const std::string& username,
                                         int image_index) OVERRIDE;
  virtual void SaveUserImage(const std::string& username,
                             const UserImage& user_image) OVERRIDE;
  virtual void SetLoggedInUserCustomWallpaperLayout(
      ash::WallpaperLayout layout) OVERRIDE;
  virtual void SaveUserImageFromFile(const std::string& username,
                                     const FilePath& path) OVERRIDE;
  virtual void SaveUserImageFromProfileImage(
      const std::string& username) OVERRIDE;
  virtual void DownloadProfileImage(const std::string& reason) OVERRIDE;
  virtual bool IsCurrentUserOwner() const OVERRIDE;
  virtual bool IsCurrentUserNew() const OVERRIDE;
  virtual bool IsCurrentUserEphemeral() const OVERRIDE;
  virtual bool IsUserLoggedIn() const OVERRIDE;
  virtual bool IsLoggedInAsDemoUser() const OVERRIDE;
  virtual bool IsLoggedInAsGuest() const OVERRIDE;
  virtual bool IsLoggedInAsStub() const OVERRIDE;
  virtual bool IsSessionStarted() const OVERRIDE;
  virtual void AddObserver(Observer* obs) OVERRIDE;
  virtual void RemoveObserver(Observer* obs) OVERRIDE;
  virtual void NotifyLocalStateChanged() OVERRIDE;
  virtual const SkBitmap& DownloadedProfileImage() const OVERRIDE;

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // ProfileSyncServiceObserver implementation.
  virtual void OnStateChanged() OVERRIDE;

 protected:
  UserManagerImpl();

  // Returns image filepath for the given user.
  FilePath GetImagePathForUser(const std::string& username);

 private:
  friend class UserManagerImplWrapper;
  friend class UserManagerTest;
  friend class WallpaperManager;

  // Loads |users_| from Local State if the list has not been loaded yet.
  // Subsequent calls have no effect. Must be called on the UI thread.
  void EnsureUsersLoaded();

  // Retrieves trusted device policies and removes users from the persistent
  // list if ephemeral users are enabled. Schedules a callback to itself if
  // trusted device policies are not yet available.
  void RetrieveTrustedDevicePolicies();

  // Returns true if trusted device policies have successfully been retrieved
  // and ephemeral users are enabled.
  bool AreEphemeralUsersEnabled() const;

  // Returns true if the user with the given email address is to be treated as
  // ephemeral.
  bool IsEphemeralUser(const std::string& email) const;

  // Returns the user with the given email address if found in the persistent
  // list. Returns |NULL| otherwise.
  const User* FindUserInList(const std::string& email) const;

  // Notifies on new user session.
  void NotifyOnLogin();

  // Reads user's oauth token status from local state preferences.
  User::OAuthTokenStatus LoadUserOAuthStatus(const std::string& username) const;

  void SetCurrentUserIsOwner(bool is_current_user_owner);

  // Sets one of the default images for the specified user and saves this
  // setting in local state.
  // Does not send LOGIN_USER_IMAGE_CHANGED notification.
  void SetInitialUserImage(const std::string& username);

  // Migrate the old wallpaper index to a new wallpaper structure.
  // The new wallpaper structure is:
  // { WallpaperType: DAILY|CUSTOMIZED|DEFAULT,
  //   index: index of the default wallpapers }
  void MigrateWallpaperData();

  // Sets image for user |username| and sends LOGIN_USER_IMAGE_CHANGED
  // notification unless this is a new user and image is set for the first time.
  // If |image| is empty, sets a stub image for the user.
  void SetUserImage(const std::string& username,
                    int image_index,
                    const GURL& image_url,
                    const UserImage& user_image);

  // Saves image to file, updates local state preferences to given image index
  // and sends LOGIN_USER_IMAGE_CHANGED notification.
  void SaveUserImageInternal(const std::string& username,
                             int image_index,
                             const GURL& image_url,
                             const UserImage& user_image);

  // Saves image to file with specified path and sends LOGIN_USER_IMAGE_CHANGED
  // notification. Runs on FILE thread. Posts task for saving image info to
  // Local State on UI thread.
  void SaveImageToFile(const std::string& username,
                       const UserImage& user_image,
                       const FilePath& image_path,
                       int image_index,
                       const GURL& image_url);

  // Stores path to the image and its index in local state. Runs on UI thread.
  // If |is_async| is true, it has been posted from the FILE thread after
  // saving the image.
  void SaveImageToLocalState(const std::string& username,
                             const std::string& image_path,
                             int image_index,
                             const GURL& image_url,
                             bool is_async);

  // Stores layout and type preference in local state. Runs on UI thread.
  void SaveWallpaperToLocalState(const std::string& username,
                                 const std::string& wallpaper_path,
                                 ash::WallpaperLayout layout,
                                 User::WallpaperType type);

  // Saves |image| to the specified |image_path|. Runs on FILE thread.
  bool SaveBitmapToFile(const UserImage& user_image,
                        const FilePath& image_path);

  // Initializes |downloaded_profile_image_| with the picture of the logged-in
  // user.
  void InitDownloadedProfileImage();

  // Download user's profile data, including full name and picture, when
  // |download_image| is true.
  // |reason| is an arbitrary string (used to report UMA histograms with
  // download times).
  void DownloadProfileData(const std::string& reason, bool download_image);

  // Scheduled call for downloading profile data.
  void DownloadProfileDataScheduled();

  // Deletes user's image file. Runs on FILE thread.
  void DeleteUserImage(const FilePath& image_path);

  // Updates current user ownership on UI thread.
  void UpdateOwnership(bool is_owner);

  // Checks current user's ownership on file thread.
  void CheckOwnership();

  // ProfileDownloaderDelegate implementation.
  virtual bool NeedsProfilePicture() const OVERRIDE;
  virtual int GetDesiredImageSideLength() const OVERRIDE;
  virtual Profile* GetBrowserProfile() OVERRIDE;
  virtual std::string GetCachedPictureURL() const OVERRIDE;
  virtual void OnProfileDownloadSuccess(ProfileDownloader* downloader) OVERRIDE;
  virtual void OnProfileDownloadFailure(ProfileDownloader* downloader) OVERRIDE;

  // Creates a new User instance.
  User* CreateUser(const std::string& email, bool is_ephemeral) const;

  // Removes the user from the persistent list only. Also removes the user's
  // picture.
  void RemoveUserFromListInternal(const std::string& email);

  // Loads user image from its file.
  scoped_refptr<UserImageLoader> image_loader_;

  // List of all known users. User instances are owned by |this| and deleted
  // when users are removed by |RemoveUserFromListInternal|.
  mutable UserList users_;

  // The logged-in user. NULL until a user has logged in, then points to one
  // of the User instances in |users_|, the |guest_user_| instance or an
  // ephemeral user instance.
  User* logged_in_user_;

  // True if SessionStarted() has been called.
  bool session_started_;

  // Cached flag of whether currently logged-in user is owner or not.
  // May be accessed on different threads, requires locking.
  bool is_current_user_owner_;
  mutable base::Lock is_current_user_owner_lock_;

  // Cached flag of whether the currently logged-in user existed before this
  // login.
  bool is_current_user_new_;

  // Cached flag of whether the currently logged-in user is ephemeral. Storage
  // of persistent information is avoided for such users by not adding them to
  // the user list in local state, not downloading their custom user images and
  // mounting their cryptohomes using tmpfs.
  bool is_current_user_ephemeral_;

  // Cached flag indicating whether ephemeral users are enabled. Defaults to
  // |false| if the value has not been read from trusted device policy yet.
  bool ephemeral_users_enabled_;

  // True if user pod row is showed at login screen.
  bool show_users_;

  // Cached name of device owner. Defaults to empty string if the value has not
  // been read from trusted device policy yet.
  std::string owner_email_;

  content::NotificationRegistrar registrar_;

  // Profile sync service which is observed to take actions after sync
  // errors appear. NOTE: there is no guarantee that it is the current sync
  // service, so do NOT use it outside |OnStateChanged| method.
  ProfileSyncService* observed_sync_service_;

  ObserverList<Observer> observer_list_;

  // Download user profile image on login to update it if it's changed.
  scoped_ptr<ProfileDownloader> profile_image_downloader_;

  // Arbitrary string passed to the last |DownloadProfileImage| call.
  std::string profile_image_download_reason_;

  // Time when the profile image download has started.
  base::Time profile_image_load_start_time_;

  // True if the last user image required async save operation (which may not
  // have been completed yet). This flag is used to avoid races when user image
  // is first set with |SaveUserImage| and then with |SaveUserImagePath|.
  bool last_image_set_async_;

  // Result of the last successful profile image download, if any.
  SkBitmap downloaded_profile_image_;

  // Data URL for |downloaded_profile_image_|.
  std::string downloaded_profile_image_data_url_;

  // Original URL of |downloaded_profile_image_|, from which it was downloaded.
  GURL profile_image_url_;

  // True when |profile_image_downloader_| is fetching profile picture (not
  // just full name).
  bool downloading_profile_image_;

  // Timer triggering DownloadProfileDataScheduled for refreshing profile data.
  base::RepeatingTimer<UserManagerImpl> profile_download_timer_;

  DISALLOW_COPY_AND_ASSIGN(UserManagerImpl);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_USER_MANAGER_IMPL_H_
