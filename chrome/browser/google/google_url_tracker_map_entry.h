// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_GOOGLE_GOOGLE_URL_TRACKER_MAP_ENTRY_H_
#define CHROME_BROWSER_GOOGLE_GOOGLE_URL_TRACKER_MAP_ENTRY_H_

#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_source.h"

class GoogleURLTracker;
class GoogleURLTrackerInfoBarDelegate;
class InfoBarTabHelper;

class GoogleURLTrackerMapEntry : public content::NotificationObserver {
 public:
  GoogleURLTrackerMapEntry(
      GoogleURLTracker* google_url_tracker,
      InfoBarTabHelper* infobar_helper,
      const content::NotificationSource& navigation_controller_source,
      const content::NotificationSource& web_contents_source);
  virtual ~GoogleURLTrackerMapEntry();

  bool has_infobar() const { return !!infobar_; }
  GoogleURLTrackerInfoBarDelegate* infobar() { return infobar_; }
  void SetInfoBar(GoogleURLTrackerInfoBarDelegate* infobar);

  const content::NotificationSource& navigation_controller_source() const {
    return navigation_controller_source_;
  }
  const content::NotificationSource& web_contents_source() const {
    return web_contents_source_;
  }

  void Close(bool redo_search);

 private:
  friend class GoogleURLTrackerTest;

  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  content::NotificationRegistrar registrar_;
  GoogleURLTracker* const google_url_tracker_;
  const InfoBarTabHelper* const infobar_helper_;
  GoogleURLTrackerInfoBarDelegate* infobar_;
  const content::NotificationSource navigation_controller_source_;
  const content::NotificationSource web_contents_source_;

  DISALLOW_COPY_AND_ASSIGN(GoogleURLTrackerMapEntry);
};

#endif  // CHROME_BROWSER_GOOGLE_GOOGLE_URL_TRACKER_MAP_ENTRY_H_
