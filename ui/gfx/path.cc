// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/path.h"

#include "base/logging.h"

namespace gfx {

Path::Path()
    : SkPath() {
}

Path::Path(const Point* points, size_t count) {
  DCHECK(count > 1);
  moveTo(SkIntToScalar(points[0].x), SkIntToScalar(points[0].y));
  for (size_t i = 1; i < count; ++i)
    lineTo(SkIntToScalar(points[i].x), SkIntToScalar(points[i].y));
}

Path::~Path() {
}

}  // namespace gfx
