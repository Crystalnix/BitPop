// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/certificate_viewer.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/webui/certificate_viewer_webui.h"
#include "chrome/browser/ui/webui/web_ui_browsertest.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/test_web_dialog_observer.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "net/base/test_certificate_data.h"
#include "net/base/x509_certificate.h"

// Test framework for chrome/test/data/webui/certificate_viewer_dialog_test.js.
class CertificateViewerUITest : public WebUIBrowserTest {
 public:
  CertificateViewerUITest();
  virtual ~CertificateViewerUITest();

 protected:
  void ShowCertificateViewer();
};

void CertificateViewerUITest::ShowCertificateViewer() {
  scoped_refptr<net::X509Certificate> google_cert(
      net::X509Certificate::CreateFromBytes(
          reinterpret_cast<const char*>(google_der), sizeof(google_der)));

  ASSERT_TRUE(browser());
  ASSERT_TRUE(browser()->window());

  TestWebDialogObserver dialog_observer(this);
  CertificateViewerDialog* dialog = new CertificateViewerDialog(
      google_cert);
  dialog->AddObserver(&dialog_observer);
  dialog->Show(chrome::GetActiveWebContents(browser()),
               browser()->window()->GetNativeWindow());
  dialog->RemoveObserver(&dialog_observer);
  content::WebUI* webui = dialog_observer.GetWebUI();
  webui->GetWebContents()->GetRenderViewHost()->SetWebUIProperty(
      "expectedUrl", chrome::kChromeUICertificateViewerURL);
  SetWebUIInstance(webui);
}
