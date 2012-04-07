// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * TestFixture for certificate manager WebUI testing.
 * @extends {testing.Test}
 * @constructor
 **/
function CertificateManagerWebUITest() {}

CertificateManagerWebUITest.prototype = {
  __proto__: testing.Test.prototype,

  /**
   * Browse to the certificate manager.
   **/
  browsePreload: 'chrome://settings/certificates',
};

// Mac and Windows go to native certificate manager.
GEN('#if defined(OS_MACOSX) || defined(OS_WIN)');
GEN('#define MAYBE_testOpenCertificateManager ' +
    'DISABLED_testOpenCertificateManager');
GEN('#else');
GEN('#define MAYBE_testOpenCertificateManager ' +
    'testOpenCertificateManager');
GEN('#endif  // defined(OS_MACOSX) || defined(OS_WIN)');
// Test opening the certificate manager has correct location.
TEST_F('CertificateManagerWebUITest',
       'MAYBE_testOpenCertificateManager', function() {
         assertEquals(this.browsePreload, document.location.href);
       });
