// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Custom bindings for the experimental.socket API.

(function() {
  native function GetChromeHidden();
  native function GetNextSocketEventId();

  var chromeHidden = GetChromeHidden();

  chromeHidden.registerCustomHook('experimental.socket', function(api) {
      var apiFunctions = api.apiFunctions;
      var sendRequest = api.sendRequest;

      apiFunctions.setHandleRequest("experimental.socket.create", function() {
          var args = arguments;
          if (args.length > 3 && args[3] && args[3].onEvent) {
            var id = GetNextSocketEventId();
            args[3].srcId = id;
            chromeHidden.socket.handlers[id] = args[3].onEvent;
          }
          sendRequest(this.name, args, this.definition.parameters);
          return id;
        });

      // Set up events.
      chromeHidden.socket = {};
      chromeHidden.socket.handlers = {};
      chrome.experimental.socket.onEvent.addListener(function(event) {
          var eventHandler = chromeHidden.socket.handlers[event.srcId];
          if (eventHandler) {
            switch (event.type) {
              case "writeComplete":
              case "connectComplete":
                eventHandler({
                 type: event.type,
                        resultCode: event.resultCode,
                        });
              break;
              case "dataRead":
                eventHandler({
                 type: event.type,
                        resultCode: event.resultCode,
                        data: event.data,
                        });
                break;
              default:
                console.error("Unexpected SocketEvent, type " + event.type);
              break;
            }
            if (event.isFinalEvent) {
              delete chromeHidden.socket.handlers[event.srcId];
            }
          }
        });
    });

})();
