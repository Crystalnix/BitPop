// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_GEOLOCATION_CHROME_GEOLOCATION_PERMISSION_CONTEXT_H_
#define CHROME_BROWSER_GEOLOCATION_CHROME_GEOLOCATION_PERMISSION_CONTEXT_H_

#include "base/memory/scoped_ptr.h"
#include "content/public/browser/geolocation_permission_context.h"

class GeolocationInfoBarQueueController;
class PrefService;
class Profile;

// Chrome specific implementation of GeolocationPermissionContext; manages
// Geolocation permissions flow, and delegates UI handling via
// GeolocationInfoBarQueueController.
class ChromeGeolocationPermissionContext
    : public content::GeolocationPermissionContext {
 public:
  explicit ChromeGeolocationPermissionContext(Profile* profile);

  // Notifies whether or not the corresponding bridge is allowed to use
  // geolocation via
  // GeolocationPermissionContext::SetGeolocationPermissionResponse().
  void NotifyPermissionSet(int render_process_id,
                           int render_view_id,
                           int bridge_id,
                           const GURL& requesting_frame,
                           base::Callback<void(bool)> callback,
                           bool allowed);

  static void RegisterUserPrefs(PrefService *user_prefs);

  // GeolocationPermissionContext implementation:
  virtual void RequestGeolocationPermission(
      int render_process_id,
      int render_view_id,
      int bridge_id,
      const GURL& requesting_frame,
      base::Callback<void(bool)> callback) OVERRIDE;
  virtual void CancelGeolocationPermissionRequest(
      int render_process_id,
      int render_view_id,
      int bridge_id,
      const GURL& requesting_frame) OVERRIDE;

 private:
  virtual ~ChromeGeolocationPermissionContext();

  // Removes any pending InfoBar request.
  void CancelPendingInfoBarRequest(int render_process_id,
                                   int render_view_id,
                                   int bridge_id);

  // This must only be accessed from the UI thread.
  Profile* const profile_;

  scoped_ptr<GeolocationInfoBarQueueController>
      geolocation_infobar_queue_controller_;

  DISALLOW_COPY_AND_ASSIGN(ChromeGeolocationPermissionContext);
};

#endif  // CHROME_BROWSER_GEOLOCATION_CHROME_GEOLOCATION_PERMISSION_CONTEXT_H_
