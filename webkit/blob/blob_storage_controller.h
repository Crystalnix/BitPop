// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_BLOB_BLOB_STORAGE_CONTROLLER_H_
#define WEBKIT_BLOB_BLOB_STORAGE_CONTROLLER_H_

#include "base/hash_tables.h"
#include "base/memory/ref_counted.h"
#include "base/process.h"

class GURL;
class FilePath;

namespace base {
class Time;
}
namespace net {
class UploadData;
}

namespace webkit_blob {

class BlobData;

// This class handles the logistics of blob Storage within the browser process.
class BlobStorageController {
 public:
  BlobStorageController();
  ~BlobStorageController();

  void RegisterBlobUrl(const GURL& url, const BlobData* blob_data);
  void RegisterBlobUrlFrom(const GURL& url, const GURL& src_url);
  void UnregisterBlobUrl(const GURL& url);
  BlobData* GetBlobDataFromUrl(const GURL& url);

  // If there is any blob reference in the upload data, it will get resolved
  // and updated in place.
  void ResolveBlobReferencesInUploadData(net::UploadData* upload_data);

 private:
  friend class ViewBlobInternalsJob;

  void AppendStorageItems(BlobData* target_blob_data,
                          BlobData* src_blob_data,
                          uint64 offset,
                          uint64 length);
  void AppendFileItem(BlobData* target_blob_data,
                      const FilePath& file_path, uint64 offset, uint64 length,
                      const base::Time& expected_modification_time);

  typedef base::hash_map<std::string, scoped_refptr<BlobData> > BlobMap;
  BlobMap blob_map_;

  DISALLOW_COPY_AND_ASSIGN(BlobStorageController);
};

}  // namespace webkit_blob

#endif  // WEBKIT_BLOB_BLOB_STORAGE_CONTROLLER_H_
