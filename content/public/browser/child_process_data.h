// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_CHILD_PROCESS_DATA_H_
#define CONTENT_PUBLIC_BROWSER_CHILD_PROCESS_DATA_H_
#pragma once

#include "base/process.h"
#include "base/string16.h"
#include "content/common/content_export.h"
#include "content/public/common/process_type.h"

namespace content {

// Holds information about a child process.
struct ChildProcessData {
  // The type of the process.
  content::ProcessType type;

  // The name of the process.  i.e. for plugins it might be Flash, while for
  // for workers it might be the domain that it's from.
  string16 name;

  // The unique identifier for this child process. This identifier is NOT a
  // process ID, and will be unique for all types of child process for
  // one run of the browser.
  int id;

  // The handle to the process.
  base::ProcessHandle handle;

  ChildProcessData(content::ProcessType type)
    : type(type), id(0), handle(base::kNullProcessHandle) {
}
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_CHILD_PROCESS_DATA_H_
