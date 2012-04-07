// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/dom_view.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_preferences_util.h"
#include "chrome/browser/ui/views/tab_contents/tab_contents_view_views.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "ui/views/focus/focus_manager.h"

using content::SiteInstance;
using content::WebContents;

// static
const char DOMView::kViewClassName[] =
    "browser/ui/views/DOMView";

DOMView::DOMView() : initialized_(false) {
  set_focusable(true);
}

DOMView::~DOMView() {
  if (native_view())
    Detach();
}

std::string DOMView::GetClassName() const {
  return kViewClassName;
}

bool DOMView::Init(Profile* profile, SiteInstance* instance) {
  if (initialized_)
    return true;

  initialized_ = true;
  WebContents* web_contents = CreateTabContents(profile, instance);
  dom_contents_.reset(new TabContentsWrapper(web_contents));

  renderer_preferences_util::UpdateFromSystemSettings(
        web_contents->GetMutableRendererPrefs(), profile);

  // Attach the native_view now if the view is already added to Widget.
  if (GetWidget())
    AttachTabContents();

  return true;
}

WebContents* DOMView::CreateTabContents(Profile* profile,
                                        SiteInstance* instance) {
  return WebContents::Create(profile, instance, MSG_ROUTING_NONE, NULL, NULL);
}

void DOMView::LoadURL(const GURL& url) {
  DCHECK(initialized_);
  dom_contents_->web_contents()->GetController().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_START_PAGE,
      std::string());
}

bool DOMView::SkipDefaultKeyEventProcessing(const views::KeyEvent& e) {
  // Don't move the focus to the next view when tab is pressed, we want the
  // key event to be propagated to the render view for doing the tab traversal
  // there.
  return views::FocusManager::IsTabTraversalKeyEvent(e);
}

void DOMView::OnFocus() {
  dom_contents_->web_contents()->Focus();
}

void DOMView::ViewHierarchyChanged(bool is_add, views::View* parent,
                                   views::View* child) {
  // Attach the native_view when this is added to Widget if
  // the native view has not been attached yet and tab_contents_ exists.
  views::NativeViewHost::ViewHierarchyChanged(is_add, parent, child);
  if (is_add && GetWidget() && !native_view() && dom_contents_.get())
    AttachTabContents();
  else if (!is_add && child == this && native_view())
    Detach();
}

void DOMView::AttachTabContents() {
  Attach(dom_contents_->web_contents()->GetNativeView());
}
