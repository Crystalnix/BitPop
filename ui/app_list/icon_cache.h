// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_APP_LIST_ICON_CACHE_H_
#define UI_APP_LIST_ICON_CACHE_H_

#include <map>
#include <string>

#include "base/basictypes.h"
#include "ui/app_list/app_list_export.h"
#include "ui/gfx/image/image_skia.h"

namespace gfx {
class Size;
}

namespace app_list {

// IconCache stores processed image, keyed by the source image and desired size.
class APP_LIST_EXPORT IconCache {
 public:
  static void CreateInstance();
  static void DeleteInstance();

  static IconCache* GetInstance();

  void MarkAllEntryUnused();
  void PurgeAllUnused();

  bool Get(const gfx::ImageSkia& src,
           const gfx::Size& size,
           gfx::ImageSkia* processed);
  void Put(const gfx::ImageSkia& src,
           const gfx::Size& size,
           const gfx::ImageSkia& processed);

 private:
  struct Item {
    gfx::ImageSkia image;
    bool used;
  };
  typedef std::map<std::string, Item> Cache;

  IconCache();
  ~IconCache();

  static IconCache* instance_;

  Cache cache_;

  DISALLOW_COPY_AND_ASSIGN(IconCache);
};

}  // namespace app_list

#endif  // UI_APP_LIST_ICON_CACHE_H_
