// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/certificate_viewer.h"

#include <windows.h>
#include <cryptuiapi.h>
#pragma comment(lib, "cryptui.lib")

#include "base/logging.h"
#include "net/base/x509_certificate.h"

void ShowCertificateViewer(gfx::NativeWindow parent,
                           net::X509Certificate* cert) {
  // Create a new cert context and store containing just the certificate
  // and its intermediate certificates.
  PCCERT_CONTEXT cert_list = cert->CreateOSCertChainForCert();
  CHECK(cert_list);

  CRYPTUI_VIEWCERTIFICATE_STRUCT view_info = { 0 };
  view_info.dwSize = sizeof(view_info);
  // We set our parent to the tab window. This makes the cert dialog created
  // in CryptUIDlgViewCertificate modal to the browser.
  view_info.hwndParent = parent;
  view_info.dwFlags = CRYPTUI_DISABLE_EDITPROPERTIES |
                      CRYPTUI_DISABLE_ADDTOSTORE;
  view_info.pCertContext = cert_list;
  HCERTSTORE cert_store = cert_list->hCertStore;
  view_info.cStores = 1;
  view_info.rghStores = &cert_store;
  BOOL properties_changed;

  // This next call blocks but keeps processing windows messages, making it
  // modal to the browser window.
  BOOL rv = ::CryptUIDlgViewCertificate(&view_info, &properties_changed);

  CertFreeCertificateContext(cert_list);
}
