// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_LOAD_FROM_MEMORY_CACHE_DETAILS_H__
#define CHROME_BROWSER_LOAD_FROM_MEMORY_CACHE_DETAILS_H__
#pragma once

#include "base/basictypes.h"
#include "googleurl/src/gurl.h"

class LoadFromMemoryCacheDetails {
 public:
   LoadFromMemoryCacheDetails(
       const GURL& url,
       int pid,
       int cert_id,
       int cert_status);
  ~LoadFromMemoryCacheDetails();

  const GURL& url() const { return url_; }
  int pid() const { return pid_; }
  int ssl_cert_id() const { return cert_id_; }
  int ssl_cert_status() const { return cert_status_; }

 private:
  GURL url_;
  int pid_;
  int cert_id_;
  int cert_status_;

  DISALLOW_COPY_AND_ASSIGN(LoadFromMemoryCacheDetails);
};

#endif  // CHROME_BROWSER_LOAD_FROM_MEMORY_CACHE_DETAILS_H__
