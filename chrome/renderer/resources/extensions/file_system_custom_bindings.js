// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Custom bindings for the fileSystem API.

var chromeHidden = requireNative('chrome_hidden').GetChromeHidden();
var fileSystemNatives = requireNative('file_system_natives');
var GetIsolatedFileSystem = fileSystemNatives.GetIsolatedFileSystem;
var lastError = require('lastError');

chromeHidden.registerCustomHook('fileSystem', function(bindingsAPI) {
  var apiFunctions = bindingsAPI.apiFunctions;
  function bindFileEntryFunction(functionName) {
    apiFunctions.setUpdateArgumentsPostValidate(
        functionName, function(fileEntry, callback) {
      var fileSystemName = fileEntry.filesystem.name;
      var relativePath = fileEntry.fullPath.slice(1);
      return [fileSystemName, relativePath, callback];
    });
  }
  ['getDisplayPath', 'getWritableEntry', 'isWritableEntry']
      .forEach(bindFileEntryFunction);

  function bindFileEntryCallback(functionName) {
    apiFunctions.setCustomCallback(functionName,
        function(name, request, response) {
      if (request.callback && response) {
        var callback = request.callback;
        request.callback = null;

        var fileSystemId = response.fileSystemId;
        var baseName = response.baseName;
        var fs = GetIsolatedFileSystem(fileSystemId);

        try {
          fs.root.getFile(baseName, {}, function(fileEntry) {
            callback(fileEntry);
          }, function(fileError) {
            lastError.set('Error getting fileEntry, code: ' + fileError.code);
            callback();
          });
        } catch (e) {
          lastError.set('Error in event handler for onLaunched: ' + e.stack);
          callback();
        }
      }
    });
  }
  ['getWritableEntry', 'chooseEntry'].forEach(bindFileEntryCallback);

  // TODO(benwells): Remove these deprecated versions of the functions.
  chrome.fileSystem.getWritableFileEntry = function() {
    console.log("chrome.fileSystem.getWritableFileEntry is deprecated");
    console.log("Please use chrome.fileSystem.getWritableEntry instead");
    chrome.fileSystem.getWritableEntry.apply(this, arguments);
  };

  chrome.fileSystem.isWritableFileEntry = function() {
    console.log("chrome.fileSystem.isWritableFileEntry is deprecated");
    console.log("Please use chrome.fileSystem.isWritableEntry instead");
    chrome.fileSystem.isWritableEntry.apply(this, arguments);
  };

  chrome.fileSystem.chooseFile = function() {
    console.log("chrome.fileSystem.chooseFile is deprecated");
    console.log("Please use chrome.fileSystem.chooseEntry instead");
    chrome.fileSystem.chooseEntry.apply(this, arguments);
  };
});
