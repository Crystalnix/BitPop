// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/resource/resource_bundle.h"

#import <AppKit/AppKit.h>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/mac/mac_util.h"
#include "base/memory/scoped_nsobject.h"
#include "base/synchronization/lock.h"
#include "base/sys_info.h"
#include "base/sys_string_conversions.h"
#include "ui/gfx/image.h"

namespace ui {

namespace {

FilePath GetResourcesPakFilePath(NSString* name, NSString* mac_locale) {
  NSString *resource_path;
  // Some of the helper processes need to be able to fetch resources
  // (chrome_main.cc: SubprocessNeedsResourceBundle()). Fetch the same locale
  // as the already-running browser instead of using what NSBundle might pick
  // based on values at helper launch time.
  if ([mac_locale length]) {
    resource_path = [base::mac::MainAppBundle() pathForResource:name
                                                        ofType:@"pak"
                                                   inDirectory:@""
                                               forLocalization:mac_locale];
  } else {
    resource_path = [base::mac::MainAppBundle() pathForResource:name
                                                        ofType:@"pak"];
  }
  if (!resource_path)
    return FilePath();
  return FilePath([resource_path fileSystemRepresentation]);
}

}  // namespace

// static
FilePath ResourceBundle::GetResourcesFilePath() {
  return GetResourcesPakFilePath(@"chrome", nil);
}

// static
FilePath ResourceBundle::GetLargeIconResourcesFilePath() {
  int32 major = 0;
  int32 minor = 0;
  int32 bugfix = 0;
  base::SysInfo::OperatingSystemVersionNumbers(&major, &minor, &bugfix);

  // Only load the large resource pak on if we're running on 10.7 or above.
  if (major > 10 || (major == 10 && minor >= 7))
    return GetResourcesPakFilePath(@"theme_resources_large", nil);
  else
    return FilePath();
}

// static
FilePath ResourceBundle::GetLocaleFilePath(const std::string& app_locale) {
  NSString* mac_locale = base::SysUTF8ToNSString(app_locale);

  // Mac OS X uses "_" instead of "-", so swap to get a Mac-style value.
  mac_locale = [mac_locale stringByReplacingOccurrencesOfString:@"-"
                                                     withString:@"_"];

  // On disk, the "en_US" resources are just "en" (http://crbug.com/25578).
  if ([mac_locale isEqual:@"en_US"])
    mac_locale = @"en";

  return GetResourcesPakFilePath(@"locale", mac_locale);
}

gfx::Image& ResourceBundle::GetNativeImageNamed(int resource_id) {
  // Check to see if the image is already in the cache.
  {
    base::AutoLock lock(*lock_);
    ImageMap::const_iterator found = images_.find(resource_id);
    if (found != images_.end()) {
      if (!found->second->HasRepresentation(gfx::Image::kImageRepCocoa)) {
        DLOG(WARNING) << "ResourceBundle::GetNativeImageNamed() is returning a"
          << " cached gfx::Image that isn't backed by an NSImage. The image"
          << " will be converted, rather than going through the NSImage loader."
          << " resource_id = " << resource_id;
      }
      return *found->second;
    }
  }

  // Load the raw data from the resource pack.
  scoped_refptr<RefCountedStaticMemory> data(
      LoadDataResourceBytes(resource_id));

  // Create a data object from the raw bytes.
  scoped_nsobject<NSData> ns_data([[NSData alloc] initWithBytes:data->front()
                                                         length:data->size()]);

  // Create the image from the data. The gfx::Image will take ownership of this.
  scoped_nsobject<NSImage> ns_image([[NSImage alloc] initWithData:ns_data]);

  // Cache the converted image.
  if (ns_image.get()) {
    // Load a high resolution version of the icon if available.
    if (large_icon_resources_data_) {
      scoped_refptr<RefCountedStaticMemory> large_data(
          LoadResourceBytes(large_icon_resources_data_, resource_id));
      if (large_data.get()) {
        scoped_nsobject<NSData> ns_large_data(
            [[NSData alloc] initWithBytes:large_data->front()
                                   length:large_data->size()]);
        NSImageRep* image_rep =
            [NSBitmapImageRep imageRepWithData:ns_large_data];
        if (image_rep)
          [ns_image addRepresentation:image_rep];
      }
    }

    base::AutoLock lock(*lock_);

    // Another thread raced the load and has already cached the image.
    if (images_.count(resource_id)) {
      return *images_[resource_id];
    }

    gfx::Image* image = new gfx::Image(ns_image.release());
    images_[resource_id] = image;
    return *image;
  }

  LOG(WARNING) << "Unable to load image with id " << resource_id;
  NOTREACHED();  // Want to assert in debug mode.
  return *GetEmptyImage();
}

}  // namespace ui
