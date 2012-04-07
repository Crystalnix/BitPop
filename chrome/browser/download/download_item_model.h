// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOWNLOAD_DOWNLOAD_ITEM_MODEL_H_
#define CHROME_BROWSER_DOWNLOAD_DOWNLOAD_ITEM_MODEL_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/string16.h"

class SavePackage;

namespace content {
class DownloadItem;
}

// This class provides an interface for functions which have different behaviors
// depending on the type of download.
class BaseDownloadItemModel {
 public:
  explicit BaseDownloadItemModel(content::DownloadItem* download)
      : download_(download) { }
  virtual ~BaseDownloadItemModel() { }

  // Cancel the task corresponding to the item.
  virtual void CancelTask() = 0;

  // Get the status text to display.
  virtual string16 GetStatusText() = 0;

  content::DownloadItem* download() { return download_; }

 protected:
  content::DownloadItem* download_;
};

// This class is a model class for DownloadItemView. It provides functionality
// for canceling the downloading, and also the text for displaying downloading
// status.
class DownloadItemModel : public BaseDownloadItemModel {
 public:
  explicit DownloadItemModel(content::DownloadItem* download);
  virtual ~DownloadItemModel() { }

  // Cancel the downloading.
  virtual void CancelTask() OVERRIDE;

  // Get downloading status text.
  virtual string16 GetStatusText() OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(DownloadItemModel);
};

#endif  // CHROME_BROWSER_DOWNLOAD_DOWNLOAD_ITEM_MODEL_H_
