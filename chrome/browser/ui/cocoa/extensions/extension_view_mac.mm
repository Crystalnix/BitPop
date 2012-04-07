// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/extensions/extension_view_mac.h"

#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/tab_contents/moving_to_content/tab_contents_view_mac.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/renderer_host/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"

// The minimum/maximum dimensions of the popup.
const CGFloat ExtensionViewMac::kMinWidth = 25.0;
const CGFloat ExtensionViewMac::kMinHeight = 25.0;
const CGFloat ExtensionViewMac::kMaxWidth = 800.0;
const CGFloat ExtensionViewMac::kMaxHeight = 600.0;

ExtensionViewMac::ExtensionViewMac(ExtensionHost* extension_host,
                                   Browser* browser)
    : browser_(browser),
      extension_host_(extension_host) {
  DCHECK(extension_host_);
  [native_view() setHidden:YES];
}

ExtensionViewMac::~ExtensionViewMac() {
}

void ExtensionViewMac::Init() {
  CreateWidgetHostView();
}

gfx::NativeView ExtensionViewMac::native_view() {
  return extension_host_->host_contents()->GetView()->GetNativeView();
}

RenderViewHost* ExtensionViewMac::render_view_host() const {
  return extension_host_->render_view_host();
}

void ExtensionViewMac::DidStopLoading() {
  ShowIfCompletelyLoaded();
}

void ExtensionViewMac::SetBackground(const SkBitmap& background) {
  if (!pending_background_.empty() && render_view_host()->view()) {
    render_view_host()->view()->SetBackground(background);
  } else {
    pending_background_ = background;
  }
  ShowIfCompletelyLoaded();
}

void ExtensionViewMac::UpdatePreferredSize(const gfx::Size& new_size) {
  // When we update the size, our container becomes visible. Stay hidden until
  // the host is loaded.
  pending_preferred_size_ = new_size;
  if (!extension_host_->did_stop_loading())
    return;

  // No need to use CA here, our caller calls us repeatedly to animate the
  // resizing.
  NSView* view = native_view();
  NSRect frame = [view frame];
  frame.size.width = new_size.width();
  frame.size.height = new_size.height();

  // |new_size| is in pixels. Convert to view units.
  frame.size = [view convertSize:frame.size fromView:nil];

  // On first display of some extensions, this function is called with zero
  // width after the correct size has been set. Bail if zero is seen, assuming
  // that an extension's view doesn't want any dimensions to ever be zero.
  // TODO(andybons): Verify this assumption and look into WebCore's
  // |contentesPreferredWidth| to see why this is occurring.
  if (NSIsEmptyRect(frame))
    return;

  DCHECK([view isKindOfClass:[TabContentsViewCocoa class]]);
  TabContentsViewCocoa* hostView = (TabContentsViewCocoa*)view;

  // TabContentsViewCocoa overrides setFrame but not setFrameSize.
  // We need to defer the update back to the RenderWidgetHost so we don't
  // get the flickering effect on 10.5 of http://crbug.com/31970
  [hostView setFrameWithDeferredUpdate:frame];
  [hostView setNeedsDisplay:YES];
}

void ExtensionViewMac::RenderViewCreated() {
  // Do not allow webkit to draw scroll bars on views smaller than
  // the largest size view allowed.  The view will be resized to make
  // scroll bars unnecessary.  Scroll bars change the height of the
  // view, so not drawing them is necessary to avoid infinite resizing.
  gfx::Size largest_popup_size(
      CGSizeMake(ExtensionViewMac::kMaxWidth, ExtensionViewMac::kMaxHeight));
  extension_host_->DisableScrollbarsForSmallWindows(largest_popup_size);

  if (!pending_background_.empty() && render_view_host()->view()) {
    render_view_host()->view()->SetBackground(pending_background_);
    pending_background_.reset();
  }
}

void ExtensionViewMac::WindowFrameChanged() {
  if (render_view_host()->view())
    render_view_host()->view()->WindowFrameChanged();
}

void ExtensionViewMac::CreateWidgetHostView() {
  extension_host_->CreateRenderViewSoon();
}

void ExtensionViewMac::ShowIfCompletelyLoaded() {
  // We wait to show the ExtensionView until it has loaded, and the view has
  // actually been created. These can happen in different orders.
  if (extension_host_->did_stop_loading()) {
    [native_view() setHidden:NO];
    UpdatePreferredSize(pending_preferred_size_);
  }
}
