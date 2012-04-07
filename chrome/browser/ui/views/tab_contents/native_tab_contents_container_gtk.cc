// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tab_contents/native_tab_contents_container_gtk.h"

#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/tab_contents/tab_contents_container.h"
#include "chrome/browser/ui/views/tab_contents/tab_contents_view_views.h"
#include "content/browser/renderer_host/render_widget_host_view.h"
#include "content/browser/tab_contents/interstitial_page.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/views/focus/focus_manager.h"

using content::WebContents;

////////////////////////////////////////////////////////////////////////////////
// NativeTabContentsContainerGtk, public:

NativeTabContentsContainerGtk::NativeTabContentsContainerGtk(
    TabContentsContainer* container)
    : container_(container),
      focus_callback_id_(0) {
  set_id(VIEW_ID_TAB_CONTAINER_FOCUS_VIEW);
}

NativeTabContentsContainerGtk::~NativeTabContentsContainerGtk() {
}

////////////////////////////////////////////////////////////////////////////////
// NativeTabContentsContainerGtk, NativeTabContentsContainer overrides:

void NativeTabContentsContainerGtk::AttachContents(WebContents* contents) {
  Attach(contents->GetNativeView());
}

void NativeTabContentsContainerGtk::DetachContents(WebContents* contents) {
  gtk_widget_hide(contents->GetNativeView());

  // Now detach the TabContents.
  Detach();

  static_cast<TabContentsViewViews*>(contents->GetView())->Unparent();
}

void NativeTabContentsContainerGtk::SetFastResize(bool fast_resize) {
  set_fast_resize(fast_resize);
}

bool NativeTabContentsContainerGtk::GetFastResize() const {
  return fast_resize();
}

bool NativeTabContentsContainerGtk::FastResizeAtLastLayout() const {
  return fast_resize_at_last_layout();
}

void NativeTabContentsContainerGtk::RenderViewHostChanged(
    RenderViewHost* old_host,
    RenderViewHost* new_host) {
  // If we are focused, we need to pass the focus to the new RenderViewHost.
  if (GetFocusManager()->GetFocusedView() == this)
    OnFocus();
}

views::View* NativeTabContentsContainerGtk::GetView() {
  return this;
}

void NativeTabContentsContainerGtk::WebContentsFocused(WebContents* contents) {
  // Called when the tab contents native view gets focused (typically through a
  // user click).  We make ourself the focused view, so the focus is restored
  // properly when the browser window is deactivated/reactivated.
  views::FocusManager* focus_manager = GetFocusManager();
  if (!focus_manager) {
    NOTREACHED();
    return;
  }
  focus_manager->SetFocusedView(this);
}

////////////////////////////////////////////////////////////////////////////////
// NativeTabContentsContainerGtk, views::View overrides:

bool NativeTabContentsContainerGtk::SkipDefaultKeyEventProcessing(
    const views::KeyEvent& e) {
  // Don't look-up accelerators or tab-traverse if we are showing a non-crashed
  // TabContents.
  // We'll first give the page a chance to process the key events.  If it does
  // not process them, they'll be returned to us and we'll treat them as
  // accelerators then.
  return container_->web_contents() &&
         !container_->web_contents()->IsCrashed();
}

views::FocusTraversable* NativeTabContentsContainerGtk::GetFocusTraversable() {
  return NULL;
}

bool NativeTabContentsContainerGtk::IsFocusable() const {
  // We need to be focusable when our contents is not a view hierarchy, as
  // clicking on the contents needs to focus us.
  return container_->web_contents() != NULL;
}

void NativeTabContentsContainerGtk::OnFocus() {
  if (container_->web_contents())
    container_->web_contents()->Focus();
}

void NativeTabContentsContainerGtk::RequestFocus() {
  // This is a hack to circumvent the fact that a view does not explicitly get
  // a call to set the focus if it already has the focus. This causes a problem
  // with tabs such as the TabContents that instruct the RenderView that it got
  // focus when they actually get the focus. When switching from one TabContents
  // tab that has focus to another TabContents tab that had focus, since the
  // TabContentsContainerView already has focus, OnFocus() would not be called
  // and the RenderView would not get notified it got focused.
  // By clearing the focused view before-hand, we ensure OnFocus() will be
  // called.
  views::FocusManager* focus_manager = GetFocusManager();
  if (focus_manager)
    focus_manager->SetFocusedView(NULL);
  View::RequestFocus();
}

void NativeTabContentsContainerGtk::AboutToRequestFocusFromTabTraversal(
    bool reverse) {
  if (!container_->web_contents())
    return;
  // Give an opportunity to the tab to reset its focus.
  if (container_->web_contents()->GetInterstitialPage()) {
    container_->web_contents()->GetInterstitialPage()->FocusThroughTabTraversal(
        reverse);
    return;
  }
  container_->web_contents()->FocusThroughTabTraversal(reverse);
}

void NativeTabContentsContainerGtk::GetAccessibleState(
    ui::AccessibleViewState* state) {
  state->role = ui::AccessibilityTypes::ROLE_GROUPING;
}

////////////////////////////////////////////////////////////////////////////////
// NativeTabContentsContainer, public:

// static
NativeTabContentsContainer* NativeTabContentsContainer::CreateNativeContainer(
    TabContentsContainer* container) {
  return new NativeTabContentsContainerGtk(container);
}
