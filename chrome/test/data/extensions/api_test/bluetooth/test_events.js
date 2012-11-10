// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function testEvents() {
  chrome.test.assertEq(kExpectedValues.length, powerChangedValues.length);
  chrome.test.assertEq(kExpectedValues.length,
      availabilityChangedValues.length);
  chrome.test.assertEq(kExpectedValues.length, discoveringChangedValues.length);

  for (var i = 0; i < kExpectedValues.length; ++i) {
    chrome.test.assertEq(kExpectedValues[i], powerChangedValues[i]);
    chrome.test.assertEq(kExpectedValues[i], availabilityChangedValues[i]);
    chrome.test.assertEq(kExpectedValues[i], discoveringChangedValues[i]);
  }

  chrome.test.succeed();
}

var powerChangedValues = [];
var availabilityChangedValues = [];
var discoveringChangedValues = [];
var kExpectedValues = [true, false];
chrome.experimental.bluetooth.onPowerChanged.addListener(
    function(result) {
      powerChangedValues.push(result);
    });
chrome.experimental.bluetooth.onAvailabilityChanged.addListener(
    function(result) {
      availabilityChangedValues.push(result);
    });
chrome.experimental.bluetooth.onDiscoveringChanged.addListener(
    function(result) {
      discoveringChangedValues.push(result);
    });
chrome.test.sendMessage('ready',
    function(message) {
      chrome.test.runTests([
          testEvents
      ]);
    });
