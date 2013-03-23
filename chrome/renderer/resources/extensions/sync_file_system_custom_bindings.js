// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Custom bindings for the syncFileSystem API.

var chromeHidden = requireNative('chrome_hidden').GetChromeHidden();
var syncFileSystemNatives = requireNative('sync_file_system');

chromeHidden.registerCustomHook('syncFileSystem', function(bindingsAPI) {
  var apiFunctions = bindingsAPI.apiFunctions;

  // Functions which take in an [instanceOf=FileEntry].
  function bindFileEntryFunction(functionName) {
    apiFunctions.setUpdateArgumentsPostValidate(
        functionName, function(entry, callback) {
      var fileSystemUrl = entry.toURL();
      return [fileSystemUrl, callback];
    });
  }
  ['getFileSyncStatus']
      .forEach(bindFileEntryFunction);

  // Functions which take in an [instanceOf=DOMFileSystem].
  function bindFileSystemFunction(functionName) {
    apiFunctions.setUpdateArgumentsPostValidate(
        functionName, function(filesystem, callback) {
      var fileSystemUrl = filesystem.root.toURL();
      return [fileSystemUrl, callback];
    });
  }
  ['deleteFileSystem', 'getUsageAndQuota']
      .forEach(bindFileSystemFunction);

  // Functions which return an [instanceOf=DOMFileSystem].
  apiFunctions.setCustomCallback('requestFileSystem',
                                 function(name, request, response) {
    var result = null;
    if (response) {
      result = syncFileSystemNatives.GetSyncFileSystemObject(
          response.name, response.root);
    }
    if (request.callback)
      request.callback(result);
    request.callback = null;
  });
});
