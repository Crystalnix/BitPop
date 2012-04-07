// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CERTIFICATE_VIEWER_UI_H_
#define CHROME_BROWSER_UI_WEBUI_CERTIFICATE_VIEWER_UI_H_
#pragma once

#if defined(USE_AURA)
#include "chrome/browser/ui/webui/constrained_html_ui.h"
#else
#include "chrome/browser/ui/webui/html_dialog_ui.h"
#endif

// The WebUI for chrome://view-cert
class CertificateViewerUI
#if defined(USE_AURA)
    : public ConstrainedHtmlUI {
#else
    : public HtmlDialogUI {
#endif
 public:
  explicit CertificateViewerUI(content::WebUI* web_ui);
  virtual ~CertificateViewerUI();

  DISALLOW_COPY_AND_ASSIGN(CertificateViewerUI);
};

#endif  // CHROME_BROWSER_UI_WEBUI_CERTIFICATE_VIEWER_UI_H_
