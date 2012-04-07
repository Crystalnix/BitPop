// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This script contains unprivileged javascript APIs related to chrome
// extensions.  It is loaded by any extension-related context, such as content
// scripts or background pages.
// See user_script_slave.cc for script that is loaded by content scripts only.
// TODO(mpcomplete): we also load this in regular web pages, but don't need to.

var chrome = chrome || {};
(function () {
  native function OpenChannelToExtension(sourceId, targetId, name);
  native function CloseChannel(portId, notifyBrowser);
  native function PortAddRef(portId);
  native function PortRelease(portId);
  native function PostMessage(portId, msg);
  native function GetChromeHidden();
  native function GetL10nMessage();
  native function Print();
  native function BindToGC();

  var chromeHidden = GetChromeHidden();
  var manifestVersion;

  // The reserved channel name for the sendRequest API.
  chromeHidden.kRequestChannel = "chrome.extension.sendRequest";

  // Map of port IDs to port object.
  var ports = {};

  // Map of port IDs to chromeHidden.onUnload listeners. Keep track of these
  // to free the onUnload listeners when ports are closed.
  var portReleasers = {};

  // Change even to odd and vice versa, to get the other side of a given
  // channel.
  function getOppositePortId(portId) { return portId ^ 1; }

  // Port object.  Represents a connection to another script context through
  // which messages can be passed.
  chrome.Port = function(portId, opt_name) {
    this.portId_ = portId;
    this.name = opt_name;
    this.onDisconnect = new chrome.Event();
    this.onMessage = new chrome.Event();
  };

  chromeHidden.Port = {};

  // Returns true if the specified port id is in this context. This is used by
  // the C++ to avoid creating the javascript message for all the contexts that
  // don't care about a particular message.
  chromeHidden.Port.hasPort = function(portId) {
    return portId in ports;
  };

  // Hidden port creation function.  We don't want to expose an API that lets
  // people add arbitrary port IDs to the port list.
  chromeHidden.Port.createPort = function(portId, opt_name) {
    if (ports[portId]) {
      throw new Error("Port '" + portId + "' already exists.");
    }
    var port = new chrome.Port(portId, opt_name);
    ports[portId] = port;
    portReleasers[portId] = PortRelease.bind(this, portId);
    chromeHidden.onUnload.addListener(portReleasers[portId]);

    PortAddRef(portId);
    return port;
  };

  // Called by native code when a channel has been opened to this context.
  chromeHidden.Port.dispatchOnConnect = function(portId, channelName, tab,
                                                 sourceExtensionId,
                                                 targetExtensionId) {
    // Only create a new Port if someone is actually listening for a connection.
    // In addition to being an optimization, this also fixes a bug where if 2
    // channels were opened to and from the same process, closing one would
    // close both.
    if (targetExtensionId != chromeHidden.extensionId)
      return;  // not for us
    if (ports[getOppositePortId(portId)])
      return;  // this channel was opened by us, so ignore it

    // Determine whether this is coming from another extension, so we can use
    // the right event.
    var isExternal = sourceExtensionId != chromeHidden.extensionId;

    if (tab)
      tab = chromeHidden.JSON.parse(tab);
    var sender = {tab: tab, id: sourceExtensionId};

    // Special case for sendRequest/onRequest.
    if (channelName == chromeHidden.kRequestChannel) {
      var requestEvent = (isExternal ?
          chrome.extension.onRequestExternal : chrome.extension.onRequest);
      if (requestEvent.hasListeners()) {
        var port = chromeHidden.Port.createPort(portId, channelName);
        port.onMessage.addListener(function(request) {
          var responseCallback = function(response) {
            if (port) {
              port.postMessage(response);
              port = null;
            } else {
              // We nulled out port when sending the response, and now the page
              // is trying to send another response for the same request.
              var errorMsg =
                  "Cannot send a response more than once per " +
                  "chrome.extension.onRequest listener per document (message " +
                  "was sent by extension " + sourceExtensionId;
              if (sourceExtensionId != targetExtensionId) {
                errorMsg += " for extension " + targetExtensionId;
              }
              errorMsg += ").";
              chrome.extension.lastError = {"message": errorMsg};
              console.error("Could not send response: " + errorMsg);
            }
          };
          // In case the extension never invokes the responseCallback, and also
          // doesn't keep a reference to it, we need to clean up the port. Do
          // so by attaching to the garbage collection of the responseCallback
          // using some native hackery.
          BindToGC(responseCallback, function() {
            if (port) {
              port.disconnect();
              port = null;
            }
          });
          requestEvent.dispatch(request, sender, responseCallback);
        });
      }
      return;
    }

    var connectEvent = (isExternal ?
        chrome.extension.onConnectExternal : chrome.extension.onConnect);
    if (connectEvent.hasListeners()) {
      var port = chromeHidden.Port.createPort(portId, channelName);
      port.sender = sender;
      if (manifestVersion < 2)
        port.tab = port.sender.tab;

      connectEvent.dispatch(port);
    }
  };

  // Called by native code when a channel has been closed.
  chromeHidden.Port.dispatchOnDisconnect = function(
      portId, connectionInvalid) {
    var port = ports[portId];
    if (port) {
      // Update the renderer's port bookkeeping, without notifying the browser.
      CloseChannel(portId, false);
      if (connectionInvalid) {
        var errorMsg =
            "Could not establish connection. Receiving end does not exist.";
        chrome.extension.lastError = {"message": errorMsg};
        console.error("Port error: " + errorMsg);
      }
      try {
        port.onDisconnect.dispatch(port);
      } finally {
        port.destroy_();
        delete chrome.extension.lastError;
      }
    }
  };

  // Called by native code when a message has been sent to the given port.
  chromeHidden.Port.dispatchOnMessage = function(msg, portId) {
    var port = ports[portId];
    if (port) {
      if (msg) {
        msg = chromeHidden.JSON.parse(msg);
      }
      port.onMessage.dispatch(msg, port);
    }
  };

  // Sends a message asynchronously to the context on the other end of this
  // port.
  chrome.Port.prototype.postMessage = function(msg) {
    // JSON.stringify doesn't support a root object which is undefined.
    if (msg === undefined)
      msg = null;
    PostMessage(this.portId_, chromeHidden.JSON.stringify(msg));
  };

  // Disconnects the port from the other end.
  chrome.Port.prototype.disconnect = function() {
    CloseChannel(this.portId_, true);
    this.destroy_();
  };

  chrome.Port.prototype.destroy_ = function() {
    var portId = this.portId_;

    this.onDisconnect.destroy_();
    this.onMessage.destroy_();

    PortRelease(portId);
    chromeHidden.onUnload.removeListener(portReleasers[portId]);

    delete ports[portId];
    delete portReleasers[portId];
  };

  // This function is called on context initialization for both content scripts
  // and extension contexts.
  chromeHidden.onLoad.addListener(function(extensionId,
                                           isExtensionProcess,
                                           inIncognitoContext,
                                           tempManifestVersion) {
    chromeHidden.extensionId = extensionId;
    manifestVersion = tempManifestVersion;

    chrome.extension = chrome.extension || {};

    if (manifestVersion < 2) {
      chrome.self = chrome.extension;
      chrome.extension.inIncognitoTab = inIncognitoContext;
    }

    chrome.extension.inIncognitoContext = inIncognitoContext;

    // Events for when a message channel is opened to our extension.
    chrome.extension.onConnect = new chrome.Event();
    chrome.extension.onRequest = new chrome.Event();

    if (isExtensionProcess) {
      chrome.extension.onConnectExternal = new chrome.Event();
      chrome.extension.onRequestExternal = new chrome.Event();
    }

    // Opens a message channel to the given target extension, or the current one
    // if unspecified.  Returns a Port for message passing.
    chrome.extension.connect = function(targetId_opt, connectInfo_opt) {
      var name = "";
      var targetId = extensionId;
      var nextArg = 0;
      if (typeof(arguments[nextArg]) == "string")
        targetId = arguments[nextArg++];
      if (typeof(arguments[nextArg]) == "object")
        name = arguments[nextArg++].name || name;
      if (nextArg != arguments.length)
        throw new Error("Invalid arguments to connect.");

      var portId = OpenChannelToExtension(extensionId, targetId, name);
      if (portId >= 0)
        return chromeHidden.Port.createPort(portId, name);
      throw new Error("Error connecting to extension '" + targetId + "'");
    };

    chrome.extension.sendRequest =
        function(targetId_opt, request, responseCallback_opt) {
      var targetId = extensionId;
      var responseCallback = null;
      var lastArg = arguments.length - 1;
      if (typeof(arguments[lastArg]) == "function")
        responseCallback = arguments[lastArg--];
      request = arguments[lastArg--];
      if (lastArg >= 0 && typeof(arguments[lastArg]) == "string")
        targetId = arguments[lastArg--];
      if (lastArg != -1)
        throw new Error("Invalid arguments to sendRequest.");

      var port = chrome.extension.connect(targetId,
                                          {name: chromeHidden.kRequestChannel});
      port.postMessage(request);
      port.onDisconnect.addListener(function() {
        // For onDisconnects, we only notify the callback if there was an error
        try {
          if (chrome.extension.lastError && responseCallback)
            responseCallback();
        } finally {
          port = null;
        }
      });
      port.onMessage.addListener(function(response) {
        try {
          if (responseCallback)
            responseCallback(response);
        } finally {
          port.disconnect();
          port = null;
        }
      });
    };

    // Returns a resource URL that can be used to fetch a resource from this
    // extension.
    chrome.extension.getURL = function(path) {
      path = String(path);
      if (!path.length || path[0] != "/")
        path = "/" + path;
      return "chrome-extension://" + extensionId + path;
    };

    chrome.i18n = chrome.i18n || {};
    chrome.i18n.getMessage = function(message_name, placeholders) {
      return GetL10nMessage(message_name, placeholders, extensionId);
    };
  });

})();
