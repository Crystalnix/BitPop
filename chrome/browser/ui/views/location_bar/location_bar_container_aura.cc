// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/location_bar_container.h"

#include "chrome/browser/ui/views/location_bar/location_bar_view.h"

void LocationBarContainer::SetInToolbar(bool in_toolbar) {
  if (animator_.IsAnimating())
    animator_.Cancel();
  // See comment in PlatformInit() as to why we do this.
  SetPaintToLayer(!in_toolbar);
  if (!in_toolbar) {
    layer()->SetFillsBoundsOpaquely(false);
    StackAtTop();
  }
}

void LocationBarContainer::OnFocus() {
}

void LocationBarContainer::PlatformInit() {
  view_parent_ = this;
  // Ideally we would turn on layer painting here, but this poses problems with
  // infobar arrows. So, instead we turn on the layer when animating and turn it
  // off when done.
}

// static
SkColor LocationBarContainer::GetBackgroundColor() {
  return SK_ColorWHITE;
}

void LocationBarContainer::StackAtTop() {
  // TODO: this is hack. The problem is NativeViewHostAura does an AddChild(),
  // which places its layer at the top of the stack.
  if (layer())
    layer()->parent()->StackAtTop(layer());
}
