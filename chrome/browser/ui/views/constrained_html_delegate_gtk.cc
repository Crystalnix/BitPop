// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/constrained_html_ui.h"

#include "base/property_bag.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/gtk/constrained_window_gtk.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/views/tab_contents/tab_contents_container.h"
#include "chrome/browser/ui/webui/html_dialog_tab_contents_delegate.h"
#include "chrome/browser/ui/webui/html_dialog_ui.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/gtk/gtk_hig_constants.h"
#include "ui/gfx/size.h"
#include "ui/views/widget/native_widget_gtk.h"

using content::WebContents;

// ConstrainedHtmlDelegateGtk works with ConstrainedWindowGtk to present
// a TabContents in a ContraintedHtmlUI.
class ConstrainedHtmlDelegateGtk : public views::NativeWidgetGtk,
                                   public ConstrainedHtmlUIDelegate,
                                   public ConstrainedWindowGtkDelegate,
                                   public HtmlDialogTabContentsDelegate {
 public:
  ConstrainedHtmlDelegateGtk(Profile* profile,
                             HtmlDialogUIDelegate* delegate,
                             HtmlDialogTabContentsDelegate* tab_delegate);
  ~ConstrainedHtmlDelegateGtk();

  void set_window(ConstrainedWindow* window) {
    window_ = window;
  }

  // ConstrainedHtmlUIDelegate interface.
  virtual HtmlDialogUIDelegate* GetHtmlDialogUIDelegate() OVERRIDE;
  virtual void OnDialogCloseFromWebUI() OVERRIDE;
  virtual void ReleaseTabContentsOnDialogClose() OVERRIDE {
    release_tab_on_close_ = true;
  }
  virtual ConstrainedWindow* window() OVERRIDE {
    return window_;
  }
  virtual TabContentsWrapper* tab() OVERRIDE {
    return html_tab_contents_.get();
  }

  // ConstrainedWindowGtkDelegate implementation.
  virtual GtkWidget* GetWidgetRoot() OVERRIDE {
    return GetNativeView();
  }
  virtual GtkWidget* GetFocusWidget() OVERRIDE {
    return html_tab_contents_->web_contents()->GetContentNativeView();
  }
  virtual void DeleteDelegate() OVERRIDE {
    if (!closed_via_webui_)
      html_delegate_->OnDialogClosed("");
    tab_container_->ChangeWebContents(NULL);
  }
  virtual bool GetBackgroundColor(GdkColor* color) OVERRIDE {
    *color = ui::kGdkWhite;
    return true;
  }
  virtual bool ShouldHaveBorderPadding() const OVERRIDE {
    return false;
  }

  // HtmlDialogTabContentsDelegate interface.
  void HandleKeyboardEvent(const NativeWebKeyboardEvent& event) OVERRIDE {}

 private:
  scoped_ptr<TabContentsWrapper> html_tab_contents_;
  TabContentsContainer* tab_container_;
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

ConstrainedHtmlDelegateGtk::ConstrainedHtmlDelegateGtk(
    Profile* profile,
    HtmlDialogUIDelegate* delegate,
    HtmlDialogTabContentsDelegate* tab_delegate)
    : views::NativeWidgetGtk(new views::Widget),
      HtmlDialogTabContentsDelegate(profile),
      tab_container_(NULL),
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
      web_contents->GetPropertyBag(), this);
  web_contents->GetController().LoadURL(delegate->GetDialogContentURL(),
                                        content::Referrer(),
                                        content::PAGE_TRANSITION_START_PAGE,
                                        std::string());

  views::Widget::InitParams params(views::Widget::InitParams::TYPE_CONTROL);
  params.native_widget = this;
  GetWidget()->Init(params);

  tab_container_ = new TabContentsContainer;
  GetWidget()->SetContentsView(tab_container_);
  tab_container_->ChangeWebContents(html_tab_contents_->web_contents());

  gfx::Size dialog_size;
  html_delegate_->GetDialogSize(&dialog_size);
  gtk_widget_set_size_request(GetWidgetRoot(),
                              dialog_size.width(),
                              dialog_size.height());
}

ConstrainedHtmlDelegateGtk::~ConstrainedHtmlDelegateGtk() {
  if (release_tab_on_close_)
    ignore_result(html_tab_contents_.release());
}

HtmlDialogUIDelegate* ConstrainedHtmlDelegateGtk::GetHtmlDialogUIDelegate() {
  return html_delegate_;
}

void ConstrainedHtmlDelegateGtk::OnDialogCloseFromWebUI() {
  closed_via_webui_ = true;
  window_->CloseConstrainedWindow();
}

// static
ConstrainedHtmlUIDelegate* ConstrainedHtmlUI::CreateConstrainedHtmlDialog(
    Profile* profile,
    HtmlDialogUIDelegate* delegate,
    HtmlDialogTabContentsDelegate* tab_delegate,
    TabContentsWrapper* wrapper) {
  ConstrainedHtmlDelegateGtk* constrained_delegate =
      new ConstrainedHtmlDelegateGtk(profile, delegate, tab_delegate);
  ConstrainedWindow* constrained_window =
      new ConstrainedWindowGtk(wrapper, constrained_delegate);
  constrained_delegate->set_window(constrained_window);
  return constrained_delegate;
}
