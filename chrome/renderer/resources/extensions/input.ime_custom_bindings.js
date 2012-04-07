// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Custom bindings for the input ime API. Only injected into the
// v8 contexts for extensions which have permission for the API.

(function() {

native function GetChromeHidden();

GetChromeHidden().registerCustomHook('input.ime', function() {
  chrome.input.ime.onKeyEvent.dispatch = function(engineID, keyData) {
    var args = Array.prototype.slice.call(arguments);
    if (this.validate_) {
      var validationErrors = this.validate_(args);
      if (validationErrors) {
        chrome.input.ime.eventHandled(requestId, false);
        return validationErrors;
      }
    }
    if (this.listeners_.length > 1) {
      console.error("Too many listeners for 'onKeyEvent': " + e.stack);
      chrome.input.ime.eventHandled(requestId, false);
      return;
    }
    for (var i = 0; i < this.listeners_.length; i++) {
      try {
        var requestId = keyData.requestId;
        var result = this.listeners_[i].apply(null, args);
        chrome.input.ime.eventHandled(requestId, result);
      } catch (e) {
        console.error("Error in event handler for 'onKeyEvent': " + e.stack);
        chrome.input.ime.eventHandled(requestId, false);
      }
    }
  };
});

})();
