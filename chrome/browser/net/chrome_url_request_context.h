// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NET_CHROME_URL_REQUEST_CONTEXT_H_
#define CHROME_BROWSER_NET_CHROME_URL_REQUEST_CONTEXT_H_
#pragma once

#include <string>
#include <vector>

#include "base/file_path.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/extensions/extension_info_map.h"
#include "chrome/browser/extensions/extension_webrequest_api.h"
#include "chrome/browser/prefs/pref_change_registrar.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/common/extensions/extension_icon_set.h"
#include "content/browser/appcache/chrome_appcache_service.h"
#include "content/browser/chrome_blob_storage_context.h"
#include "content/common/notification_observer.h"
#include "content/common/notification_registrar.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "webkit/fileapi/file_system_context.h"

class ChromeURLDataManagerBackend;
class ChromeURLRequestContextFactory;
class IOThread;
class PrefService;
class Profile;
class ProfileIOData;
namespace base {
class WaitableEvent;
}
namespace net {
class NetworkDelegate;
}

// Subclass of net::URLRequestContext which can be used to store extra
// information for requests.
//
// All methods of this class must be called from the IO thread,
// including the constructor and destructor.
class ChromeURLRequestContext : public net::URLRequestContext {
 public:
  ChromeURLRequestContext();

  // Copies the state from |other| into this context.
  void CopyFrom(ChromeURLRequestContext* other);

  // Gets the path to the directory user scripts are stored in.
  FilePath user_script_dir_path() const {
    return user_script_dir_path_;
  }

  // Gets the appcache service to be used for requests in this context.
  // May be NULL if requests for this context aren't subject to appcaching.
  ChromeAppCacheService* appcache_service() const {
    return appcache_service_.get();
  }

  // Gets the blob storage context associated with this context's profile.
  ChromeBlobStorageContext* blob_storage_context() const {
    return blob_storage_context_.get();
  }

  // Gets the file system host context with this context's profile.
  fileapi::FileSystemContext* file_system_context() const {
    return file_system_context_.get();
  }

  bool is_incognito() const {
    return is_incognito_;
  }

  virtual const std::string& GetUserAgent(const GURL& url) const;

  const ExtensionInfoMap* extension_info_map() const {
    return extension_info_map_;
  }

  // TODO(willchan): Get rid of the need for this accessor. Really, this should
  // move completely to ProfileIOData.
  ChromeURLDataManagerBackend* chrome_url_data_manager_backend() const;

  // Setters to simplify initializing from factory objects.
  void set_user_script_dir_path(const FilePath& path) {
    user_script_dir_path_ = path;
  }
  void set_is_incognito(bool is_incognito) {
    is_incognito_ = is_incognito;
  }
  void set_appcache_service(ChromeAppCacheService* service) {
    appcache_service_ = service;
  }
  void set_blob_storage_context(ChromeBlobStorageContext* context) {
    blob_storage_context_ = context;
  }
  void set_file_system_context(fileapi::FileSystemContext* context) {
    file_system_context_ = context;
  }
  void set_extension_info_map(ExtensionInfoMap* map) {
    extension_info_map_ = map;
  }
  void set_chrome_url_data_manager_backend(
      ChromeURLDataManagerBackend* backend);

  // Callback for when the accept language changes.
  void OnAcceptLanguageChange(const std::string& accept_language);

  // Callback for when the default charset changes.
  void OnDefaultCharsetChange(const std::string& default_charset);

 protected:
  virtual ~ChromeURLRequestContext();

 private:
  // ---------------------------------------------------------------------------
  // Important: When adding any new members below, consider whether they need to
  // be added to CopyFrom.
  // ---------------------------------------------------------------------------

  // Path to the directory user scripts are stored in.
  FilePath user_script_dir_path_;

  // TODO(willchan): Make these non-refcounted.
  scoped_refptr<ChromeAppCacheService> appcache_service_;
  scoped_refptr<ChromeBlobStorageContext> blob_storage_context_;
  scoped_refptr<fileapi::FileSystemContext> file_system_context_;
  // TODO(aa): This should use chrome/common/extensions/extension_set.h.
  scoped_refptr<ExtensionInfoMap> extension_info_map_;

  ChromeURLDataManagerBackend* chrome_url_data_manager_backend_;
  bool is_incognito_;

  // ---------------------------------------------------------------------------
  // Important: When adding any new members above, consider whether they need to
  // be added to CopyFrom.
  // ---------------------------------------------------------------------------

  DISALLOW_COPY_AND_ASSIGN(ChromeURLRequestContext);
};

