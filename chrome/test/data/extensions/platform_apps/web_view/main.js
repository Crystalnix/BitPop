// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var util = {};

// Creates a <webview> tag in document.body and returns the reference to it.
// It also sets a dummy src. The dummy src is significant because this makes
// sure that the <object> shim is created (asynchronously at this point) for the
// <webview> tag. This makes the <webview> tag ready for add/removeEventListener
// calls.
util.createWebViewTagInDOM = function() {
  var webview = document.createElement('webview');
  webview.style.width = '300px';
  webview.style.height = '200px';
  document.body.appendChild(webview);
  var urlDummy = 'data:text/html,<body>Initial dummy guest</body>';
  webview.setAttribute('src', urlDummy);
  return webview;
};

onload = function() {
  chrome.test.runTests([
    function webView() {
      var webview = document.querySelector('webview');
      // Since we can't currently inspect the page loaded inside the <webview>,
      // the only way we can check that the shim is working is by changing the
      // size and seeing if the shim updates the size of the DOM.
      chrome.test.assertEq(300, webview.offsetWidth);
      chrome.test.assertEq(200, webview.offsetHeight);

      webview.style.width = '310px';
      webview.style.height = '210px';

      chrome.test.assertEq(310, webview.offsetWidth);
      chrome.test.assertEq(210, webview.offsetHeight);

      webview.style.width = '320px';
      webview.style.height = '220px';

      chrome.test.assertEq(320, webview.offsetWidth);
      chrome.test.assertEq(220, webview.offsetHeight);

      var dynamicWebViewTag = document.createElement('webview');
      dynamicWebViewTag.setAttribute('src', 'data:text/html,dynamic browser');
      dynamicWebViewTag.style.width = '330px';
      dynamicWebViewTag.style.height = '230px';
      document.body.appendChild(dynamicWebViewTag);

      // Timeout is necessary to give the mutation observers a chance to fire.
      setTimeout(function() {
        chrome.test.assertEq(330, dynamicWebViewTag.offsetWidth);
        chrome.test.assertEq(230, dynamicWebViewTag.offsetHeight);

        chrome.test.succeed();
      }, 0);
    },

    function webViewApiMethodExistence() {
      var apiMethodsToCheck = [
        'back',
        'canGoBack',
        'canGoForward',
        'forward',
        'getProcessId',
        'go',
        'reload',
        'stop',
        'terminate'
      ];
      var webview = document.createElement('webview');
      webview.addEventListener('loadstop', function(e) {
        for (var i = 0; i < apiMethodsToCheck.length; ++i) {
          chrome.test.assertEq('function',
                               typeof webview[apiMethodsToCheck[i]]);
        }

        // Check contentWindow.
        chrome.test.assertEq('object', typeof webview.contentWindow);
        chrome.test.assertEq('function',
                             typeof webview.contentWindow.postMessage);

        chrome.test.succeed();
      });
      webview.setAttribute('src', 'data:text/html,webview check api');
      document.body.appendChild(webview);
    },

    function webViewEventName() {
      var webview = document.createElement('webview');
      webview.setAttribute('src', 'data:text/html,webview check api');
      document.body.appendChild(webview);

      webview.addEventListener('loadstart', function(evt) {
        chrome.test.assertEq('loadstart', evt.type);
      });

      webview.addEventListener('loadstop', function(evt) {
        chrome.test.assertEq('loadstop', evt.type);
        webview.terminate();
      });

      webview.addEventListener('exit', function(evt) {
        chrome.test.assertEq('exit', evt.type);
        chrome.test.succeed();
      });

      webview.setAttribute('src', 'data:text/html,trigger navigation');
    },

    // This test registers two listeners on an event (loadcommit) and removes
    // the <webview> tag when the first listener fires.
    // Current expected behavior is that the second event listener will still
    // fire without crashing.
    function webviewDestroyOnEventListener() {
      var webview = util.createWebViewTagInDOM();
      var url = 'data:text/html,<body>Destroy test</body>';

      var loadCommitCount = 0;
      function loadCommitCommon(e) {
        chrome.test.assertEq('loadcommit', e.type);
        if (url != e.url)
          return;
        ++loadCommitCount;
        if (loadCommitCount == 1) {
          webview.parentNode.removeChild(webview);
          webview = null;
          setTimeout(function() {
            chrome.test.succeed();
          }, 0);
        } else if (loadCommitCount > 2) {
          chrome.test.fail();
        }
      };

      // The test starts from here, by setting the src to |url|.
      webview.addEventListener('loadcommit', function(e) {
        loadCommitCommon(e);
      });
      webview.addEventListener('loadcommit', function(e) {
        loadCommitCommon(e);
      });
      webview.setAttribute('src', url);
    },

    // This test registers two event listeners on a same event (loadcommit).
    // Each of the listener tries to change some properties on the event param,
    // which should not be possible.
    function cannotMutateEventName() {
      var webview = util.createWebViewTagInDOM();
      var url = 'data:text/html,<body>Two</body>';

      var loadCommitACalled = false;
      var loadCommitBCalled = false;

      var maybeFinishTest = function(e) {
        if (loadCommitACalled && loadCommitBCalled) {
          chrome.test.assertEq('loadcommit', e.type);
          chrome.test.succeed();
        }
      };

      var onLoadCommitA = function(e) {
        if (e.url == url) {
          chrome.test.assertEq('loadcommit', e.type);
          chrome.test.assertTrue(e.isTopLevel);
          chrome.test.assertFalse(loadCommitACalled);
          loadCommitACalled = true;
          // Try mucking with properities inside |e|.
          e.type = 'modified';
          maybeFinishTest(e);
        }
      };
      var onLoadCommitB = function(e) {
        if (e.url == url) {
          chrome.test.assertEq('loadcommit', e.type);
          chrome.test.assertTrue(e.isTopLevel);
          chrome.test.assertFalse(loadCommitBCalled);
          loadCommitBCalled = true;
          // Try mucking with properities inside |e|.
          e.type = 'modified';
          maybeFinishTest(e);
        }
      };

      // The test starts from here, by setting the src to |url|. Event
      // listener registration works because we already have a (dummy) src set
      // on the <webview> tag.
      webview.addEventListener('loadcommit', onLoadCommitA);
      webview.addEventListener('loadcommit', onLoadCommitB);
      webview.setAttribute('src', url);
    },

    // This test verifies that setting the partition attribute after the src has
    // been set raises an exception.
    function partitionRaisesException() {
      var webview = document.createElement('webview');
      webview.setAttribute('src', 'data:text/html,trigger navigation');
      document.body.appendChild(webview);
      setTimeout(function() {
        try {
          webview.partition = 'illegal';
          chrome.test.fail();
        } catch (e) {
          chrome.test.succeed();
        }
      }, 0);
    }
  ]);
};
