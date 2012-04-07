// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/html_dialog_tab_contents_delegate.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "content/public/browser/web_contents.h"

using content::OpenURLParams;
using content::WebContents;

// Incognito profiles are not long-lived, so we always want to store a
// non-incognito profile.
//
// TODO(akalin): Should we make it so that we have a default incognito
// profile that's long-lived?  Of course, we'd still have to clear it out
// when all incognito browsers close.
HtmlDialogTabContentsDelegate::HtmlDialogTabContentsDelegate(Profile* profile)
    : profile_(profile) {}

HtmlDialogTabContentsDelegate::~HtmlDialogTabContentsDelegate() {}

Profile* HtmlDialogTabContentsDelegate::profile() const { return profile_; }

void HtmlDialogTabContentsDelegate::Detach() {
  profile_ = NULL;
}

WebContents* HtmlDialogTabContentsDelegate::OpenURLFromTab(
    WebContents* source, const OpenURLParams& params) {
  WebContents* new_contents = NULL;
  StaticOpenURLFromTab(profile_, source, params, &new_contents);
  return new_contents;
}

// static
Browser* HtmlDialogTabContentsDelegate::StaticOpenURLFromTab(
    Profile* profile, WebContents* source, const OpenURLParams& params,
    WebContents** out_new_contents) {
  if (!profile)
    return NULL;

  // Specify a NULL browser for navigation. This will cause Navigate()
  // to find a browser matching params.profile or create a new one.
  Browser* browser = NULL;
  browser::NavigateParams nav_params(browser, params.url, params.transition);
  nav_params.profile = profile;
  nav_params.referrer = params.referrer;
  if (source && source->IsCrashed() &&
      params.disposition == CURRENT_TAB &&
      params.transition == content::PAGE_TRANSITION_LINK) {
    nav_params.disposition = NEW_FOREGROUND_TAB;
  } else {
    nav_params.disposition = params.disposition;
  }
  nav_params.window_action = browser::NavigateParams::SHOW_WINDOW;
  nav_params.user_gesture = true;
  browser::Navigate(&nav_params);
  *out_new_contents = nav_params.target_contents ?
      nav_params.target_contents->web_contents() : NULL;
  return nav_params.browser;
}

void HtmlDialogTabContentsDelegate::AddNewContents(
    WebContents* source, WebContents* new_contents,
    WindowOpenDisposition disposition, const gfx::Rect& initial_pos,
    bool user_gesture) {
  StaticAddNewContents(profile_, source, new_contents, disposition,
                       initial_pos, user_gesture);
}

// static
Browser* HtmlDialogTabContentsDelegate::StaticAddNewContents(
    Profile* profile,
    WebContents* source,
    WebContents* new_contents,
    WindowOpenDisposition disposition,
    const gfx::Rect& initial_pos,
    bool user_gesture) {
  if (!profile)
    return NULL;

  // Specify a NULL browser for navigation. This will cause Navigate()
  // to find a browser matching params.profile or create a new one.
  Browser* browser = NULL;

  TabContentsWrapper* wrapper = new TabContentsWrapper(new_contents);
  browser::NavigateParams params(browser, wrapper);
  params.profile = profile;
  // TODO(pinkerton): no way to get a wrapper for this.
  // params.source_contents = source;
  params.disposition = disposition;
  params.window_bounds = initial_pos;
  params.window_action = browser::NavigateParams::SHOW_WINDOW;
  params.user_gesture = true;
  browser::Navigate(&params);

  return params.browser;
}

bool HtmlDialogTabContentsDelegate::IsPopupOrPanel(
    const WebContents* source) const {
  // This needs to return true so that we are allowed to be resized by our
  // contents.
  return true;
}

bool HtmlDialogTabContentsDelegate::ShouldAddNavigationToHistory(
    const history::HistoryAddPageArgs& add_page_args,
    content::NavigationType navigation_type) {
  return false;
}
