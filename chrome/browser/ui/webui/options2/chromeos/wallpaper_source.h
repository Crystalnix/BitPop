// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS2_CHROMEOS_WALLPAPER_SOURCE_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS2_CHROMEOS_WALLPAPER_SOURCE_H_

#include <string>

#include "base/compiler_specific.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace chromeos {
namespace options2 {

// A DataSource for chrome://wallpaper/ URL, provides current user's wallpaper.
class WallpaperImageSource : public ChromeURLDataManager::DataSource {
 public:
  WallpaperImageSource();

  // ChromeURLDataManager::DataSource implementation.
  virtual void StartDataRequest(const std::string& path,
                                bool is_incognito,
                                int request_id) OVERRIDE;
  virtual std::string GetMimeType(const std::string&) const OVERRIDE;

 private:
  class WallpaperEncodingOperation;

  virtual ~WallpaperImageSource();

  // Get wallpaper for request identified by |request_id| in UI thread
  // and pass it to encoding phase.
  void GetCurrentUserWallpaper(int request_id);

  // Callback is called when image is obtained from UI thread to
  // encode and send reply to request identified by |request_id|.
  void ImageAcquired(SkBitmap image, int request_id);

  // Cancel current image encoding operation.
  void CancelPendingEncodingOperation();

    // Send image stored in |data| as a reply to request
  // identifed by |request_id|.
  void SendCurrentUserWallpaper(int request_id,
                                scoped_refptr<base::RefCountedBytes> data);

  scoped_refptr<WallpaperEncodingOperation> wallpaper_encoding_op_;

  base::WeakPtrFactory<WallpaperImageSource> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(WallpaperImageSource);
};

}  // namespace options2
}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS2_CHROMEOS_WALLPAPER_SOURCE_H_
