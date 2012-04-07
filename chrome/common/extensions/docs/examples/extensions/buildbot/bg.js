// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var statusURL = "http://chromium-status.appspot.com/current?format=raw";

if (!localStorage.prefs) {
  // Default to notifications being on.
  localStorage.prefs = JSON.stringify({ "use_notifications": true });
}

var lastOpen = null;
var lastNotification = null;
function notifyIfStatusChange(open, status) {
  var prefs = JSON.parse(localStorage.prefs);
  if (lastOpen && lastOpen != open && prefs.use_notifications) {
    if (lastNotification) {
      lastNotification.cancel();
    }
    var notification = webkitNotifications.createNotification(
        chrome.extension.getURL("icon.png"), "Tree is " + open, status);
    lastNotification = notification;
    notification.show();
  }
  lastOpen = open;
}

function updateStatus(status) {
  chrome.browserAction.setTitle({title:status});
  var open = /open/i;
  if (open.exec(status)) {
    notifyIfStatusChange("open", status);
    //chrome.browserAction.setBadgeText("\u263A");
    chrome.browserAction.setBadgeText({text:"\u2022"});
    chrome.browserAction.setBadgeBackgroundColor({color:[0,255,0,255]});
  } else {
    notifyIfStatusChange("closed", status);
    //chrome.browserAction.setBadgeText("\u2639");
    chrome.browserAction.setBadgeText({text:"\u00D7"});
    chrome.browserAction.setBadgeBackgroundColor({color:[255,0,0,255]});
  }
}

function requestStatus() {
  requestURL(statusURL, updateStatus);
  setTimeout(requestStatus, 30000);
}

function requestURL(url, callback) {
  //console.log("requestURL: " + url);
  var xhr = new XMLHttpRequest();
  try {
    xhr.onreadystatechange = function(state) {
      if (xhr.readyState == 4) {
        if (xhr.status == 200) {
          var text = xhr.responseText;
          //console.log(text);
          callback(text);
        } else {
          chrome.browserAction.setBadgeText({text:"?"});
          chrome.browserAction.setBadgeBackgroundColor({color:[0,0,255,255]});
        }
      }
    }

    xhr.onerror = function(error) {
      console.log("xhr error: " + JSON.stringify(error));
      console.dir(error);
    }

    xhr.open("GET", url, true);
    xhr.send({});
  } catch(e) {
    console.log("exception: " + e);
  }
}

window.onload = function() {
  window.setTimeout(requestStatus, 10);
}
