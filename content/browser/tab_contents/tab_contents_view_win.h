// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_TAB_CONTENTS_TAB_CONTENTS_VIEW_WIN_H_
#define CONTENT_BROWSER_TAB_CONTENTS_TAB_CONTENTS_VIEW_WIN_H_
#pragma once

#include "content/browser/tab_contents/tab_contents_view_helper.h"
#include "content/public/browser/web_contents_view.h"
#include "ui/base/win/window_impl.h"

class RenderWidgetHostViewWin;

// An implementation of WebContentsView for Windows.
class TabContentsViewWin : public content::WebContentsView,
                           public ui::WindowImpl {
 public:
  // TODO(jam): make this take a WebContents once it's created from content.
  explicit TabContentsViewWin(content::WebContents* web_contents);
  virtual ~TabContentsViewWin();

  void SetParent(HWND parent);

  BEGIN_MSG_MAP_EX(TabContentsViewWin)
    MESSAGE_HANDLER(WM_WINDOWPOSCHANGED, OnWindowPosChanged)
  END_MSG_MAP()

  // Overridden from WebContentsView:
  virtual void CreateView(const gfx::Size& initial_size) OVERRIDE;
  virtual RenderWidgetHostView* CreateViewForWidget(
      RenderWidgetHost* render_widget_host) OVERRIDE;
  virtual gfx::NativeView GetNativeView() const OVERRIDE;
  virtual gfx::NativeView GetContentNativeView() const OVERRIDE;
  virtual gfx::NativeWindow GetTopLevelNativeWindow() const OVERRIDE;
  virtual void GetContainerBounds(gfx::Rect *out) const OVERRIDE;
  virtual void SetPageTitle(const string16& title) OVERRIDE;
  virtual void OnTabCrashed(base::TerminationStatus status,
                            int error_code) OVERRIDE;
  virtual void SizeContents(const gfx::Size& size) OVERRIDE;
  virtual void RenderViewCreated(RenderViewHost* host) OVERRIDE;
  virtual void Focus() OVERRIDE;
  virtual void SetInitialFocus() OVERRIDE;
  virtual void StoreFocus() OVERRIDE;
  virtual void RestoreFocus() OVERRIDE;
  virtual bool IsDoingDrag() const OVERRIDE;
  virtual void CancelDragAndCloseTab() OVERRIDE;
  virtual bool IsEventTracking() const OVERRIDE;
  virtual void CloseTabAfterEventTracking() OVERRIDE;
  virtual void GetViewBounds(gfx::Rect* out) const OVERRIDE;
  virtual void InstallOverlayView(gfx::NativeView view) OVERRIDE;
  virtual void RemoveOverlayView() OVERRIDE;

  // Implementation of RenderViewHostDelegate::View.
  virtual void CreateNewWindow(
      int route_id,
      const ViewHostMsg_CreateWindow_Params& params) OVERRIDE;
  virtual void CreateNewWidget(int route_id,
                               WebKit::WebPopupType popup_type) OVERRIDE;
  virtual void CreateNewFullscreenWidget(int route_id) OVERRIDE;
  virtual void ShowCreatedWindow(int route_id,
                                 WindowOpenDisposition disposition,
                                 const gfx::Rect& initial_pos,
                                 bool user_gesture) OVERRIDE;
  virtual void ShowCreatedWidget(int route_id,
                                 const gfx::Rect& initial_pos) OVERRIDE;
  virtual void ShowCreatedFullscreenWidget(int route_id) OVERRIDE;
  virtual void ShowContextMenu(const ContextMenuParams& params) OVERRIDE;
  virtual void ShowPopupMenu(const gfx::Rect& bounds,
                             int item_height,
                             double item_font_size,
                             int selected_item,
                             const std::vector<WebMenuItem>& items,
                             bool right_aligned) OVERRIDE;
  virtual void StartDragging(const WebDropData& drop_data,
                             WebKit::WebDragOperationsMask operations,
                             const SkBitmap& image,
                             const gfx::Point& image_offset) OVERRIDE;
  virtual void UpdateDragCursor(WebKit::WebDragOperation operation) OVERRIDE;
  virtual void GotFocus() OVERRIDE;
  virtual void TakeFocus(bool reverse) OVERRIDE;

  TabContents* tab_contents() const { return tab_contents_; }

 private:
  LRESULT OnWindowPosChanged(
      UINT message, WPARAM wparam, LPARAM lparam, BOOL& handled);

  HWND parent_;

  gfx::Size initial_size_;

  // The TabContents whose contents we display.
  TabContents* tab_contents_;

  RenderWidgetHostViewWin* view_;

  // Common implementations of some WebContentsView methods.
  TabContentsViewHelper tab_contents_view_helper_;

  DISALLOW_COPY_AND_ASSIGN(TabContentsViewWin);
};

#endif  // CONTENT_BROWSER_TAB_CONTENTS_TAB_CONTENTS_VIEW_WIN_H_
