// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var mediaGalleries = chrome.experimental.mediaGalleries;

var mediaFileSystemsDirectoryErrorCallback = function(err) {
  chrome.test.fail("Couldn't read from directory: " + err);
};

var mediaFileSystemsListCallback = function(results) {
  // There should be a "Pictures" directory on all desktop platforms.
  var expectedFileSystems = 1;
  // But not on Android and ChromeOS.
  if (/Android/.test(navigator.userAgent) || /CrOS/.test(navigator.userAgent))
    expectedFileSystems = 0;
  chrome.test.assertEq(expectedFileSystems, results.length);
  if (expectedFileSystems) {
    var dir_reader = results[0].root.createReader();
    dir_reader.readEntries(chrome.test.callbackPass(),
                           mediaFileSystemsDirectoryErrorCallback);
  }
};

chrome.test.runTests([
  function getGalleries() {
    mediaGalleries.getMediaFileSystems(
        chrome.test.callbackPass(mediaFileSystemsListCallback));
  },
]);
