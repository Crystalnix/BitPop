// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/constrained_html_ui.h"

#include "base/property_bag.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/views/constrained_window_views.h"
#include "chrome/browser/ui/views/tab_contents/tab_contents_container.h"
#include "chrome/browser/ui/webui/html_dialog_tab_contents_delegate.h"
#include "chrome/browser/ui/webui/html_dialog_ui.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/size.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget_delegate.h"

using content::WebContents;

class ConstrainedHtmlDelegateViews : public TabContentsContainer,
                                     public ConstrainedHtmlUIDelegate,
                                     public views::WidgetDelegate,
                                     public HtmlDialogTabContentsDelegate {
 public:
  ConstrainedHtmlDelegateViews(Profile* profile,
                               HtmlDialogUIDelegate* delegate,
                               HtmlDialogTabContentsDelegate* tab_delegate);
  ~ConstrainedHtmlDelegateViews();

  void set_window(ConstrainedWindow* window) {
    window_ = window;
  }

  // ConstrainedHtmlUIDelegate interface.
  virtual HtmlDialogUIDelegate* GetHtmlDialogUIDelegate() OVERRIDE;
  virtual void OnDialogCloseFromWebUI() OVERRIDE;
  virtual void ReleaseTabContentsOnDialogClose() OVERRIDE;
  virtual ConstrainedWindow* window() OVERRIDE { return window_; }
  virtual TabContentsWrapper* tab() OVERRIDE {
    return html_tab_contents_.get();
  }

  // views::WidgetDelegate interface.
  virtual views::View* GetInitiallyFocusedView() OVERRIDE {
    return GetFocusView();
  }
  virtual bool CanResize() const OVERRIDE { return true; }
  virtual void WindowClosing() OVERRIDE {
    if (!closed_via_webui_)
      html_delegate_->OnDialogClosed(std::string());
  }
  virtual views::Widget* GetWidget() OVERRIDE {
    return View::GetWidget();
  }
  virtual const views::Widget* GetWidget() const OVERRIDE {
    return View::GetWidget();
  }
  virtual string16 GetWindowTitle() const OVERRIDE {
    return html_delegate_->GetDialogTitle();
  }
  virtual views::View* GetContentsView() OVERRIDE {
    return this;
  }

  // HtmlDialogTabContentsDelegate interface.
  void HandleKeyboardEvent(const NativeWebKeyboardEvent& event) OVERRIDE {}
  virtual void CloseContents(WebContents* source) OVERRIDE {
    window_->CloseConstrainedWindow();
  }

  // Overridden from TabContentsContainer.
  virtual gfx::Size GetPreferredSize() OVERRIDE {
    gfx::Size size;
    html_delegate_->GetDialogSize(&size);
    return size;
  }

  // views::WidgetDelegate interface.
  virtual void ViewHierarchyChanged(bool is_add,
                                    views::View* parent,
                                    views::View* child) OVERRIDE {
    TabContentsContainer::ViewHierarchyChanged(is_add, parent, child);
    if (is_add && child == this) {
      ChangeWebContents(html_tab_contents_->web_contents());
    }
  }

 private:
  scoped_ptr<TabContentsWrapper> html_tab_contents_;
  HtmlDialogUIDelegate* html_delegate_;
  scoped_ptr<HtmlDialogTabContentsDelegate> override_tab_delegate_;

  // The constrained window that owns |this|.  Saved so we can close it later.
  ConstrainedWindow* window_;

  // Was the dialog closed from WebUI (in which case |html_delegate_|'s
  // OnDialogClosed() method has already been called)?
  bool closed_via_webui_;

  // If true, release |tab_| on close instead of destroying it.
  bool release_tab_on_close_;
};

ConstrainedHtmlDelegateViews::ConstrainedHtmlDelegateViews(
    Profile* profile,
    HtmlDialogUIDelegate* delegate,
    HtmlDialogTabContentsDelegate* tab_delegate)
    : HtmlDialogTabContentsDelegate(profile),
      html_delegate_(delegate),
      window_(NULL),
      closed_via_webui_(false),
      release_tab_on_close_(false) {
  CHECK(delegate);
  WebContents* web_contents =
      WebContents::Create(profile, NULL, MSG_ROUTING_NONE, NULL, NULL);
  html_tab_contents_.reset(new TabContentsWrapper(web_contents));
  if (tab_delegate) {
    override_tab_delegate_.reset(tab_delegate);
    web_contents->SetDelegate(tab_delegate);
  } else {
    web_contents->SetDelegate(this);
  }

  // Set |this| as a property so the ConstrainedHtmlUI can retrieve it.
  ConstrainedHtmlUI::GetPropertyAccessor().SetProperty(
      html_tab_contents_->web_contents()->GetPropertyBag(), this);
  web_contents->GetController().LoadURL(delegate->GetDialogContentURL(),
                                        content::Referrer(),
                                        content::PAGE_TRANSITION_START_PAGE,
                                        std::string());
}

ConstrainedHtmlDelegateViews::~ConstrainedHtmlDelegateViews() {
  if (release_tab_on_close_)
    ignore_result(html_tab_contents_.release());
}

HtmlDialogUIDelegate* ConstrainedHtmlDelegateViews::GetHtmlDialogUIDelegate() {
  return html_delegate_;
}

void ConstrainedHtmlDelegateViews::OnDialogCloseFromWebUI() {
  closed_via_webui_ = true;
  window_->CloseConstrainedWindow();
}

void ConstrainedHtmlDelegateViews::ReleaseTabContentsOnDialogClose() {
  release_tab_on_close_ = true;
}

// static
ConstrainedHtmlUIDelegate* ConstrainedHtmlUI::CreateConstrainedHtmlDialog(
    Profile* profile,
    HtmlDialogUIDelegate* delegate,
    HtmlDialogTabContentsDelegate* tab_delegate,
    TabContentsWrapper* container) {
  ConstrainedHtmlDelegateViews* constrained_delegate =
      new ConstrainedHtmlDelegateViews(profile, delegate, tab_delegate);
  ConstrainedWindow* constrained_window =
      new ConstrainedWindowViews(container, constrained_delegate);
  constrained_delegate->set_window(constrained_window);
  return constrained_delegate;
}
