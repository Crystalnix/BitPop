// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_WALLPAPER_PRIVATE_API_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_WALLPAPER_PRIVATE_API_H_

#include "ash/desktop_background/desktop_background_resources.h"
#include "chrome/browser/extensions/extension_function.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "ui/gfx/image/image_skia.h"

// Wallpaper manager strings.
class WallpaperStringsFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("wallpaperPrivate.getStrings");

 protected:
  virtual ~WallpaperStringsFunction() {}

  // SyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

class WallpaperSetWallpaperFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("wallpaperPrivate.setWallpaper");

  WallpaperSetWallpaperFunction();

 protected:
  virtual ~WallpaperSetWallpaperFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  class WallpaperDecoder;

  void OnWallpaperDecoded(const gfx::ImageSkia& wallpaper);
  void OnFail();

  // Saves the image data to a file.
  void SaveToFile();

  // Sets wallpaper to the decoded image.
  void SetDecodedWallpaper();

  // Layout of the downloaded wallpaper.
  ash::WallpaperLayout layout_;

  // The decoded wallpaper.
  gfx::ImageSkia wallpaper_;

  // Email address of logged in user.
  std::string email_;

  // File name extracts from URL.
  std::string file_name_;

  // String representation of downloaded wallpaper.
  std::string image_data_;

  // Holds an instance of WallpaperDecoder.
  static WallpaperDecoder* wallpaper_decoder_;
};

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_WALLPAPER_PRIVATE_API_H_
