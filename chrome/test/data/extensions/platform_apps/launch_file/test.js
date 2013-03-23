// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

chrome.app.runtime.onLaunched.addListener(function (launchData) {
  // Test that there is a launchData.intent, it is set up proerly, and that the
  // FileEntry in launchData.intent.data can be read.
  chrome.test.runTests([
    function testFileHandler() {
      // TODO(benwells): remove once we no longer support intents.
      chrome.test.assertFalse(!launchData, "No launchData");
      chrome.test.assertFalse(!launchData.intent, "No launchData.intent");
      chrome.test.assertEq(launchData.intent.action,
          "http://webintents.org/view");
      chrome.test.assertEq(launchData.intent.type,
          "chrome-extension://fileentry");
      chrome.test.assertFalse(!launchData.intent.data,
          "No launchData.intent.data");

      chrome.test.assertEq(typeof launchData.id, 'string',
          "launchData.id not received");
      chrome.test.assertEq(launchData.items.length, 1);

      launchData.items[0].entry.file(function(file) {
        var reader = new FileReader();
        reader.onloadend = function(e) {
          chrome.test.assertEq(
              reader.result.indexOf("This is a test. Word."), 0);
          chrome.test.succeed();
        };
        reader.onerror = function(e) {
          chrome.test.fail("Error reading file contents.");
        };
        reader.readAsText(file);
      });
    }
  ]);
});
