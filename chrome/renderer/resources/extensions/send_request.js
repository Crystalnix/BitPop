// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var chromeHidden = requireNative('chrome_hidden').GetChromeHidden();
var lastError = require('lastError');
var natives = requireNative('sendRequest');
var validate = require('schemaUtils').validate;

// Callback handling.
var requests = [];
chromeHidden.handleResponse = function(requestId, name,
                                       success, responseList, error) {
  try {
    var request = requests[requestId];
    if (success) {
      lastError.clear();
    } else {
      if (!error) {
        error = "Unknown error.";
      }
      console.error("Error during " + name + ": " + error);
      lastError.set(error);
    }

    if (request.customCallback) {
      var customCallbackArgs = [name, request].concat(responseList);
      request.customCallback.apply(request, customCallbackArgs);
    }

    if (request.callback) {
      // Validate callback in debug only -- and only when the
      // caller has provided a callback. Implementations of api
      // calls my not return data if they observe the caller
      // has not provided a callback.
      if (chromeHidden.validateCallbacks && !error) {
        try {
          if (!request.callbackSchema.parameters) {
            throw new Error("No callback schemas defined");
          }

          validate(responseList, request.callbackSchema.parameters);
        } catch (exception) {
          return "Callback validation error during " + name + " -- " +
                 exception.stack;
        }
      }

      request.callback.apply(request, responseList);
    }
  } finally {
    delete requests[requestId];
    lastError.clear();
  }

  return undefined;
};

function prepareRequest(args, argSchemas) {
  var request = {};
  var argCount = args.length;

  // Look for callback param.
  if (argSchemas.length > 0 &&
      argSchemas[argSchemas.length - 1].type == "function") {
    request.callback = args[args.length - 1];
    request.callbackSchema = argSchemas[argSchemas.length - 1];
    --argCount;
  }

  request.args = [];
  for (var k = 0; k < argCount; k++) {
    request.args[k] = args[k];
  }

  return request;
}

// Send an API request and optionally register a callback.
// |opt_args| is an object with optional parameters as follows:
// - noStringify: true if we should not stringify the request arguments.
// - customCallback: a callback that should be called instead of the standard
//   callback.
// - nativeFunction: the v8 native function to handle the request, or
//   StartRequest if missing.
// - forIOThread: true if this function should be handled on the browser IO
//   thread.
function sendRequest(functionName, args, argSchemas, opt_args) {
  if (!opt_args)
    opt_args = {};
  var request = prepareRequest(args, argSchemas);
  if (opt_args.customCallback) {
    request.customCallback = opt_args.customCallback;
  }
  // JSON.stringify doesn't support a root object which is undefined.
  if (request.args === undefined)
    request.args = null;

  // TODO(asargent) - convert all optional native functions to accept raw
  // v8 values instead of expecting JSON strings.
  var doStringify = false;
  if (opt_args.nativeFunction && !opt_args.noStringify)
    doStringify = true;
  var requestArgs = doStringify ?
      chromeHidden.JSON.stringify(request.args) : request.args;
  var nativeFunction = opt_args.nativeFunction || natives.StartRequest;

  var requestId = natives.GetNextRequestId();
  request.id = requestId;
  requests[requestId] = request;
  var hasCallback =
      (request.callback || opt_args.customCallback) ? true : false;
  return nativeFunction(functionName, requestArgs, requestId, hasCallback,
                        opt_args.forIOThread);
}

exports.sendRequest = sendRequest;
