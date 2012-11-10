// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_MANAGER_WIN_H_
#define CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_MANAGER_WIN_H_

#include <oleacc.h>

#include "base/win/scoped_comptr.h"
#include "content/browser/accessibility/browser_accessibility_manager.h"

class BrowserAccessibilityWin;

// Manages a tree of BrowserAccessibilityWin objects.
class BrowserAccessibilityManagerWin : public BrowserAccessibilityManager {
 public:
  virtual ~BrowserAccessibilityManagerWin();

  // Get a the default IAccessible for the parent window, does not make a
  // new reference.
  IAccessible* GetParentWindowIAccessible();

  // BrowserAccessibilityManager methods
  virtual void NotifyAccessibilityEvent(int type, BrowserAccessibility* node);

  // Track this object and post a VISIBLE_DATA_CHANGED notification when
  // its container scrolls.
  // TODO(dmazzoni): remove once http://crbug.com/113483 is fixed.
  void TrackScrollingObject(BrowserAccessibilityWin* node);

 private:
  BrowserAccessibilityManagerWin(
      HWND parent_window,
      const content::AccessibilityNodeData& src,
      BrowserAccessibilityDelegate* delegate,
      BrowserAccessibilityFactory* factory);

  // A default IAccessible instance for the parent window.
  base::win::ScopedComPtr<IAccessible> window_iaccessible_;

  // Give BrowserAccessibilityManager::Create access to our constructor.
  friend class BrowserAccessibilityManager;

  // Track the most recent object that has been asked to scroll and
  // post a notification directly on it when it reaches its destination.
  // TODO(dmazzoni): remove once http://crbug.com/113483 is fixed.
  BrowserAccessibilityWin* tracked_scroll_object_;

  DISALLOW_COPY_AND_ASSIGN(BrowserAccessibilityManagerWin);
};

#endif  // CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_MANAGER_WIN_H_
