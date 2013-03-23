// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_NTP_THUMBNAIL_SOURCE_H_
#define CHROME_BROWSER_UI_WEBUI_NTP_THUMBNAIL_SOURCE_H_

#include <string>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"

class Profile;

namespace base {
class RefCountedMemory;
}

namespace thumbnails {
class ThumbnailService;
}

// ThumbnailSource is the gateway between network-level chrome: requests for
// thumbnails and the history/top-sites backend that serves these.
class ThumbnailSource : public ChromeURLDataManager::DataSource {
 public:
  explicit ThumbnailSource(Profile* profile);

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_incognito,
                                int request_id) OVERRIDE;

  virtual std::string GetMimeType(const std::string& path) const OVERRIDE;

  virtual MessageLoop* MessageLoopForRequestPath(
      const std::string& path) const OVERRIDE;

 private:
  virtual ~ThumbnailSource();

  // Send the default thumbnail when we are missing a real one.
  void SendDefaultThumbnail(int request_id);

  // Raw PNG representation of the thumbnail to show when the thumbnail
  // database doesn't have a thumbnail for a webpage.
  scoped_refptr<base::RefCountedMemory> default_thumbnail_;

  // ThumbnailService.
  scoped_refptr<thumbnails::ThumbnailService> thumbnail_service_;

  DISALLOW_COPY_AND_ASSIGN(ThumbnailSource);
};

#endif  // CHROME_BROWSER_UI_WEBUI_NTP_THUMBNAIL_SOURCE_H_
