// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Maps hostnames to custom zoom levels.  Written on the UI thread and read on
// any thread.  One instance per profile.

#ifndef CONTENT_BROWSER_HOST_ZOOM_MAP_H_
#define CONTENT_BROWSER_HOST_ZOOM_MAP_H_
#pragma once

#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "content/browser/browser_thread.h"
#include "content/common/notification_observer.h"
#include "content/common/notification_registrar.h"

class GURL;

// HostZoomMap needs to be deleted on the UI thread because it listens
// to notifications on there (and holds a NotificationRegistrar).
class HostZoomMap :
    public NotificationObserver,
    public base::RefCountedThreadSafe<HostZoomMap,
                                      BrowserThread::DeleteOnUIThread> {
 public:
  HostZoomMap();

  // Returns the zoom level for the host or spec for a given url. The zoom
  // level is determined by the host portion of the URL, or (in the absence of
  // a host) the complete spec of the URL. In most cases, there is no custom
  // zoom level, and this returns the user's default zoom level.  Otherwise,
  // returns the saved zoom level, which may be positive (to zoom in) or
  // negative (to zoom out).
  //
  // This may be called on any thread.
  double GetZoomLevel(std::string host) const;

  // Sets the zoom level for the host or spec for a given url to |level|.  If
  // the level matches the current default zoom level, the host is erased
  // from the saved preferences; otherwise the new value is written out.
  //
  // This should only be called on the UI thread.
  void SetZoomLevel(std::string host, double level);

  // Returns the temporary zoom level that's only valid for the lifetime of
  // the given tab (i.e. isn't saved and doesn't affect other tabs) if it
  // exists, the default zoom level otherwise.
  //
  // This may be called on any thread.
  double GetTemporaryZoomLevel(int render_process_id,
                               int render_view_id) const;

  // Sets the temporary zoom level that's only valid for the lifetime of this
  // tab.
  //
  // This should only be called on the UI thread.
  void SetTemporaryZoomLevel(int render_process_id,
                             int render_view_id,
                             double level);

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  double default_zoom_level() const { return default_zoom_level_; }
  void set_default_zoom_level(double level) { default_zoom_level_ = level; }

 private:
  friend struct BrowserThread::DeleteOnThread<BrowserThread::UI>;
  friend class DeleteTask<HostZoomMap>;

  typedef std::map<std::string, double> HostZoomLevels;

  virtual ~HostZoomMap();

  // Copy of the pref data, so that we can read it on the IO thread.
  HostZoomLevels host_zoom_levels_;
  double default_zoom_level_;

  struct TemporaryZoomLevel {
    int render_process_id;
    int render_view_id;
    double zoom_level;
  };

  // Don't expect more than a couple of tabs that are using a temporary zoom
  // level, so vector is fine for now.
  std::vector<TemporaryZoomLevel> temporary_zoom_levels_;

  // Used around accesses to |host_zoom_levels_|, |default_zoom_level_| and
  // |temporary_zoom_levels_| to guarantee thread safety.
  mutable base::Lock lock_;

  NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(HostZoomMap);
};

#endif  // CONTENT_BROWSER_HOST_ZOOM_MAP_H_
