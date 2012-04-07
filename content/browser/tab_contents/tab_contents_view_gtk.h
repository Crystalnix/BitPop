// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_TAB_CONTENTS_TAB_CONTENTS_VIEW_GTK_H_
#define CONTENT_BROWSER_TAB_CONTENTS_TAB_CONTENTS_VIEW_GTK_H_
#pragma once

#include <gtk/gtk.h>

#include <vector>

#include "base/memory/scoped_ptr.h"
#include "content/browser/tab_contents/tab_contents_view_helper.h"
#include "content/common/content_export.h"
#include "content/public/browser/web_contents_view.h"
#include "ui/base/gtk/focus_store_gtk.h"
#include "ui/base/gtk/gtk_signal.h"
#include "ui/base/gtk/owned_widget_gtk.h"

namespace content {

class TabContentsViewWrapperGtk;
class WebDragDestDelegate;
class WebDragDestGtk;
class WebDragSourceGtk;

class CONTENT_EXPORT TabContentsViewGtk : public WebContentsView {
 public:
  // The corresponding TabContents is passed in the constructor, and manages
  // our lifetime. This doesn't need to be the case, but is this way currently
  // because that's what was easiest when they were split. We optionally take
  // |wrapper| which creates an intermediary widget layer for features from the
  // Embedding layer that lives with the WebContentsView.
  // TODO(jam): make this take a WebContents once it's created from content.
  explicit TabContentsViewGtk(content::WebContents* web_contents,
                              TabContentsViewWrapperGtk* wrapper);
  virtual ~TabContentsViewGtk();

  // Override the stored focus widget. This call only makes sense when the
  // tab contents is not focused.
  void SetFocusedWidget(GtkWidget* widget);

  TabContentsViewWrapperGtk* wrapper() const { return view_wrapper_.get(); }
  TabContents* tab_contents() { return tab_contents_; }
  GtkWidget* expanded_container() { return expanded_.get(); }
  WebContents* web_contents();

  // Allows our embeder to intercept incoming drag messages.
  void SetDragDestDelegate(WebDragDestDelegate* delegate);

  // WebContentsView implementation --------------------------------------------

  virtual void CreateView(const gfx::Size& initial_size) OVERRIDE;
  virtual RenderWidgetHostView* CreateViewForWidget(
      RenderWidgetHost* render_widget_host) OVERRIDE;

  virtual gfx::NativeView GetNativeView() const OVERRIDE;
  virtual gfx::NativeView GetContentNativeView() const OVERRIDE;
  virtual gfx::NativeWindow GetTopLevelNativeWindow() const OVERRIDE;
  virtual void GetContainerBounds(gfx::Rect* out) const OVERRIDE;
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

  // Backend implementation of RenderViewHostDelegate::View.
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
                             WebKit::WebDragOperationsMask allowed_ops,
                             const SkBitmap& image,
                             const gfx::Point& image_offset) OVERRIDE;
  virtual void UpdateDragCursor(WebKit::WebDragOperation operation) OVERRIDE;
  virtual void GotFocus() OVERRIDE;
  virtual void TakeFocus(bool reverse) OVERRIDE;

 private:
  // Insert the given widget into the content area. Should only be used for
  // web pages and the like (including interstitials and sad tab). Note that
  // this will be perfectly happy to insert overlapping render views, so care
  // should be taken that the correct one is hidden/shown.
  void InsertIntoContentArea(GtkWidget* widget);

  // Handle focus traversal on the render widget native view. Can be overridden
  // by subclasses.
  CHROMEGTK_CALLBACK_1(TabContentsViewGtk, gboolean, OnFocus, GtkDirectionType);

  // Used to adjust the size of its children when the size of |expanded_| is
  // changed.
  CHROMEGTK_CALLBACK_2(TabContentsViewGtk, void, OnChildSizeRequest,
                       GtkWidget*, GtkRequisition*);

  // Used to propagate the size change of |expanded_| to our RWHV to resize the
  // renderer content.
  CHROMEGTK_CALLBACK_1(TabContentsViewGtk, void, OnSizeAllocate,
                       GtkAllocation*);

  // The TabContents whose contents we display.
  TabContents* tab_contents_;

  // Common implementations of some WebContentsView methods.
  TabContentsViewHelper tab_contents_view_helper_;

  // This container holds the tab's web page views. It is a GtkExpandedContainer
  // so that we can control the size of the web pages.
  ui::OwnedWidgetGtk expanded_;

  ui::FocusStoreGtk focus_store_;

  // The helper object that handles drag destination related interactions with
  // GTK.
  scoped_ptr<WebDragDestGtk> drag_dest_;

  // Object responsible for handling drags from the page for us.
  scoped_ptr<WebDragSourceGtk> drag_source_;

  // Our optional views wrapper. If non-NULL, we return this widget as our
  // GetNativeView() and insert |expanded_| as its child in the GtkWidget
  // hierarchy.
  scoped_ptr<TabContentsViewWrapperGtk> view_wrapper_;

  // The size we want the tab contents view to be.  We keep this in a separate
  // variable because resizing in GTK+ is async.
  gfx::Size requested_size_;

  // The overlaid view. Owned by the caller of |InstallOverlayView|; this is a
  // weak reference.
  GtkWidget* overlaid_view_;

  DISALLOW_COPY_AND_ASSIGN(TabContentsViewGtk);
};

}  // namespace content

#endif  // CONTENT_BROWSER_TAB_CONTENTS_TAB_CONTENTS_VIEW_GTK_H_
