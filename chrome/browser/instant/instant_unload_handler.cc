// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/instant/instant_unload_handler.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"

using content::WebContents;

// TabContentsDelegate implementation. This owns the TabContents supplied to the
// constructor.
class InstantUnloadHandler::TabContentsDelegateImpl
    : public content::WebContentsDelegate {
 public:
  TabContentsDelegateImpl(InstantUnloadHandler* handler,
                          TabContentsWrapper* tab_contents,
                          int index)
      : handler_(handler),
        tab_contents_(tab_contents),
        index_(index) {
    tab_contents->web_contents()->SetDelegate(this);
  }

  ~TabContentsDelegateImpl() {
  }

  // Releases ownership of the TabContentsWrapper to the caller.
  TabContentsWrapper* ReleaseTab() {
    TabContentsWrapper* tab = tab_contents_.release();
    tab->web_contents()->SetDelegate(NULL);
    return tab;
  }

  // See description above field.
  int index() const { return index_; }

  // content::WebContentsDelegate overrides:
  virtual void WillRunBeforeUnloadConfirm() {
    handler_->Activate(this);
  }

  virtual bool ShouldSuppressDialogs() {
    return true;  // Return true so dialogs are suppressed.
  }

  virtual void CloseContents(WebContents* source) OVERRIDE {
    handler_->Destroy(this);
  }

 private:
  InstantUnloadHandler* handler_;
  scoped_ptr<TabContentsWrapper> tab_contents_;

  // The index |tab_contents_| was originally at. If we add the tab back we add
  // it at this index.
  const int index_;

  DISALLOW_COPY_AND_ASSIGN(TabContentsDelegateImpl);
};

InstantUnloadHandler::InstantUnloadHandler(Browser* browser)
    : browser_(browser) {
}

InstantUnloadHandler::~InstantUnloadHandler() {
}

void InstantUnloadHandler::RunUnloadListenersOrDestroy(TabContentsWrapper* tab,
                                                       int index) {
  if (!tab->web_contents()->NeedToFireBeforeUnload()) {
    // Tab doesn't have any before unload listeners and can be safely deleted.
    delete tab;
    return;
  }

  // Tab has before unload listener. Install a delegate and fire the before
  // unload listener.
  TabContentsDelegateImpl* delegate =
      new TabContentsDelegateImpl(this, tab, index);
  delegates_.push_back(delegate);
  // TODO: decide if we really want false here. false is used for tab closes,
  // and is needed so that the tab correctly closes but it doesn't really match
  // what's logically happening.
  tab->web_contents()->GetRenderViewHost()->FirePageBeforeUnload(false);
}

void InstantUnloadHandler::Activate(TabContentsDelegateImpl* delegate) {
  // Take ownership of the TabContents from the delegate.
  TabContentsWrapper* tab = delegate->ReleaseTab();
  browser::NavigateParams params(browser_, tab);
  params.disposition = NEW_FOREGROUND_TAB;
  params.tabstrip_index = delegate->index();

  // Remove (and delete) the delegate.
  ScopedVector<TabContentsDelegateImpl>::iterator i =
      std::find(delegates_.begin(), delegates_.end(), delegate);
  DCHECK(i != delegates_.end());
  delegates_.erase(i);
  delegate = NULL;

  // Add the tab back in.
  browser::Navigate(&params);
}

void InstantUnloadHandler::Destroy(TabContentsDelegateImpl* delegate) {
  ScopedVector<TabContentsDelegateImpl>::iterator i =
      std::find(delegates_.begin(), delegates_.end(), delegate);
  DCHECK(i != delegates_.end());
  delegates_.erase(i);
}
