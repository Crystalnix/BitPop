// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/render_widget_host_view.h"

#include "base/logging.h"
#include "content/browser/accessibility/browser_accessibility_manager.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebScreenInfo.h"

#if defined(TOOLKIT_USES_GTK)
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "content/browser/renderer_host/gtk_window_utils.h"
#endif

RenderWidgetHostView::RenderWidgetHostView()
    : popup_type_(WebKit::WebPopupTypeNone),
      mouse_locked_(false),
      selection_text_offset_(0),
      selection_range_(ui::Range::InvalidRange()) {
}

RenderWidgetHostView::~RenderWidgetHostView() {
  DCHECK(!mouse_locked_);
}

void RenderWidgetHostView::SetBackground(const SkBitmap& background) {
  background_ = background;
}

BrowserAccessibilityManager*
    RenderWidgetHostView::GetBrowserAccessibilityManager() const {
  return browser_accessibility_manager_.get();
}

void RenderWidgetHostView::SetBrowserAccessibilityManager(
    BrowserAccessibilityManager* manager) {
  browser_accessibility_manager_.reset(manager);
}

void RenderWidgetHostView::SelectionChanged(const string16& text,
                                            size_t offset,
                                            const ui::Range& range) {
  selection_text_ = text;
  selection_text_offset_ = offset;
  selection_range_.set_start(range.start());
  selection_range_.set_end(range.end());
}
