// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_DOWNLOAD_DANGER_TYPE_H_
#define CONTENT_PUBLIC_BROWSER_DOWNLOAD_DANGER_TYPE_H_
#pragma once

namespace content {

// This enum is also used by histograms.  Do not change the ordering or remove
// items.
enum DownloadDangerType {
  // The download is safe.
  DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS = 0,

  // A dangerous file to the system (e.g.: a pdf or extension from
  // places other than gallery).
  DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE,

  // Safebrowsing download service shows this URL leads to malicious file
  // download.
  DOWNLOAD_DANGER_TYPE_DANGEROUS_URL,

  // SafeBrowsing download service shows this file content as being malicious.
  DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT,

  // The content of this download may be malicious (e.g., extension is exe but
  // SafeBrowsing has not finished checking the content).
  DOWNLOAD_DANGER_TYPE_MAYBE_DANGEROUS_CONTENT,

  // Memory space for histograms is determined by the max.
  // ALWAYS ADD NEW VALUES BEFORE THIS ONE.
  DOWNLOAD_DANGER_TYPE_MAX
};

}

#endif  // CONTENT_PUBLIC_BROWSER_DOWNLOAD_DANGER_TYPE_H_
