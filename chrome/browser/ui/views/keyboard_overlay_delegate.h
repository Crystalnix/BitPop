// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_KEYBOARD_OVERLAY_DELEGATE_H_
#define CHROME_BROWSER_UI_VIEWS_KEYBOARD_OVERLAY_DELEGATE_H_

#include "chrome/browser/ui/webui/html_dialog_ui.h"


class HtmlDialogView;

class KeyboardOverlayDelegate : public HtmlDialogUIDelegate {
 public:
  explicit KeyboardOverlayDelegate(const std::wstring& title);

  void set_view(HtmlDialogView* html_view) {
    view_ = html_view;
  }

  HtmlDialogView* view() {
    return view_;
  }

 private:
  virtual ~KeyboardOverlayDelegate();

  // Overridden from HtmlDialogUI::Delegate:
  virtual bool IsDialogModal() const;
  virtual std::wstring GetDialogTitle() const;
  virtual GURL GetDialogContentURL() const;
  virtual void GetWebUIMessageHandlers(
      std::vector<WebUIMessageHandler*>* handlers) const;
  virtual void GetDialogSize(gfx::Size* size) const;
  virtual std::string GetDialogArgs() const;
  virtual void OnDialogClosed(const std::string& json_retval);
  virtual void OnCloseContents(TabContents* source, bool* out_close_dialog);
  virtual bool ShouldShowDialogTitle() const;
  virtual bool HandleContextMenu(const ContextMenuParams& params);

  // The dialog title.
  std::wstring title_;

  // The view associated with this delegate.
  // This class does not own the pointer.
  HtmlDialogView* view_;

  DISALLOW_COPY_AND_ASSIGN(KeyboardOverlayDelegate);
};

#endif  // CHROME_BROWSER_UI_VIEWS_KEYBOARD_OVERLAY_DELEGATE_H_
