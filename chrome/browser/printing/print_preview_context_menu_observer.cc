// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/printing/print_preview_context_menu_observer.h"

#include "base/logging.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/printing/print_preview_tab_controller.h"

PrintPreviewContextMenuObserver::PrintPreviewContextMenuObserver(
    TabContents* tab) : tab_(tab) {
}

PrintPreviewContextMenuObserver::~PrintPreviewContextMenuObserver() {
}

bool PrintPreviewContextMenuObserver::IsPrintPreviewTab() {
  printing::PrintPreviewTabController* controller =
      printing::PrintPreviewTabController::GetInstance();
  if (!controller)
    return false;
  return !!controller->GetPrintPreviewForTab(tab_);
}

bool PrintPreviewContextMenuObserver::IsCommandIdSupported(int command_id) {
  switch (command_id) {
    case IDC_PRINT:
    case IDC_VIEW_SOURCE:
    case IDC_CONTENT_CONTEXT_VIEWFRAMESOURCE:
    case IDC_CONTENT_CONTEXT_VIEWPAGEINFO:
    case IDC_CONTENT_CONTEXT_SEARCHWEBFOR:
      return IsPrintPreviewTab();

    default:
      return false;
  }
}

bool PrintPreviewContextMenuObserver::IsCommandIdEnabled(int command_id) {
  switch (command_id) {
    case IDC_PRINT:
    case IDC_VIEW_SOURCE:
    case IDC_CONTENT_CONTEXT_VIEWFRAMESOURCE:
    case IDC_CONTENT_CONTEXT_VIEWPAGEINFO:
    case IDC_CONTENT_CONTEXT_SEARCHWEBFOR:
      return false;

    default:
      NOTREACHED();
      return true;
  }
}
