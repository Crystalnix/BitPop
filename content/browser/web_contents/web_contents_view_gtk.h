// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_WEB_CONTENTS_WEB_CONTENTS_VIEW_GTK_H_
#define CONTENT_BROWSER_WEB_CONTENTS_WEB_CONTENTS_VIEW_GTK_H_

#include <gtk/gtk.h>

#include <vector>

#include "base/memory/scoped_ptr.h"
#include "content/common/content_export.h"
#include "content/port/browser/render_view_host_delegate_view.h"
#include "content/public/browser/web_contents_view.h"
#include "ui/base/gtk/focus_store_gtk.h"
#include "ui/base/gtk/gtk_signal.h"
#include "ui/base/gtk/owned_widget_gtk.h"

class WebContentsImpl;

namespace content {

class WebContents;
class WebContentsViewDelegate;
class WebDragDestDelegate;
class WebDragDestGtk;
class WebDragSourceGtk;

class CONTENT_EXPORT WebContentsViewGtk
    : public WebContentsView,
      public RenderViewHostDelegateView {
 public:
  // The corresponding WebContentsImpl is passed in the constructor, and manages
  // our lifetime. This doesn't need to be the case, but is this way currently
  // because that's what was easiest when they were split. We optionally take
  // |wrapper| which creates an intermediary widget layer for features from the
  // Embedding layer that lives with the WebContentsView.
  WebContentsViewGtk(WebContentsImpl* web_contents,
                     WebContentsViewDelegate* delegate);
  virtual ~WebContentsViewGtk();

  WebContentsViewDelegate* delegate() const { return delegate_.get(); }
  WebContents* web_contents();

  // WebContentsView implementation --------------------------------------------

  virtual void CreateView(const gfx::Size& initial_size) OVERRIDE;
  virtual content::RenderWidgetHostView* CreateViewForWidget(
      content::RenderWidgetHost* render_widget_host) OVERRIDE;

  virtual gfx::NativeView GetNativeView() const OVERRIDE;
  virtual gfx::NativeView GetContentNativeView() const OVERRIDE;
  virtual gfx::NativeWindow GetTopLevelNativeWindow() const OVERRIDE;
  virtual void GetContainerBounds(gfx::Rect* out) const OVERRIDE;
  virtual void SetPageTitle(const string16& title) OVERRIDE;
  virtual void OnTabCrashed(base::TerminationStatus status,
                            int error_code) OVERRIDE;
  virtual void SizeContents(const gfx::Size& size) OVERRIDE;
  virtual void RenderViewCreated(content::RenderViewHost* host) OVERRIDE;
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
                             WebKit::WebDragOperationsMask allowed_ops,
                             const gfx::ImageSkia& image,
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
  CHROMEGTK_CALLBACK_1(WebContentsViewGtk, gboolean, OnFocus, GtkDirectionType);

  // Used to adjust the size of its children when the size of |expanded_| is
  // changed.
  CHROMEGTK_CALLBACK_2(WebContentsViewGtk, void, OnChildSizeRequest,
                       GtkWidget*, GtkRequisition*);

  // Used to propagate the size change of |expanded_| to our RWHV to resize the
  // renderer content.
  CHROMEGTK_CALLBACK_1(WebContentsViewGtk, void, OnSizeAllocate,
                       GtkAllocation*);

  // The WebContentsImpl whose contents we display.
  WebContentsImpl* web_contents_;

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
  scoped_ptr<WebContentsViewDelegate> delegate_;

  // The size we want the view to be.  We keep this in a separate variable
  // because resizing in GTK+ is async.
  gfx::Size requested_size_;

  DISALLOW_COPY_AND_ASSIGN(WebContentsViewGtk);
};

}  // namespace content

#endif  // CONTENT_BROWSER_WEB_CONTENTS_WEB_CONTENTS_VIEW_GTK_H_
