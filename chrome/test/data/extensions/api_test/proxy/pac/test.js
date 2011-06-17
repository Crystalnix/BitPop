// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// proxy api test
// browser_tests.exe --gtest_filter=ProxySettingsApiTest.ProxyPacScript

chrome.test.runTests([
  function setAutoSettings() {
    var pacScriptObject = {
      url: "http://wpad/windows.pac"
    };
    var config = {
      mode: "pac_script",
      pacScript: pacScriptObject
    };
    chrome.experimental.proxy.settings.set(
        {'value': config},
        chrome.test.callbackPass());
  }
]);
