// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/drag_utils.h"

#include "base/logging.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/dragdrop/os_exchange_data_provider_aura.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/point.h"
#include "ui/gfx/size.h"

namespace drag_utils {

void SetDragImageOnDataObject(const SkBitmap& bitmap,
                              const gfx::Size& size,
                              const gfx::Point& cursor_offset,
                              ui::OSExchangeData* data_object) {
  ui::OSExchangeDataProviderAura& provider(
      static_cast<ui::OSExchangeDataProviderAura&>(data_object->provider()));
  provider.set_drag_image(bitmap);
}

}  // namespace drag_utils
