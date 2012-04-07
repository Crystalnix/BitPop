// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_FILE_METADATA_MAC_H_
#define CONTENT_BROWSER_FILE_METADATA_MAC_H_
#pragma once

class FilePath;
class GURL;

namespace file_metadata {

// Adds origin metadata to the file.
// |source| should be the source URL for the download, and |referrer| should be
// the URL the user initiated the download from.
void AddOriginMetadataToFile(const FilePath& file, const GURL& source,
                             const GURL& referrer);

// Adds quarantine metadata to the file, assuming it has already been
// quarantined by the OS.
// |source| should be the source URL for the download, and |referrer| should be
// the URL the user initiated the download from.
void AddQuarantineMetadataToFile(const FilePath& file, const GURL& source,
                                 const GURL& referrer);

}  // namespace file_metadata

#endif  // CONTENT_BROWSER_FILE_METADATA_MAC_H_
