// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_STATE_INFO_H_
#define CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_STATE_INFO_H_
#pragma once

#include "base/file_path.h"
#include "content/common/content_export.h"
#include "content/public/browser/download_danger_type.h"
#include "content/public/common/page_transition_types.h"

// Contains information relating to the process of determining what to do with
// the download.
struct DownloadStateInfo {
  DownloadStateInfo();
  DownloadStateInfo(bool has_user_gesture,
                    bool prompt_user_for_save_location);
  DownloadStateInfo(const FilePath& target,
                    const FilePath& forced_name,
                    bool has_user_gesture,
                    content::PageTransition transition_type,
                    bool prompt_user_for_save_location);

  // Indicates if the download is dangerous.
  CONTENT_EXPORT bool IsDangerous() const;

  // The original name for a dangerous download, specified by the request.
  FilePath target_name;

  // The path where we save the download.  Typically generated.
  FilePath suggested_path;

  // A number that should be added to the suggested path to make it unique.
  // 0 means no number should be appended.  It is eventually incorporated
  // into the final file name.
  int path_uniquifier;

  // True if the download is the result of user action.
  bool has_user_gesture;

  content::PageTransition transition_type;

  // True if we should display the 'save as...' UI and prompt the user
  // for the download location.
  // False if the UI should be suppressed and the download performed to the
  // default location.
  bool prompt_user_for_save_location;

  content::DownloadDangerType danger;

  // True if this download's file name was specified initially.
  FilePath force_file_name;
};

#endif  // CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_STATE_INFO_H_
