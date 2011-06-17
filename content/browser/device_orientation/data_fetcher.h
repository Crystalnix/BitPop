// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DEVICE_ORIENTATION_DATA_FETCHER_H_
#define CONTENT_BROWSER_DEVICE_ORIENTATION_DATA_FETCHER_H_

namespace device_orientation {

class Orientation;

class DataFetcher {
 public:
  virtual ~DataFetcher() {}
  virtual bool GetOrientation(Orientation*) = 0;
};

}  // namespace device_orientation

#endif  // CONTENT_BROWSER_DEVICE_ORIENTATION_DATA_FETCHER_H_
