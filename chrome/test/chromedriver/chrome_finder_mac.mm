// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Foundation/Foundation.h>

#include "base/file_path.h"
#include "base/mac/foundation_util.h"

void GetApplicationDirs(std::vector<FilePath>* app_dirs) {
  FilePath user_app_dir;
  if (base::mac::GetUserDirectory(NSApplicationDirectory, &user_app_dir))
    app_dirs->push_back(user_app_dir);
  FilePath local_app_dir;
  if (base::mac::GetLocalDirectory(NSApplicationDirectory, &local_app_dir))
    app_dirs->push_back(local_app_dir);
}
