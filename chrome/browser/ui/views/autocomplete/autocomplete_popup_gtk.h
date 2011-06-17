// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AUTOCOMPLETE_AUTOCOMPLETE_POPUP_GTK_H_
#define CHROME_BROWSER_UI_VIEWS_AUTOCOMPLETE_AUTOCOMPLETE_POPUP_GTK_H_
#pragma once

#include "base/memory/weak_ptr.h"
#include "views/widget/widget_gtk.h"

class AutocompletePopupGtk
    : public views::WidgetGtk,
      public base::SupportsWeakPtr<AutocompletePopupGtk> {
 public:
  // Creates the popup and shows it. |edit_view| is the edit that created us.
  AutocompletePopupGtk();
  virtual ~AutocompletePopupGtk();

  // Returns the window the popup should be relative to. |edit_native_view| is
  // the native view of the autocomplete edit.
  gfx::NativeView GetRelativeWindowForPopup(
      gfx::NativeView edit_native_view) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(AutocompletePopupGtk);
};

#endif  // CHROME_BROWSER_UI_VIEWS_AUTOCOMPLETE_AUTOCOMPLETE_POPUP_GTK_H_
