// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TTS api test for Chrome on ChromeOS.
// browser_tests.exe --gtest_filter="TtsApiTest.*"

chrome.test.runTests([
  function testAllSpeakCallbackFunctionsAreCalled() {
    // In this test, two utterances are queued, and then a third
    // interrupts. The first gets interrupted, the second never gets spoken
    // at all. The test expectations in extension_tts_apitest.cc ensure that
    // the first call to tts.speak keeps going until it's interrupted.
    var callbacks = 0;
    chrome.experimental.tts.speak('text 1', {'enqueue': true}, function() {
        chrome.test.assertEq('Utterance interrupted.',
                             chrome.extension.lastError.message);
        callbacks++;
      });
    chrome.experimental.tts.speak('text 2', {'enqueue': true}, function() {
        chrome.test.assertEq('Utterance removed from queue.',
                             chrome.extension.lastError.message);
        callbacks++;
      });
    chrome.experimental.tts.speak('text 3', {'enqueue': false}, function() {
        chrome.test.assertNoLastError();
        callbacks++;
        if (callbacks == 3) {
          chrome.test.succeed();
        } else {
          chrome.test.fail();
        }
      });
  }
]);
