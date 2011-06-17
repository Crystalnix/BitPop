// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// proxy api test
// browser_tests.exe --gtest_filter=ProxySettingsApiTest.ProxyBypass

function expect(expected, message) {
  return chrome.test.callbackPass(function(value) {
    chrome.test.assertEq(expected, value, message);
  });
}

chrome.test.runTests([
  function setIndividualProxies() {
    var httpProxy = {
      host: "1.1.1.1"
    };
    var httpProxyExpected = {
      scheme: "http",
      host: "1.1.1.1",
      port: 80
    };

    var rules = {
      proxyForHttp: httpProxy,
      bypassList: ["localhost", "::1", "foo.bar", "<local>"]
    };
    var rulesExpected = {
      proxyForHttp: httpProxyExpected,
      bypassList: ["localhost", "::1", "foo.bar", "<local>"]
    };

    var config = { rules: rules, mode: "fixed_servers" };
    var configExpected = { rules: rulesExpected, mode: "fixed_servers" };

    chrome.experimental.proxy.settings.set(
        {'value': config},
        chrome.test.callbackPass());
    chrome.experimental.proxy.settings.get(
        {'incognito': false},
    expect({ 'value': configExpected,
             'levelOfControl': "ControlledByThisExtension" },
           "invalid proxy settings"));
    chrome.experimental.proxy.settings.get(
        {'incognito': true},
    expect({ 'value': configExpected,
             'incognitoSpecific': false,
             'levelOfControl': "ControlledByThisExtension" },
           "invalid proxy settings"));
  }
]);
