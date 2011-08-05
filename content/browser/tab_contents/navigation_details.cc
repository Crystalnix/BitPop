// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/tab_contents/navigation_details.h"

namespace content {

LoadCommittedDetails::LoadCommittedDetails()
    : entry(NULL),
      type(NavigationType::UNKNOWN),
      previous_entry_index(-1),
      is_auto(false),
      did_replace_entry(false),
      is_in_page(false),
      is_main_frame(true),
      http_status_code(0) {
}

}  // namespace content
