// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These have to be sync'd with extension_file_browser_private_apitest.cc
var expectedVolume1 = {
  devicePath: 'device_path1',
  mountPath: 'removable/mount_path1',
  systemPath: 'system_path1',
  filePath: 'file_path1',
  deviceLabel: 'device_label1',
  driveLabel: 'drive_label1',
  deviceType: 'flash',
  totalSize:  1073741824,
  isParent: false,
  isReadOnly: false,
  hasMedia: false,
  isOnBootDevice: false
};

var expectedVolume2 = {
  devicePath: 'device_path2',
  mountPath: 'mount_path2',
  mountPath: 'removable/mount_path2',
  systemPath: 'system_path2',
  filePath: 'file_path2',
  deviceLabel: 'device_label2',
  driveLabel: 'drive_label2',
  deviceType: 'hdd',
  totalSize:  47723,
  isParent: true,
  isReadOnly: true,
  hasMedia: true,
  isOnBootDevice: true
};

var expectedVolume3 = {
  devicePath: 'device_path3',
  mountPath: 'mount_path3',
  mountPath: 'removable/mount_path3',
  systemPath: 'system_path3',
  filePath: 'file_path3',
  deviceLabel: 'device_label3',
  driveLabel: 'drive_label3',
  deviceType: 'optical',
  totalSize:  0,
  isParent: true,
  isReadOnly: false,
  hasMedia: false,
  isOnBootDevice: true
};

function validateVolume(volume, expected) {
  for (var key in expected) {
    if (volume[key] != expected[key]) {
      console.log('Expected "' + key + '" volume property to be: "' +
                  expected[key] + '"' + ', but got: "' + volume[key] +
                  '" instead.');
      return false;
    }
  }
  if (Object.keys(expected).length != Object.keys(volume).length) {
    console.log("Unexpected property found in returned volume");
    return false;
  }
  return true;
};

chrome.test.runTests([
  function removeMount() {
    // The ID of this extension.
    var fileBrowserExtensionId = "ddammdhioacbehjngdmkjcjbnfginlla";
    var testFileName = "tmp/test_file.zip";
    var fileUrl = "filesystem:chrome-extension://" + fileBrowserExtensionId +
                  "/external/" + testFileName;

    chrome.fileBrowserPrivate.removeMount(fileUrl);

    // We actually check this one on C++ side. If MountLibrary.RemoveMount
    // doesn't get called, test will fail.
    chrome.test.succeed();
  },

  function getVolumeMetadataValid1() {
    chrome.fileBrowserPrivate.getVolumeMetadata(
        "device_path1",
        chrome.test.callbackPass(function(result) {
          chrome.test.assertTrue(validateVolume(result, expectedVolume1),
              "getVolumeMetadata result for first volume not as expected");
    }));
  },

  function getVolumeMetadataValid2() {
    chrome.fileBrowserPrivate.getVolumeMetadata(
        "device_path2",
        chrome.test.callbackPass(function(result) {
          chrome.test.assertTrue(validateVolume(result, expectedVolume2),
              "getVolumeMetadata result for second volume not as expected");
    }));
  },

  function getVolumeMetadataValid3() {
    chrome.fileBrowserPrivate.getVolumeMetadata(
        "device_path3",
        chrome.test.callbackPass(function(result) {
          chrome.test.assertTrue(validateVolume(result, expectedVolume3),
              "getVolumeMetadata result for third volume not as expected");
    }));
  },

  function getVolumeMetadataNonExistentPath() {
    chrome.fileBrowserPrivate.getVolumeMetadata(
        "non_existent_device_path",
        chrome.test.callbackFail("Device path not found"));
  },

  function getVolumeMetadataBlankPath() {
    chrome.fileBrowserPrivate.getVolumeMetadata(
        "",
        chrome.test.callbackFail("Device path not found"));
  }
]);
