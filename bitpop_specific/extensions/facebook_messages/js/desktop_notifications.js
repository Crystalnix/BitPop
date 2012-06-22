// This file is packaged with the extension then re-fetched from the
// server for updates. Bitter experience has taught us that we can't depend on
// the server resource loading correctly.

Array.prototype.contains = function(obj) {
  var i = this.length;
  while (i--) {
    if (this[i] === obj) {
      return true;
    }
  }
  return false;
};

/**
 * Copyright 2004-present Facebook. All Rights Reserved.
 *
 * Provides functions to get and display notifications in a desktop window.
 * Currently only Chrome supports the "web notifications" spec. Its
 * implementation is called webkitNotifications.
 *
 * Typically webkitNotifications is accessed from two distinct contexts:
 * 1) from a document on the host site
 * 2) from a Chrome extension that has permission to request data from the
 *    host site.
 *
 * This module supports both use cases. Some functions are Chrome-extension
 * only. A future refactoring may resolve to put them in a subclass, but
 * chances are higher we will eliminate support for 1) entirely.
 *
 * This module intentionally has no @requires dependencies so that it can
 * work in the extension context with a single <script> tag. The extension
 * may also load it as a haste resource via rscrx.php. Contact gdingle for
 * details.
 *
 * Web Notifications:
 *   http://dev.w3.org/2006/webapi/WebNotifications/publish/
 *   Chrome implementation: http://www.fburl.com/?key=1717695
 *
 * @provides desktop-notifications
 */
