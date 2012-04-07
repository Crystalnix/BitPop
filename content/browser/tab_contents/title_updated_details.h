// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_TAB_CONTENTS_TITLE_UPDATED_DETAILS_
#define CONTENT_BROWSER_TAB_CONTENTS_TITLE_UPDATED_DETAILS_
#pragma once

#include "base/basictypes.h"

namespace content {
class NavigationEntry;
}

class TitleUpdatedDetails {
 public:
   TitleUpdatedDetails(const content::NavigationEntry* entry, bool explicit_set)
      : entry_(entry),
        explicit_set_(explicit_set) {}
  ~TitleUpdatedDetails() {}

  // |entry()| may return null.
  const content::NavigationEntry* entry() const { return entry_; }
  bool explicit_set() const { return explicit_set_; }

 private:
  const content::NavigationEntry* entry_;  // |entry_| may be null.
  bool explicit_set_;  // If a synthesized title.

  TitleUpdatedDetails() {}

  DISALLOW_COPY_AND_ASSIGN(TitleUpdatedDetails);
};

#endif  // CONTENT_BROWSER_TAB_CONTENTS_TITLE_UPDATED_DETAILS_