// A net::URLRequestContextGetter subclass used by the browser. This returns a
// subclass of net::URLRequestContext which can be used to store extra
// information about requests.
//
// Most methods are expected to be called on the UI thread, except for
// the destructor and GetURLRequestContext().
class ChromeURLRequestContextGetter : public net::URLRequestContextGetter,
                                      public NotificationObserver {
 public:
  // Constructs a ChromeURLRequestContextGetter that will use |factory| to
  // create the ChromeURLRequestContext. If |profile| is non-NULL, then the
  // ChromeURLRequestContextGetter will additionally watch the preferences for
  // changes to charset/language and CleanupOnUIThread() will need to be
  // called to unregister.
  ChromeURLRequestContextGetter(Profile* profile,
                                ChromeURLRequestContextFactory* factory);

  // Note that GetURLRequestContext() can only be called from the IO
  // thread (it will assert otherwise). DONTUSEME_GetCookieStore() and
  // GetIOMessageLoopProxy however can be called from any thread.
  //
  // net::URLRequestContextGetter implementation.
  virtual net::URLRequestContext* GetURLRequestContext();
  virtual net::CookieStore* DONTUSEME_GetCookieStore();
  virtual scoped_refptr<base::MessageLoopProxy> GetIOMessageLoopProxy() const;

  // Releases |url_request_context_|.  It's invalid to call
  // GetURLRequestContext() after this point.
  void ReleaseURLRequestContext();

  // Convenience overload of GetURLRequestContext() that returns a
  // ChromeURLRequestContext* rather than a net::URLRequestContext*.
  ChromeURLRequestContext* GetIOContext() {
    return reinterpret_cast<ChromeURLRequestContext*>(GetURLRequestContext());
  }

  // Create an instance for use with an 'original' (non-OTR) profile. This is
  // expected to get called on the UI thread.
  static ChromeURLRequestContextGetter* CreateOriginal(
      Profile* profile, const ProfileIOData* profile_io_data);

  // Create an instance for an original profile for media. This is expected to
  // get called on UI thread. This method takes a profile and reuses the
  // 'original' net::URLRequestContext for common files.
  static ChromeURLRequestContextGetter* CreateOriginalForMedia(
      Profile* profile, const ProfileIOData* profile_io_data);

  // Create an instance for an original profile for extensions. This is expected
  // to get called on UI thread.
  static ChromeURLRequestContextGetter* CreateOriginalForExtensions(
      Profile* profile, const ProfileIOData* profile_io_data);

  // Create an instance for an original profile for an app with isolated
  // storage. This is expected to get called on UI thread.
  static ChromeURLRequestContextGetter* CreateOriginalForIsolatedApp(
      Profile* profile,
      const ProfileIOData* profile_io_data,
      const std::string& app_id);

  // Create an instance for use with an OTR profile. This is expected to get
  // called on the UI thread.
  static ChromeURLRequestContextGetter* CreateOffTheRecord(
      Profile* profile, const ProfileIOData* profile_io_data);

  // Create an instance for an OTR profile for extensions. This is expected
  // to get called on UI thread.
  static ChromeURLRequestContextGetter* CreateOffTheRecordForExtensions(
      Profile* profile, const ProfileIOData* profile_io_data);

  // Create an instance for an OTR profile for an app with isolated storage.
  // This is expected to get called on UI thread.
  static ChromeURLRequestContextGetter* CreateOffTheRecordForIsolatedApp(
      Profile* profile,
      const ProfileIOData* profile_io_data,
      const std::string& app_id);

  // Clean up UI thread resources. This is expected to get called on the UI
  // thread before the instance is deleted on the IO thread.
  void CleanupOnUIThread();

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  // Must be called on the IO thread.
  virtual ~ChromeURLRequestContextGetter();

  // Registers an observer on |profile|'s preferences which will be used
  // to update the context when the default language and charset change.
  void RegisterPrefsObserver(Profile* profile);

  // These methods simply forward to the corresponding method on
  // ChromeURLRequestContext.
  void OnAcceptLanguageChange(const std::string& accept_language);
  void OnDefaultCharsetChange(const std::string& default_charset);
  void OnClearSiteDataOnExitChange(bool clear_site_data);

  // Saves the cookie store to |result| and signals |completion|.
  void GetCookieStoreAsyncHelper(base::WaitableEvent* completion,
                                 net::CookieStore** result);

  PrefChangeRegistrar registrar_;

  // |io_thread_| is always valid during the lifetime of |this| since |this| is
  // deleted on the IO thread.
  IOThread* const io_thread_;

  // Deferred logic for creating a ChromeURLRequestContext.
  // Access only from the IO thread.
  scoped_ptr<ChromeURLRequestContextFactory> factory_;

  // NULL if not yet initialized. Otherwise, it is the net::URLRequestContext
  // instance that was lazilly created by GetURLRequestContext.
  // Access only from the IO thread.
  scoped_refptr<net::URLRequestContext> url_request_context_;

  DISALLOW_COPY_AND_ASSIGN(ChromeURLRequestContextGetter);
};

#endif  // CHROME_BROWSER_NET_CHROME_URL_REQUEST_CONTEXT_H_
