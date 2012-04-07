// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_DEFAULT_USER_IMAGES_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_DEFAULT_USER_IMAGES_H_

#include <cstddef>  // for size_t
#include <string>

class SkBitmap;

namespace chromeos {

// Returns path to default user image with specified index.
// The path is used in Local State to distinguish default images.
// This function is obsolete and is preserved only for compatibility with older
// profiles which don't user separate image index and path.
std::string GetDefaultImagePath(int index);

// Checks if given path is one of the default ones. If it is, returns true
// and its index through |image_id|. If not, returns false.
bool IsDefaultImagePath(const std::string& path, int* image_id);

// Returns URL to default user image with specified index.
std::string GetDefaultImageUrl(int index);

// Checks if the given URL points to one of the default images. If it is,
// returns true and its index through |image_id|. If not, returns false.
bool IsDefaultImageUrl(const std::string url, int* image_id);

// Returns bitmap of default user image with specified index.
const SkBitmap& GetDefaultImage(int index);

// Resource IDs of default user images.
extern const int kDefaultImageResources[];

// Number of default images.
extern const int kDefaultImagesCount;

// Image index to be used in histograms when user image is taken from file.
extern const int kHistogramImageFromFile;

// Image index to be used in histograms when user image is taken from camera.
extern const int kHistogramImageFromCamera;

// Image index to be used in histograms when user selects a previously used
// image from camera/file.
extern const int kHistogramImageOld;

// Image index to be used in histograms when user image is taken from profile.
extern const int kHistogramImageFromProfile;

// Number of possible user image indices to be used in histograms.
extern const int kHistogramImagesCount;

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_DEFAULT_USER_IMAGES_H_
