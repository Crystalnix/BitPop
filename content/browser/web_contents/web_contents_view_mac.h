// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_WEB_CONTENTS_WEB_CONTENTS_VIEW_MAC_H_
#define CONTENT_BROWSER_WEB_CONTENTS_WEB_CONTENTS_VIEW_MAC_H_

#import <Cocoa/Cocoa.h>

#include <string>
#include <vector>

#include "base/memory/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#include "content/port/browser/render_view_host_delegate_view.h"
#include "content/public/browser/web_contents_view.h"
#include "ui/base/cocoa/base_view.h"
#include "ui/gfx/size.h"

@class FocusTracker;
class SkBitmap;
class WebContentsImpl;
class WebContentsViewMac;
@class WebDragDest;
@class WebDragSource;

namespace content {
class WebContentsViewDelegate;
}

namespace gfx {
class Point;
}

@interface WebContentsViewCocoa : BaseView {
 @private
  WebContentsViewMac* webContentsView_;  // WEAK; owns us
  scoped_nsobject<WebDragSource> dragSource_;
  scoped_nsobject<WebDragDest> dragDest_;
  BOOL mouseDownCanMoveWindow_;
}

- (void)setMouseDownCanMoveWindow:(BOOL)canMove;

// Expose this, since sometimes one needs both the NSView and the
// WebContentsImpl.
- (WebContentsImpl*)webContents;
@end

// Mac-specific implementation of the WebContentsView. It owns an NSView that
// contains all of the contents of the tab and associated child views.
class WebContentsViewMac
    : public content::WebContentsView,
      public content::RenderViewHostDelegateView {
 public:
  // The corresponding WebContentsImpl is passed in the constructor, and manages
  // our lifetime. This doesn't need to be the case, but is this way currently
  // because that's what was easiest when they were split.
  WebContentsViewMac(WebContentsImpl* web_contents,
                     content::WebContentsViewDelegate* delegate);
  virtual ~WebContentsViewMac();

  // WebContentsView implementation --------------------------------------------

  virtual void CreateView(const gfx::Size& initial_size) OVERRIDE;
  virtual content::RenderWidgetHostView* CreateViewForWidget(
      content::RenderWidgetHost* render_widget_host) OVERRIDE;
  virtual gfx::NativeView GetNativeView() const OVERRIDE;
  virtual gfx::NativeView GetContentNativeView() const OVERRIDE;
  virtual gfx::NativeWindow GetTopLevelNativeWindow() const OVERRIDE;
  virtual void GetContainerBounds(gfx::Rect* out) const OVERRIDE;
  virtual void RenderViewCreated(content::RenderViewHost* host) OVERRIDE;
  virtual void SetPageTitle(const string16& title) OVERRIDE;
  virtual void OnTabCrashed(base::TerminationStatus status,
                            int error_code) OVERRIDE;
  virtual void SizeContents(const gfx::Size& size) OVERRIDE;
  virtual void Focus() OVERRIDE;
  virtual void SetInitialFocus() OVERRIDE;
  virtual void StoreFocus() OVERRIDE;
  virtual void RestoreFocus() OVERRIDE;
  virtual bool IsDoingDrag() const OVERRIDE;
  virtual void CancelDragAndCloseTab() OVERRIDE;
  virtual WebDropData* GetDropData() const OVERRIDE;
  virtual bool IsEventTracking() const OVERRIDE;
  virtual void CloseTabAfterEventTracking() OVERRIDE;
  virtual gfx::Rect GetViewBounds() const OVERRIDE;

  // Backend implementation of RenderViewHostDelegateView.
  virtual void ShowContextMenu(
      const content::ContextMenuParams& params) OVERRIDE;
  virtual void ShowPopupMenu(const gfx::Rect& bounds,
                             int item_height,
                             double item_font_size,
                             int selected_item,
                             const std::vector<WebMenuItem>& items,
                             bool right_aligned,
                             bool allow_multiple_selection) OVERRIDE;
  virtual void StartDragging(const WebDropData& drop_data,
                             WebKit::WebDragOperationsMask allowed_operations,
                             const gfx::ImageSkia& image,
                             const gfx::Point& image_offset) OVERRIDE;
  virtual void UpdateDragCursor(WebKit::WebDragOperation operation) OVERRIDE;
  virtual void GotFocus() OVERRIDE;
  virtual void TakeFocus(bool reverse) OVERRIDE;

  // A helper method for closing the tab in the
  // CloseTabAfterEventTracking() implementation.
  void CloseTab();

  WebContentsImpl* web_contents() { return web_contents_; }
  content::WebContentsViewDelegate* delegate() { return delegate_.get(); }

 private:
  // The WebContentsImpl whose contents we display.
  WebContentsImpl* web_contents_;

  // The Cocoa NSView that lives in the view hierarchy.
  scoped_nsobject<WebContentsViewCocoa> cocoa_view_;

  // Keeps track of which NSView has focus so we can restore the focus when
  // focus returns.
  scoped_nsobject<FocusTracker> focus_tracker_;

  // Our optional delegate.
  scoped_ptr<content::WebContentsViewDelegate> delegate_;

  DISALLOW_COPY_AND_ASSIGN(WebContentsViewMac);
};

#endif  // CONTENT_BROWSER_WEB_CONTENTS_WEB_CONTENTS_VIEW_MAC_H_
