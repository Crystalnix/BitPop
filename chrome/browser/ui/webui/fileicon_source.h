// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_FILEICON_SOURCE_H_
#define CHROME_BROWSER_UI_WEBUI_FILEICON_SOURCE_H_
#pragma once

#include <string>

#include "chrome/browser/icon_manager.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"

class GURL;
class RefCountedBytes;

namespace gfx {
class Image;
}

// FileIconSource is the gateway between network-level chrome:
// requests for favicons and the history backend that serves these.
class FileIconSource : public ChromeURLDataManager::DataSource {
 public:
  explicit FileIconSource();

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_incognito,
                                int request_id);

  virtual std::string GetMimeType(const std::string&) const;

  // Called when favicon data is available from the history backend.
  void OnFileIconDataAvailable(
      IconManager::Handle request_handle,
      gfx::Image* icon);

 private:
  virtual ~FileIconSource();

  CancelableRequestConsumerT<int, 0> cancelable_consumer_;

  // Raw PNG representation of the favicon to show when the favicon
  // database doesn't have a favicon for a webpage.
  scoped_refptr<RefCountedBytes> default_favicon_;

  DISALLOW_COPY_AND_ASSIGN(FileIconSource);
};
#endif  // CHROME_BROWSER_UI_WEBUI_FILEICON_SOURCE_H_
