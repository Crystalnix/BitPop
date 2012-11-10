// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function checkIcon(item, size, path) {
  var icons = item.icons;
  for (var i = 0; i < icons.length; i++) {
    var icon = icons[i];
    if (icon.size == size) {
      var expected_url =
          "chrome://extension-icon/" + item.id + "/" + size + "/0";
      assertEq(expected_url, icon.url);
      return;
    }
  }
  fail("didn't find icon of size " + size + " at path " + path);
}

function checkPermission(item, perm) {
  var permissions = item.permissions;
  console.log("permissions for " + item.name);
  for (var i = 0; i < permissions.length; i++) {
    var permission = permissions[i];
    console.log(" " + permission);
    if (permission == perm) {
      assertEq(perm, permission);
      return;
    }
  }
  fail("didn't find permission " + perm);
}

function checkHostPermission(item, perm) {
  var permissions = item.hostPermissions;
  for (var i = 0; i < permissions.length; i++) {
    var permission = permissions[i];
    if (permission == perm) {
      assertEq(perm, permission);
      return;
    }
  }
  fail("didn't find permission " + perm);
}

var tests = [
  function simple() {
    chrome.management.getAll(callback(function(items) {
      chrome.test.assertEq(10, items.length);

      checkItemInList(items, "Extension Management API Test", true, false);
      checkItemInList(items, "description", true, false,
          { "description": "a short description" });
      checkItemInList(items, "enabled_app", true, true,
          { "appLaunchUrl": "http://www.google.com/",
            "offlineEnabled": true,
            "updateUrl": "http://example.com/update.xml" });
      checkItemInList(items, "disabled_app", false, true,
          { "disabledReason": "unknown" });
      checkItemInList(items, "enabled_extension", true, false,
          { "homepageUrl": "http://example.com/" });
      checkItemInList(items, "disabled_extension", false, false,
          { "optionsUrl": "chrome-extension://<ID>/pages/options.html",
            "disabledReason": "unknown" });
      checkItemInList(items, "description", true, false,
          { "installType": "development" });
      checkItemInList(items, "internal_extension", true, false,
          { "installType": "normal" });
      checkItemInList(items, "external_extension", true, false,
          { "installType": "sideload" });
      checkItemInList(items, "admin_extension", true, false,
          { "installType": "admin" });

      // Check that we got the icons correctly
      var extension = getItemNamed(items, "enabled_extension");
      assertEq(3, extension.icons.length);
      checkIcon(extension, 128, "icon_128.png");
      checkIcon(extension, 48, "icon_48.png");
      checkIcon(extension, 16, "icon_16.png");

      // Check that we can retrieve this extension by ID.
      chrome.management.get(extension.id, callback(function(same_extension) {
        checkItem(same_extension, extension.name, extension.enabled,
                  extension.isApp, extension.additional_properties);
      }));

      // Check that we have a permission defined.
      var testExtension = getItemNamed(items, "Extension Management API Test");
      checkPermission(testExtension, "management");

      var permExtension = getItemNamed(items, "permissions");
      checkPermission(permExtension, "unlimitedStorage");
      checkPermission(permExtension, "notifications");
      checkHostPermission(permExtension, "http://*/*");
    }));
  },

  function permissionWarnings() {
    var manifest_str = "{ \"name\": \"Hello World!\", \"version\": \"1.0\", " +
                       "\"permissions\": [\"http://api.flickr.com/\", " +
                       "\"bookmarks\", \"geolocation\", " +
                       "\"history\", \"tabs\"]," +
                       "\"content_scripts\": [{\"js\": [\"script.js\"], " +
                       "\"matches\": [\"http://*.flickr.com/*\"]}]}";

    chrome.management.getPermissionWarningsByManifest(
        manifest_str, callback(function(warnings) {
      chrome.test.assertEq(5, warnings.length);
      chrome.test.assertEq(
        "Access your data on *.flickr.com and api.flickr.com", warnings[0]);
      chrome.test.assertEq("Read and modify your bookmarks", warnings[1]);
      chrome.test.assertEq("Detect your physical location", warnings[2]);
      chrome.test.assertEq("Read and modify your browsing history",
                           warnings[3]);
      chrome.test.assertEq("Access your tabs and browsing activity",
                           warnings[4]);
    }));

    chrome.management.getAll(callback(function(items) {
      var extension = getItemNamed(items, "Extension Management API Test");
      chrome.management.getPermissionWarningsById(extension.id,
                                                  callback(function(warnings) {
        chrome.test.assertEq(1, warnings.length);
        chrome.test.assertEq("Manage your apps, extensions, and themes",
                             warnings[0]);
      }));
    }));
  },

  // Disables an enabled app.
  function disable() {
    listenOnce(chrome.management.onDisabled, function(info) {
      assertEq(info.name, "enabled_app");
    });

    chrome.management.getAll(callback(function(items) {
      var enabled_app = getItemNamed(items, "enabled_app");
      checkItem(enabled_app, "enabled_app", true, true);
      chrome.management.setEnabled(enabled_app.id, false, callback(function() {
        chrome.management.get(enabled_app.id, callback(function(now_disabled) {
          checkItem(now_disabled, "enabled_app", false, true);
        }));
      }));
    }));
  },

  // Enables a disabled extension.
  function enable() {
    listenOnce(chrome.management.onEnabled, function(info) {
      assertEq(info.name, "disabled_extension");
    });
    chrome.management.getAll(callback(function(items) {
      var disabled = getItemNamed(items, "disabled_extension");
      checkItem(disabled, "disabled_extension", false, false);
      chrome.management.setEnabled(disabled.id, true, callback(function() {
        chrome.management.get(disabled.id, callback(function(now_enabled) {
          checkItem(now_enabled, "disabled_extension", true, false);
        }));
      }));
    }));
  }
];

chrome.test.runTests(tests);
