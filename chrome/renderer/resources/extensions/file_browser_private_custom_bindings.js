// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Custom bindings for the fileBrowserPrivate API.

var fileBrowserPrivateNatives = requireNative('file_browser_private');
var GetLocalFileSystem = fileBrowserPrivateNatives.GetLocalFileSystem;

var fileBrowserNatives = requireNative('file_browser_handler');
var GetExternalFileEntry = fileBrowserNatives.GetExternalFileEntry;

var chromeHidden = requireNative('chrome_hidden').GetChromeHidden();

chromeHidden.registerCustomHook('fileBrowserPrivate', function(bindingsAPI) {
  var apiFunctions = bindingsAPI.apiFunctions;

  apiFunctions.setCustomCallback('requestLocalFileSystem',
                                 function(name, request, response) {
    var fs = null;
    if (response && !response.error)
      fs = GetLocalFileSystem(response.name, response.path);
    if (request.callback)
      request.callback(fs);
    request.callback = null;
  });

  apiFunctions.setCustomCallback('searchGData',
                                 function(name, request, response) {
    if (response && !response.error && response.entries) {
      for (var i = 0; i < response.entries.length; i++)
       response.entries[i] = GetExternalFileEntry(response.entries[i]);
    }

    // So |request.callback| doesn't break if response is not defined.
    if (!response)
      response = {};

    if (request.callback)
      request.callback(response.entries, response.nextFeed);
    request.callback = null;
  });
});