DesktopNotifications = {

  DEFAULT_FADEOUT_DELAY: 20000,
  CLOSE_ON_CLICK_DELAY: 300,
  // 250 is a good delay for a human-visible flash
  COUNTER_BLINK_DELAY: 250,

  // Collection of notifications currently on screen
  notifications: [],
  _timer: null,

  // The following values are supposed to be in window.webkitNotifications,
  // but they're not defined as of Chrome 9/Chromium 75753.
  PERMISSION_ALLOWED: 0,
  PERMISSION_NOT_ALLOWED: 1,
  PERMISSION_DENIED: 2,

  // These may be overridden by clients
  getEndpoint: '/desktop_notifications/get.php',
  countsEndpoint: '/desktop_notifications/counts.php',
  domain: '',
  protocol: '',

  // polling instance
  _interval: null,

  // These are used to short circuit data fetching on the server.
  // See flib/notifications/prepare/prepare.php
  _latest_notif: 0,
  _latest_read_notif: 0,

  // Unread counts, used for badge and fetching new HTML
  _num_unread_notif: 0,
  _num_unseen_inbox: 0,

  // We may obtain a CSRF token from the server to suppress click-jacking
  // protection on requests to HTML pages.
  fb_dtsg: '',

  received_cache: [],

  threads_unseen_before: [],
  just_connected: false,

  /**
   * Start polling for notifications. New notifications are displayed
   * immediately. This should called from clients off of facebook.com. On the
   * main site we have presence to do our polling for us.
   */
  start: function(refresh_time) {
    var self = DesktopNotifications;
    // Don't refresh faster than fade out
    if (refresh_time < self.DEFAULT_FADEOUT_DELAY) {
      refresh_time = self.DEFAULT_FADEOUT_DELAY;
    }

    self.stop();
    self.showActiveIcon();
    // fetch the current counts immediately
    self.fetchServerInfo(self.handleServerInfo, self.showInactiveIcon);

    self._interval = setInterval(function() {
      self.fetchServerInfo(
        function(serverInfo) {
          self.handleServerInfo(serverInfo);
          // set back to active in case of previous error
          self.showActiveIcon();
        },
        self.showInactiveIcon);
    }, refresh_time);
  },

  /**
   * Get the best popup type to show. See WebDesktopNotificationsBaseController
   */
  getPopupType: function() {
    var self = DesktopNotifications;

    var type = 'inbox';
    // if (self._num_unseen_inbox && !self._num_unread_notif) {
    //   type = 'inbox';
    // }
    return type;
  },

  /**
   * Stop polling.
   */
  stop: function() {
    clearInterval(DesktopNotifications._interval);
    DesktopNotifications.showInactiveIcon();
  },

  /**
   * Updates icon in Chrome extension to normal blue icon
   */
  showActiveIcon: function() {
    if (chrome && chrome.browserAction) {
      chrome.browserAction.setIcon({path: '/images/icon19.png'});
    }
  },

  /**
   * Updates icon in Chrome extension to gray icon and clears badge.
   */
  showInactiveIcon: function() {
    if (chrome && chrome.browserAction) {
      chrome.browserAction.setBadgeText({text: ''});
      chrome.browserAction.setIcon({path: '/images/icon-loggedout.png'});
    }
  },

  /**
   * Fetches metadata from the server on the current state of the user's
   * notifications and inbox.
   */
  fetchServerInfo: function(callback, errback, no_cache) {
    callback = callback || function(d) { console.log(d); };
    errback = errback || function(u, e) { console.error(u, e); };
    var self = DesktopNotifications;
    // var uri = self.protocol + self.domain + self.countsEndpoint +
    //   '?latest=' + self._latest_notif +
    //   '&latest_read=' + self._latest_read_notif;
    // if (no_cache) {
    //   uri += '&no_cache=1';
    // }
    // self._fetch(
    //   uri,
    //   function(json) {
    //     callback(JSON.parse(json));
    //   },
    //   errback
    // );

    // var query1 = "SELECT unseen_count FROM unified_thread_count WHERE folder='inbox'";
    // var query2 = "SELECT author_id, body, created_time, unread " +
    //              "FROM unified_message " +
    //              "WHERE thread_id IN " +
    //              "(SELECT thread_id FROM unified_thread WHERE folder='inbox')" +
    //              "ORDER BY created_time DESC, unread" +
    //              "LIMIT 5";
    // var query3 = "SELECT uid, name, pic_square FROM user WHERE uid IN " +
    //              "(SELECT author_id FROM #query2)";


    var query = "SELECT thread_id, unread, unseen FROM thread WHERE folder_id=0";
    chrome.extension.sendRequest(self.controllerExtensionId,
        { type: 'fqlQuery',
          query: query
          //params: { fields: 'id,unseen,unread,from,message,updated_time' }
        },
        function (response) {
          if (response.error)
            errback(response.error, 'fqlQuery: ' + query);
          else
            callback(response);
        }
     );
  },

  _fetch: function(uri, callback, errback) {
    var xhr = new XMLHttpRequest();
    xhr.open("GET", uri, true);
    xhr.onreadystatechange = function() {
      if (xhr.readyState == 4) {
        try {
          if (xhr.status == 200) {
            return callback(xhr.responseText);
          } else {
            throw 'Status ' + xhr.status + ': ' + xhr.responseText;
          }
        } catch (e) {
          errback(e, uri);
        }
      }
    };
    xhr.send(null);
  },

  /**
   * Decides whether to fetch any items for display depending on data from
   * server on unread counts.
   */
  handleServerInfo: function(serverInfo0) {
    var self = DesktopNotifications;

    var p = {}
    p.data = serverInfo0;
    var serverInfo = p;

    serverInfo.summary = { unseen_count: 0, unread_count: 0 };
    for (var i = 0; i < serverInfo0.length; ++i) {
      serverInfo.summary.unseen_count += serverInfo0[i].unseen;
      serverInfo.summary.unread_count += serverInfo0[i].unread;
    }

    //self._handleNotifInfo(serverInfo);
    if (serverInfo.summary.unseen_count != 0) {
      var first_unseen_thread_index = null;
      var local_unseen_count = 0;
      for (var i = 0; i < serverInfo.data.length; i++) {
        if (serverInfo.data[i].unseen > 0) {
          if (self.just_connected ||
              self.threads_unseen_before.contains(serverInfo.data[i].thread_id)) {
            local_unseen_count++;

            if (first_unseen_thread_index === null)
              first_unseen_thread_index = i;
          }
          if (self.just_connected) {
            self.threads_unseen_before.push(serverInfo.data[i].thread_id);
          }
        }
      }

      self.just_connected = false;
      serverInfo.summary.unseen_count = local_unseen_count;

      // get message for last unseen thread
      if (first_unseen_thread_index !== null) {

        var query_obj = {
          users: "SELECT uid, name FROM user " +
             "WHERE uid IN (SELECT recipients FROM thread WHERE " +
             "folder_id = 0 AND thread_id='" + serverInfo.data[first_unseen_thread_index].thread_id + "')",
          messages: "SELECT thread_id, author_id, body, created_time FROM message WHERE " +
             "thread_id='" + serverInfo.data[first_unseen_thread_index].thread_id + "'" +
             " ORDER BY created_time DESC"
        };

        var query = JSON.stringify(query_obj);

        chrome.extension.sendRequest(self.controllerExtensionId,
          {
            type: 'fqlQuery',
            query: query
          },
          function (response) {
            if (response.error)
              self.showInactiveIcon();
            else
              self._handleInboxInfo(serverInfo, i, response);
          }
        );
      }

      if (serverInfo.summary.unseen_count == 0) {
        self._num_unseen_inbox = 0;
        self.updateUnreadCounter();
      }

    } else if (serverInfo.summary.unseen_count !== self._num_unseen_inbox) {
      self._num_unseen_inbox = serverInfo.summary.unseen_count; // actually 0
      self.updateUnreadCounter();
    }

    for (var i = 0; i < serverInfo.data.length; i++) {
      if (serverInfo.data[i].unseen == 0 &&
          self.threads_unseen_before.contains(serverInfo.data[i].thread_id)) {
        // if a previously marked as unseen thread became seen,
        // remove this from threads_unseen_before
        var index = -1;
        for (var j = 0; j < self.threads_unseen_before.length; j++) {
          if (self.threads_unseen_before[j] == serverInfo.data[i].thread_id) {
            index = j;
            break;
          }
        }

        // remove element from array logic
        if (index == 0)
          self.threads_unseen_before.shift();
        else if (index == self.threads_unseen_before.length - 1)
          self.threads_unseen_before.pop();
        else
          self.threads_unseen_before = self.threads_unseen_before.slice(0, index)
            .concat(
              self.threads_unseen_before.slice(index + 1,
                self.threads_unseen_before.length));
      }
    }
  },

  // _handleNotifInfo: function(notifInfo) {
  //   var self = DesktopNotifications;

  //   if (self._num_unread_notif !== notifInfo.length) {
  //     if (notifInfo.length &&
  //         (self._latest_notif < notifInfo[0].created_time)) {
  //       self._latest_notif = notifInfo[0].created_time;
  //       self._latest_data = notifInfo[0];
  //       // self._latest_read_notif = notifInfo.latest_read;
  //       // see WebDesktopNotificationsBaseController::TYPE_NOTIFICATIONS
  //       self.addNotificationByType('notifications');
  //     }
  //     self._num_unread_notif = notifInfo.length;
  //     self.updateUnreadCounter();
  //   }
  // },

  _findResultSet: function(query_name, data) {
    for (var i = 0; i < data.length; ++i) {
      if (data[i].name == query_name)
        return data[i].fql_result_set;
    }
    return null;
  },

  _handleInboxInfo: function(threads, lastUnseenThreadIndex, data) {
    var self = DesktopNotifications;

    var users = self._findResultSet('users', data);
    var messages = self._findResultSet('messages', data);

    //if (!inboxInfo) {
    //  return;
    //}
    if (threads.summary.unseen_count !== self._num_unseen_inbox) {
      if (threads.summary.unseen_count > self._num_unseen_inbox) {
        // see WebDesktopNotificationsBaseController::TYPE_INBOX
        self._latest_data = {
          thread: threads.data[lastUnseenThreadIndex],
          users: users,
          messages: messages
        };

        self.addNotificationByType('inbox');
      }
      self._num_unseen_inbox = threads.summary.unseen_count;
      self.updateUnreadCounter();
    }
  },

  /**
   * Updates "badge" in Chrome extension toolbar icon.
   * See http://code.google.com/chrome/extensions/browserAction.html#badge
   */
  updateUnreadCounter: function() {
    var self = DesktopNotifications;
    if (chrome && chrome.browserAction) {
      // first set the counter to empty
      chrome.browserAction.setBadgeText({text: ''});
      // wait, then set it to new value
      setTimeout(function() {
          // don't show a zero
          var num = (self.getUnreadCount() || '') + '';
          chrome.browserAction.setBadgeText({text: num});
        },
        self.COUNTER_BLINK_DELAY,
        false // quickling eats timeouts otherwise
        );
    }
  },

  getUnreadCount: function() {
    var self = DesktopNotifications;
    return self._num_unread_notif + self._num_unseen_inbox;
  },

  _nameByUid: function(uid, users) {
    for (var i = 0; i < users.length; i++) {
      if (users[i].uid.toString() == uid.toString())
        return users[i].name;
    }
    return "{Unknown user}";
  },

  addNotificationByType: function(type) {
    var self = DesktopNotifications;
    if (self._latest_data) {
      var fromUid, body, name;

      if (self._latest_data.messages.length > 0) {
        var lastComment = self._latest_data.messages[0];
        fromUid = lastComment.author_id;
        name = self._nameByUid(lastComment.author_id, self._latest_data.users);
        body = lastComment.body;
      } else {
        fromUid = 0;
        name = '<Unknown>';
        body = '<No messages found.>';
      }

      // var uri = self.protocol + self.domain + self.getEndpoint +
      //   '?type=' + (type || '');
      var notification =
        window.webkitNotifications.createNotification(
            'http://graph.facebook.com/'+ fromUid + '/picture?type=square',
            name + ' sent you a new message.', body);
      notification.clickHref =
        "https://www.facebook.com/messages";
      // In case the user has multiple windows or tabs open, replace
      // any existing windows for this alert with this one.
      notification.replaceId = 'com.facebook.alert.' + type;

      self.showNotification(notification, self.DEFAULT_FADEOUT_DELAY);
    }
  },

  /**
   * Adds a new notification to the queue.
   * After an expiration period, it is closed.
   */
  addNotification: function(alert_id, delay) {
    var self = DesktopNotifications;
    if (!window.webkitNotifications) {
      return;
    }

    if (typeof delay == 'undefined') {
      delay = self.DEFAULT_FADEOUT_DELAY;
    }
    var uri = self.protocol + self.domain + self.getEndpoint +
      '?alert_id=' + (alert_id || '') +
      '&latest=' + self._latest_notif +
      '&latest_read=' + self._latest_read_notif;
    var notification =
      window.webkitNotifications.createHTMLNotification(uri);

    // In case the user has multiple windows or tabs open, replace
    //// any existing windows for this alert with this one.
    notification.replaceId = 'com.facebook.alert.' + alert_id;

    self.showNotification(notification, delay);
  },

  showNotification: function(notification, delay) {
    notification.show();
    notification.onclick = function(e) {
      chrome.tabs.create({ url: this.clickHref });
      // Oddly, defer(0) still cancels the notification before the
      // click passes through.  Give it a little time before we
      // close the window.
      setTimeout(function() {
          e.srcElement.cancel();
        },
        DesktopNotifications.CLOSE_ON_CLICK_DELAY,
        false // quickling eats timeouts otherwise
        );
    };
    DesktopNotifications.notifications.push(notification);
    DesktopNotifications.restartTimer(delay);
  },

  expireNotifications: function() {
    DesktopNotifications.notifications.forEach(function(n) { n.cancel(); });
    DesktopNotifications.notifications = [];
    DesktopNotifications._timer = null;
  },

  restartTimer: function(extraTime) {
    if (DesktopNotifications._timer) {
      clearTimeout(DesktopNotifications._timer);
    }
    DesktopNotifications._timer = setTimeout(function() {
      DesktopNotifications.expireNotifications();
    }, extraTime);
  }
};
