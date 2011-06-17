// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains stub implementations of the functions declared in
// browser_dialogs.h that are currently unimplemented in GTK-views.

#include <gtk/gtk.h>

#include "base/logging.h"
#include "chrome/browser/ui/gtk/about_chrome_dialog.h"
#include "chrome/browser/ui/gtk/collected_cookies_gtk.h"
#include "chrome/browser/ui/gtk/edit_search_engine_dialog.h"
#include "chrome/browser/ui/gtk/repost_form_warning_gtk.h"
#include "chrome/browser/ui/gtk/task_manager_gtk.h"
#include "chrome/browser/ui/options/options_window.h"
#include "chrome/browser/ui/views/browser_dialogs.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "chrome/browser/ui/webui/collected_cookies_ui_delegate.h"
#include "views/widget/widget.h"

namespace browser {

void ShowTaskManager() {
  TaskManagerGtk::Show(false);
}

void ShowBackgroundPages() {
  TaskManagerGtk::Show(true);
}

void EditSearchEngine(gfx::NativeWindow parent,
                      const TemplateURL* template_url,
                      EditSearchEngineControllerDelegate* delegate,
                      Profile* profile) {
  new EditSearchEngineDialog(GTK_WINDOW(parent), template_url, NULL, profile);
}

void ShowRepostFormWarningDialog(gfx::NativeWindow parent_window,
                                 TabContents* tab_contents) {
  new RepostFormWarningGtk(GTK_WINDOW(parent_window), tab_contents);
}

void ShowCollectedCookiesDialog(gfx::NativeWindow parent_window,
                                TabContents* tab_contents) {
#if defined(OS_CHROMEOS)
  CollectedCookiesUIDelegate::Show(tab_contents);
#else
  new CollectedCookiesGtk(GTK_WINDOW(parent_window), tab_contents);
#endif
}

}  // namespace browser
