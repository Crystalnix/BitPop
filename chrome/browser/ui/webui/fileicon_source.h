// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_FILEICON_SOURCE_H_
#define CHROME_BROWSER_UI_WEBUI_FILEICON_SOURCE_H_

#include <string>

#include "base/file_path.h"
#include "chrome/browser/icon_manager.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/common/cancelable_task_tracker.h"
#include "ui/base/layout.h"

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
                                int request_id) OVERRIDE;

  virtual std::string GetMimeType(const std::string&) const OVERRIDE;

 protected:
  virtual ~FileIconSource();

  // Once the |path| and |icon_size| has been determined from the request, this
  // function is called to perform the actual fetch. Declared as virtual for
  // testing.
  virtual void FetchFileIcon(const FilePath& path,
                             ui::ScaleFactor scale_factor,
                             IconLoader::IconSize icon_size,
                             int request_id);

 private:
  // Contains the necessary information for completing an icon fetch request.
  struct IconRequestDetails {
    // The request id corresponding to these details.
    int request_id;

    // The requested scale factor to respond with.
    ui::ScaleFactor scale_factor;
  };

  // Called when favicon data is available from the history backend.
  void OnFileIconDataAvailable(const IconRequestDetails& details,
                               gfx::Image* icon);

  // Tracks tasks requesting file icons.
  CancelableTaskTracker cancelable_task_tracker_;

  DISALLOW_COPY_AND_ASSIGN(FileIconSource);
};
#endif  // CHROME_BROWSER_UI_WEBUI_FILEICON_SOURCE_H_
