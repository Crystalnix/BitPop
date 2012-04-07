// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/render_widget_host.h"

#include "content/browser/renderer_host/render_widget_host_view.h"

void RenderWidgetHost::OnMsgCreatePluginContainer(gfx::PluginWindowHandle id) {
  // TODO(piman): view_ can only be NULL with delayed view creation in
  // extensions (see ExtensionHost::CreateRenderViewSoon). Figure out how to
  // support plugins in that case.
  if (view_) {
    view_->CreatePluginContainer(id);
  } else {
    deferred_plugin_handles_.push_back(id);
  }
}

void RenderWidgetHost::OnMsgDestroyPluginContainer(gfx::PluginWindowHandle id) {
  if (view_) {
    view_->DestroyPluginContainer(id);
  } else {
    for (int i = 0;
         i < static_cast<int>(deferred_plugin_handles_.size());
         i++) {
      if (deferred_plugin_handles_[i] == id) {
        deferred_plugin_handles_.erase(deferred_plugin_handles_.begin() + i);
        i--;
      }
    }
  }
}
