// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TAB_CONTENTS_NATIVE_TAB_CONTENTS_CONTAINER_WIN_H_
#define CHROME_BROWSER_UI_VIEWS_TAB_CONTENTS_NATIVE_TAB_CONTENTS_CONTAINER_WIN_H_
#pragma once

#include "chrome/browser/ui/views/tab_contents/native_tab_contents_container.h"
#include "ui/views/controls/native/native_view_host.h"

class NativeTabContentsContainerWin : public NativeTabContentsContainer,
                                      public views::NativeViewHost {
 public:
  explicit NativeTabContentsContainerWin(TabContentsContainer* container);
  virtual ~NativeTabContentsContainerWin();

  // Overridden from NativeTabContentsContainer:
  virtual void AttachContents(content::WebContents* contents) OVERRIDE;
  virtual void DetachContents(content::WebContents* contents) OVERRIDE;
  virtual void SetFastResize(bool fast_resize) OVERRIDE;
  virtual bool GetFastResize() const OVERRIDE;
  virtual bool FastResizeAtLastLayout() const OVERRIDE;
  virtual void RenderViewHostChanged(RenderViewHost* old_host,
                                     RenderViewHost* new_host) OVERRIDE;
  virtual void WebContentsFocused(content::WebContents* contents) OVERRIDE;
  virtual views::View* GetView() OVERRIDE;

  // Overridden from views::View:
  virtual bool SkipDefaultKeyEventProcessing(const views::KeyEvent& e) OVERRIDE;
  virtual bool IsFocusable() const OVERRIDE;
  virtual void OnFocus() OVERRIDE;
  virtual void RequestFocus() OVERRIDE;
  virtual void AboutToRequestFocusFromTabTraversal(bool reverse) OVERRIDE;
  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE;
  virtual gfx::NativeViewAccessible GetNativeViewAccessible();

 private:
  TabContentsContainer* container_;

  DISALLOW_COPY_AND_ASSIGN(NativeTabContentsContainerWin);
};

#endif  // CHROME_BROWSER_UI_VIEWS_TAB_CONTENTS_NATIVE_TAB_CONTENTS_CONTAINER_WIN_H_
