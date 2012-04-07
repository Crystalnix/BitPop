// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_TAB_MODAL_CONFIRM_DIALOG_MAC_H_
#define CHROME_BROWSER_UI_COCOA_TAB_MODAL_CONFIRM_DIALOG_MAC_H_
#pragma once

#import <Cocoa/Cocoa.h>

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/cocoa/constrained_window_mac.h"

class TabContentsWrapper;
class TabModalConfirmDialogDelegate;

// Displays a tab-modal dialog, i.e. a dialog that will block the current page
// but still allow the user to switch to a different page.
// To display the dialog, allocate this object on the heap. It will open the
// dialog from its constructor and then delete itself when the user dismisses
// the dialog.
class TabModalConfirmDialogMac
    : public ConstrainedWindowMacDelegateSystemSheet {
 public:
  TabModalConfirmDialogMac(TabModalConfirmDialogDelegate* delegate,
                           TabContentsWrapper* wrapper);

  // ConstrainedWindowDelegateMacSystemSheet methods:
  virtual void DeleteDelegate() OVERRIDE;

 private:
  virtual ~TabModalConfirmDialogMac();

  scoped_ptr<TabModalConfirmDialogDelegate> delegate_;

  DISALLOW_COPY_AND_ASSIGN(TabModalConfirmDialogMac);
};

#endif  // CHROME_BROWSER_UI_COCOA_TAB_MODAL_CONFIRM_DIALOG_MAC_H_
