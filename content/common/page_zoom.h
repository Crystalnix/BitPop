// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_PAGE_ZOOM_H_
#define CONTENT_COMMON_PAGE_ZOOM_H_
#pragma once

#include "base/basictypes.h"

class PageZoom {
 public:
  // This enum is the parameter to various text/page zoom commands so we know
  // what the specific zoom command is.
  enum Function {
    ZOOM_OUT = -1,
    RESET    = 0,
    ZOOM_IN  = 1,
  };

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(PageZoom);
};

#endif  // CONTENT_COMMON_PAGE_ZOOM_H_
