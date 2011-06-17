// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The name of the extension to uninstall, from manifest.json.
var EXPECTED_NAME = "Auto-Update Test";

chrome.management.getAll(function(items) {
  for (var i = 0; i < items.length; i++) {
    var item = items[i];
    if (item.name != EXPECTED_NAME) continue;
    var id = item.id;
    chrome.test.assertEq(false, item.mayDisable);
    chrome.management.uninstall(id, function() {
      // Check that the right error occured.
      var expectedError = "Extension " + id + " can not be disabled by user";
      chrome.test.assertEq(expectedError, chrome.extension.lastError.message);
      chrome.test.sendMessage("ready");
    });
  }
});
