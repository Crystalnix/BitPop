/* ======================================================
 * Copyright (C) 2011-2012 House of Life Property Ltd. All rights reserved.
 * Copyright (C) 2011-2012 Crystalnix <vgachkaylo@crystalnix.com>
 *
 * @requires
 *   Strophe
 *   jQuery
 * ======================================================*/

var friendListCached = null;
var friendListLastUpdateTime = null;
var connection = null;
var friendListRequestInterval = null;
var need_more_permissions = false;
var seen_message_timeout = null;
var doing_permissions_request = false;
var offline_wait_timer = null;
var auth_wait_timer = null;
var strophe_wait_timer = null;
var query_idle_timer = null;
var manual_disconnect = false;
var prevIdleState = "active";

var bitpop;
if (!bitpop)
  bitpop = {};
bitpop.FacebookController = (function() {
  // -------------------------------------------------------------------------------
  // Constants
  var BOSH_SERVER_URL = "http://tools.bitpop.com:5280";
  var FB_APPLICATION_ID = "234959376616529";
  var SUCCESS_URL = 'https://www.facebook.com/connect/login_success.html';
  var FB_LOGOUT_URL = 'https://www.facebook.com/logout.php';
  var LOGOUT_NEXT_URL = 'https://sync.bitpop.com/sidebar/logout';
  var GRAPH_API_URL = 'https://graph.facebook.com';
  var FQL_API_URL = 'https://graph.facebook.com/fql';
  var REST_API_URL = 'https://api.facebook.com/method/';
  var TOKEN_EXCHANGE_URL = 'https://sync.bitpop.com/fb_exchange_token/';

  var FRIEND_LIST_UPDATE_INTERVAL = 1000 * 60; // in milliseconds
  var MACHINE_IDLE_INTERVAL = 60 * 10;  // in seconds

  var FB_PERMISSIONS = ['xmpp_login',
          'user_online_presence', 'friends_online_presence',
          'manage_notifications', 'read_mailbox', 'user_status',
          'publish_stream' ];

  // Facebook error codes
  var FQL_ERROR = {
    API_EC_PARAM_ACCESS_TOKEN: 190
  };

  var IDS = {
    friends: "engefnlnhcgeegefndkhijjfdfbpbeah",
    messages: "dhcejgafhmkdfanoalflifpjimaaijda",
    notifications: "omkphklbdjafhafacohmepaahbofnkcp"
  };

  // -------------------------------------------------------------------------------
  // Public methods
  var public = {
    init: function() {
      chrome.tabs.onUpdated.addListener(onTabUpdated);
      chrome.extension.onMessageExternal.addListener(onRequest);

      setupAjaxErrorHandler();

      onObserve({ extensionId: IDS.friends });
      // messages extension
      onObserve({ extensionId: IDS.messages });
      // notifications extensions
      onObserve({ extensionId: IDS.notifications });

      if (localStorage.accessToken) {
        checkForPermissions(hadAccessTokenCallback);
      }
    }
  };

  // -------------------------------------------------------------------------------
  // Private methods
  function notifyObservingExtensions(notificationObject) {
    for (var i = 0; i < observingExtensionIds.length; i++)
      chrome.extension.sendMessage(observingExtensionIds[i], notificationObject);
  }

  function notifyFriendsExtension(notificationObject) {
    chrome.extension.sendMessage(IDS.friends, notificationObject);
  }

  function hadAccessTokenCallback() {
    notifyObservingExtensions({ type: 'accessTokenAvailable',
                         accessToken: localStorage.accessToken });
    if (localStorage.myUid)
      onGotUid();
    else
      getMyUid();
  }

  function setupAjaxErrorHandler() {
    $.ajaxSetup({
      error:function(x,e){
        var errorMsg = null;
        var shouldSetDoingPermissionsRequest = true;

        if (x.status==0){
          errorMsg = 'You are offline!!\n Please Check Your Network.';

          notifyObservingExtensions({ type: 'wentOffline' });

          if (localStorage.accessToken && offline_wait_timer === null) {
            offline_wait_timer = setTimeout(function() {
              checkForPermissions(hadAccessTokenCallback);
              offline_wait_timer = null;
            }, 15000);
          }
        } else if (x.status == 400) {
          var response = JSON.parse(x.responseText);
          if (response.error && response.error.type == 'OAuthException') {
            if (doing_permissions_request) {
              if (auth_wait_timer === null) {
                auth_wait_timer = setTimeout(function() {
                  checkForPermissions();
                  //auth_wait_timer = null;
                }, 15000);
              } else {
                need_more_permissions = true;
                login(FB_PERMISSIONS);
              }
              doing_permissions_request = false;
            }
            else
              checkForPermissions();

            shouldSetDoingPermissionsRequest = false;
          }
          errorMsg = 'Not authorized.';
        } else if (x.status==404){
          errorMsg = 'Requested URL not found.' ;
        } else if (x.status==500){
          errorMsg = 'Internal Server Error.';
        } else if (e=='parsererror'){
          errorMsg = 'Parsing JSON Request failed.';
        } else if (e=='timeout'){
          errorMsg = 'Request Time out.';
        } else {
          errorMsg = 'Unknown Error.\n' + x.responseText;
        }

        if (errorMsg) {
          if (x.callOnError) {
            x.callOnError({ error: errorMsg });
          }

          console.error(errorMsg);
          notifyObservingExtensions(
            {
              type: 'operationFailed',
              message: errorMsg
            }
          );
        }

        if (doing_permissions_request && shouldSetDoingPermissionsRequest)
          doing_permissions_request = false;
      }
    });
  }

  function connectToFacebookChat() {
    if (!connection)
      connection = new Strophe.Connection(BOSH_SERVER_URL + '/http-bind/');
    if (connection && connection.connected) {
      connection.sync = true;
      connection.flush();
      connection.disconnect();
      connection.sync = false;
      return;
    }

    connection.facebookConnect(
                            localStorage.myUid + "@chat.facebook.com",
                            onConnect,
                            60,
                            1,
                            FB_APPLICATION_ID,
                            localStorage.accessToken
                            );
  }

  function onConnect(status) {
    if (status == Strophe.Status.CONNECTING) {
      console.log('Strophe is connecting.');
    } else if (status == Strophe.Status.CONNFAIL) {
      console.warn('Strophe failed to connect.');
      connection.reset();
      if (localStorage.myUid && localStorage.accessToken)
        setTimeout(function() {
          if (localStorage.myUid && localStorage.accessToken)
            connectToFacebookChat();
        }, 10000);
    } else if (status == Strophe.Status.DISCONNECTING) {
      console.log('Strophe is disconnecting.');
      if (!(prevIdleState == 'idle' || prevIdleState == 'locked')) {
        clearInterval(query_idle_timer);
        query_idle_timer = null;
      }
    } else if (status == Strophe.Status.DISCONNECTED) {
      console.log('Strophe is disconnected.');
      connection.reset();
      if (localStorage.myUid && localStorage.accessToken &&
          !(prevIdleState == 'idle' || prevIdleState == 'locked'))
        connectToFacebookChat();
    } else if (status == Strophe.Status.CONNECTED) {
      console.log('Strophe is connected.');
      //console.log('Send a message to ' + connection.jid +
      //' to talk to me.');

      connection.addHandler(onMessage, null, 'message', null, null, null);
      connection.addHandler(onPresence, null, 'presence', null, null, null);
      connection.send($pres().tree());

      notifyFriendsExtension({ type: 'chatAvailable' });

      if (query_idle_timer === null)
        query_idle_timer = setInterval(function() {
          chrome.idle.queryState(MACHINE_IDLE_INTERVAL, idleStateUpdate);
        }, 30 * 1000);  // every 30 seconds
    }
  }

  function onMessage(msg) {
    //console.log(msg);
    var to = msg.getAttribute('to');
    var from = msg.getAttribute('from');
    var type = msg.getAttribute('type');
    var elems = msg.getElementsByTagName('body');
    var composing = msg.getElementsByTagName('composing');
    var paused = msg.getElementsByTagName('active');

    var fromUid = null;
    var matches = from.match(/\-?(\d+)@.*/);
    if (matches.length == 2) {
      fromUid = matches[1];
    }

    if (composing.length > 0 || paused.length > 0) {
      notifyFriendsExtension({ type:     'typingStateChanged',
                               isTyping: (composing.length > 0),
                               uid:      fromUid });
    }

    if (type == "chat" && elems.length > 0) {
      var body = elems[0];
      var msgText = Strophe.getText(body);
      var msgDate = new Date();

      notifyObservingExtensions({ type: 'newMessage', body: msgText, from: fromUid });

      console.log('I got a message from ' + from + ': ' +
      msgText);

    }

    // we must return true to keep the handler alive.
    // returning false would remove it after it finishes.
    return true;
  }

  function onPresence(stanza) {
    var to = stanza.getAttribute('to');
    var from = stanza.getAttribute('from');
    var type = stanza.getAttribute('type');

    console.log('I got a presence stanza from ' + from + ': ' + type);
    // if type is not defined status is 'available'
    // can have 'show' element with the value of 'idle'
    if (!type) {
      var showElem = stanza.getElementsByTagName('show');
      if (showElem.length > 0)
        type = Strophe.getText(showElem[0]);
      else
        type = 'available';
    }

    var fromUid = null;
    var matches = from.match(/\-?(\d+)@.*/);
    if (matches.length == 2) {
      fromUid = matches[1];
    }

    if (fromUid != localStorage.myUid) {
      // cast status to extension specific values
      var statusMap = {
        available: 'active',
        idle: 'idle',
        error: 'error',
        unavailable: 'offline'
      };

      var st = statusMap[type];
      if (!st)
        st = 'offline';
      // -- end of cast

      notifyFriendsExtension({ type: 'userStatusChanged',
                               uid: fromUid,
                               status: st
                             });
    }

    // we must return true to keep the handler alive.
    // returning false would remove it after it finishes.
    return true;
  }

  function sendMessage(message, uidTo, state) {
    var to = '-' + uidTo + "@chat.facebook.com";

    if(to && (message || state)){
      var reply = $msg({
        to: to,
        type: 'chat'
      });
      if (message)
        reply = reply.c('body').t(message).up();
      reply = reply.c(state, {xmlns: "http://jabber.org/protocol/chatstates"});
      connection.send(reply.tree());

      console.log('I sent ' + to + ': ' + message);
    }
  }

  function startChatAgain() {
    if (query_idle_timer === null)
      query_idle_timer = setInterval(function() {
          chrome.idle.queryState(MACHINE_IDLE_INTERVAL, idleStateUpdate);
        }, 30 * 1000);  // every 30 seconds
    prevIdleState = 'active';
    connection.reset();
    if (localStorage.myUid && localStorage.accessToken)
      connectToFacebookChat();
    notifyObservingExtensions({ type: 'chatIsAvailable' });
  }

  function enableIdleChatState(newState, shouldLaunchTimer) {
     clearInterval(query_idle_timer);
     query_idle_timer = null;
     if (connection.connected) {
       if (shouldLaunchTimer)
         query_idle_timer = setInterval(function() {
           chrome.idle.queryState(MACHINE_IDLE_INTERVAL, idleStateUpdate);
         }, 1 * 1000);  // every second

       prevIdleState = newState;
       connection.disconnect();
       notifyObservingExtensions({ type: 'chatIsIdle' });
     }
  }

  function idleStateUpdate(newState) {
    if ((prevIdleState == "idle" || prevIdleState == "locked") &&
        newState == "active") {
      clearInterval(query_idle_timer);
      query_idle_timer = null;
      startChatAgain();
    } else if (prevIdleState == "active" &&
               (newState == "idle" || newState == "locked"))
      enableIdleChatState(newState, true);
  }

  function accessTokenFromSuccessURL(url) {
    var hashSplit = url.split('#');
    if (hashSplit.length > 1) {
      var paramsArray = hashSplit[1].split('&');
      for (var i = 0; i < paramsArray.length; i++) {
        var paramTuple = paramsArray[i].split('=');
        if (paramTuple.length > 1 && paramTuple[0] == 'access_token')
          return paramTuple[1];
      }
    }
    return null;
  }

  function onTabUpdated(tabId, changeInfo, tab) {
    if (changeInfo.url && (changeInfo.url.indexOf(SUCCESS_URL) == 0)) {
      if (changeInfo.url == LOGOUT_NEXT_URL) {
        localStorage.removeItem('accessToken');
        localStorage.removeItem('myUid');
        notifyObservingExtensions({ type: 'loggedOut' });
        connection.disconnect();
        chrome.bitpop.facebookChat.loggedOutFacebookSession();
      } else if (!localStorage.accessToken || need_more_permissions) {
        var accessToken = accessTokenFromSuccessURL(changeInfo.url);
        if (!accessToken) {
          localStorage.removeItem('accessToken');
          console.warn('Could not get an access token from url %s',
              changeInfo.url);
        } else {
          localStorage.setItem('accessToken', accessToken);
          extendAccessToken();
          checkForPermissions(function() {
              notifyObservingExtensions({ type: 'accessTokenAvailable',
                                        accessToken: accessToken });

              getMyUid();
          });
        }
      }

      chrome.tabs.remove(tabId);
    }
  }

  function checkForPermissions(callbackAfter) {
    doing_permissions_request = true;
    graphApiRequest('/me/permissions', {}, function(response) {
        var permsArray = response.data[0];

        var permsToPrompt = [];
        for (var i in FB_PERMISSIONS) {
          if (permsArray[FB_PERMISSIONS[i]] == null) {
            permsToPrompt.push(FB_PERMISSIONS[i]);
          }
        }

        if (permsToPrompt.length > 0) {
          console.warn('Insufficient permissions. Requesting for more.');
          console.warn('Need permissions: ' + permsToPrompt.join(','));
          need_more_permissions = true;
          login(permsToPrompt);
        } else if (callbackAfter) {
          need_more_permissions = false;
          callbackAfter();  // execute the 'after' callback on success
        }
        doing_permissions_request = false;
      },
      null);
  }

  function getMyUid() {
    graphApiRequest('/me', { fields: 'id' }, function(data) {
            // do all hard initialization work
            // when got the user id
            //
            if (data.id)
              localStorage.myUid = data.id;
            onGotUid();
          });
  }

  function getFriendList() {
    if (localStorage.accessToken && localStorage.myUid) {
      var friendListQuery =
            'SELECT uid, name, pic_square, online_presence, profile_url' +
            ' FROM user WHERE uid IN' +
            ' (SELECT uid2 FROM friend WHERE uid1=me())';
      fqlRequest(friendListQuery, onFriendListReceived);
    }
  }

  function graphApiRequest(path, params, callOnSuccess, callOnError) {
    if (!localStorage.accessToken)
      return;

    params.access_token = localStorage.accessToken;

    var xhr = $.get(GRAPH_API_URL + path,
          params,
          function(pdata) {
            // var pdata = null;
            // try {
            //   pdata = JSON.parse(data);
            // }
            // catch (e) {
            //   pdata = { error: { type: 'ParseException', message: 'Unable to ' +
            //     'parse server response as JSON. response: ' + data }};
            // }

            if (pdata.error) {
              var errorMsg = 'Unable to fetch data from Facebook Graph API, url:' +
                  path + '\nError: ' + pdata.error.type +
                  ': ' + pdata.error.message;

              console.error(errorMsg);

              errorMsg += '\nTry to logout and login once again.';

              callOnError({ error: errorMsg });

              notifyObservingExtensions(
                {
                  type: 'operationFailed',
                  message: errorMsg
                }
              );
              return;
            }
            callOnSuccess(pdata);
          },
          'json');

    if (callOnError)
      xhr.callOnError = callOnError;
  }

  function fqlRequest(query, callOnSuccess, callOnError) {
    xhr = $.get(FQL_API_URL,
          {
            q: query,
            //format: 'json',
            access_token: localStorage.accessToken
          },
          function (pdata) {
            // var pdata = null;
            // try {
            //   pdata = JSON.parse(data);
            // }
            // catch (e) {
            //   pdata = { error_code: '-1', error_msg: 'Unable to ' +
            //     'parse server response as JSON. response: ' + data };
            // }

            if (pdata.error_code) {
              var errorMsg = 'Unable to fetch data from Facebook FQL API, query:' +
                  query + '\nError: ' + pdata.error_code +
                  ': ' + (pdata.error_msg ? pdata.error_msg : 'No message');

              console.error(errorMsg);

              errorMsg += '\nTry to logout and login once again.';

              callOnError({ error: errorMsg });

              notifyObservingExtensions(
                {
                  type: 'operationFailed',
                  message: errorMsg
                }
              );
              return;
            }
            callOnSuccess(pdata.data);
          },
          'json');

    if (callOnError)
      xhr.callOnError = callOnError;
  }

  function restApiCall(method, params, onSuccess, onError) {
    params.format = 'json';
    params.access_token = localStorage.accessToken;

    var xhr = $.get(
        REST_API_URL + method,
        params,
        function (data) {
          if (data === true)
            onSuccess({});
          else if (data.error_code && onError) {
            var errorMsg = 'Unable to fetch data from Facebook REST API, method:' +
                  method + '\nError: ' + data.error_code +
                  ': ' + (data.error_msg ? data.error_msg : 'No message');
            onError({ error: errorMsg });
          }
        },
        'json');
    if (onError)
      xhr.callOnError = onError;
  }

  function extendAccessToken() {
    var token = localStorage.accessToken;
    if (!token)
      return;

    var xhr = $.get(
        TOKEN_EXCHANGE_URL + token, {},
        function(data) {
          var at_prefix = "access_token=";
          if (data && data.indexOf(at_prefix) == 0) {
            var access_token = data.substring(at_prefix.length,
                                              data.indexOf('&'));
            if (access_token) {
              localStorage.setItem('accessToken', access_token);
              notifyObservingExtensions({ type: 'accessTokenAvailable',
                                          accessToken: access_token });
            }
            console.log('Extend token success.')
          }
        },
      'html');
    xhr.callOnError = function (error) {
      console.error(error.error);
    };
  }

  function login(permissions) {
    var urlStart = "https://www.facebook.com/dialog/oauth?client_id=" +
        FB_APPLICATION_ID;
    var url = urlStart +
        "&response_type=token" +
        "&redirect_uri=" + SUCCESS_URL +
        '&display=popup' +
        '&scope=' + permissions.join(',');
    var loginUrlStart = 'https://www.facebook.com/login.php?api_key=' +
        FB_APPLICATION_ID;

    // https://www.facebook.com/dialog/oauth?client_id=190635611002798&response_type=token&redirect_uri=https://www.facebook.com/connect/login_success.html&display=popup&scope=
    chrome.windows.getAll({ populate: true }, function (windows) {
      var found = false;
      for (var i = 0; i < windows.length; i++) {
        for (var j = 0; j < windows[i].tabs.length; j++) {
          if (windows[i].tabs[j].url.indexOf(urlStart) == 0 ||
              windows[i].tabs[j].url.indexOf(loginUrlStart) == 0) {
            chrome.tabs.update(windows[i].tabs[j].id, { selected: true, url: url });
            chrome.windows.update(windows[i].id, { focused: true });
            found = true;
            break;
          }
        }
        if (found)
          break;
      }
      if (!found) {
        var w = 640;
        var h = 400;
        var left = (screen.width/2)-(w/2);
        var top = (screen.height/2)-(h/2);

        var w = window.open(url, "newwin", "height=" + h + ",width=" + w +
            ",left=" + left + ",top=" + top +
            ",toolbar=no,scrollbars=no,menubar=no,location=no");
      }
    });
  }

  function logout() {
    if (localStorage.accessToken) {
      var url = FB_LOGOUT_URL + '?next=' +
        escape(LOGOUT_NEXT_URL) +
        '&access_token=' + localStorage.accessToken;

      chrome.windows.create({ url: url, type: "popup",
                              top: 0, left: 0, width: 50, height: 50 });
    }
    return false;
  }

  function onRequest(request, sender, sendResponse) {
    if (!request.type || !handlers[request.type]) {
      console.warn('Invalid request received');
      return;
    }
    return handlers[request.type](request, sendResponse);
  }

  function onFriendListReceived(data) {
    notifyFriendsExtension({ type: 'friendListReceived', data: data });

    friendListCached = data;
    friendListLastUpdateTime = Date.now();
  }

  function onObserve(request) {
    if (request.extensionId) {
      // first, look if such an id exists
      var found = false;
      for (var i = 0; i < observingExtensionIds.length; i++) {
        if (observingExtensionIds[i] == request.extensionId) {
          found = true;
          break;
        }
      }
      if (!found)
        observingExtensionIds.push(request.extensionId);
    }
  }

  function onLogin(request, callback) {
    if (!localStorage.accessToken) {// do not allow to login twice
      login(FB_PERMISSIONS);
      if (callback)
        callback({ canLogin: true });
      else
        return false;
      return true;
    }
    else if (callback) {
      callback({ canLogin: false });
      return true;
    }
  }

  function onSendChatMessage(request, sendResponse) {
    if (!connection.connected) {
      sendResponse({ error: 'You are now in "Offline" mode. To be able to send messages, switch back to "Online" in the facebook sidebar.' });
      return true;
    }

    if ((request.message || request.state) && request.uidTo) {
      sendMessage(request.message, request.uidTo, request.state);
      sendResponse({});
      return true;
    }
    else {
      sendResponse({ error: 'Invalid request.' });
      return true;
    }
    return false;
  }

  function onGotUid() {
    if (!localStorage.myUid) {
      setTimeout(getMyUid, 15000);
      return;
    }

    notifyObservingExtensions({ type: 'myUidAvailable',
                                        myUid: localStorage.myUid });

    getFriendList();
    if (friendListRequestInterval)
      clearInterval(friendListRequestInterval);

    friendListRequestInterval = setInterval(getFriendList,
      FRIEND_LIST_UPDATE_INTERVAL);

    connectToFacebookChat();

    chrome.bitpop.facebookChat.loggedInFacebookSession();
    chrome.bitpop.facebookChat.setGlobalMyUidForProfile(localStorage.myUid);
  }

  function onSendInvite(request, sendResponse) {
    if (friendListCached) {
      for (var i = 0; i < friendListCached.length; ++i) {
        if (friendListCached[i].uid.toString() == request.uidTo) {
          var msg = "Greetings, " + friendListCached[i].name.split(' ')[0] +
            "\n\n" +
            "Your friend invites you to use BitPop - the most censor-free " +
            "browser on the web.\n" +
            "You can download BitPop using the following link: " +
            "http://www.bitpop.com/download.php" +
            "\n\n" +
            "Thank you.";
          sendMessage(msg, request.uidTo, 'active');
          sendResponse({msg: msg});
          return true;
        }
      }
    }
    return false;
  }

  function onGraphApiCall(request, sendResponse) {
    if (localStorage.accessToken)
      graphApiRequest(request.path, request.params, sendResponse, sendResponse);
    else
      sendNotLoggedInResponse(sendResponse);
    return true;
  }

  function onFqlQuery(request, sendResponse) {
    if (localStorage.accessToken)
      fqlRequest(request.query, sendResponse, sendResponse);
    else
      sendNotLoggedInResponse(sendResponse);

    return true;
  }

  function onRestApiCall(request, sendResponse) {
    if (localStorage.accessToken)
      restApiCall(request.method, request.params, sendResponse, sendResponse);
    else
      sendNotLoggedInResponse(sendResponse);
    return true;
  }

  function onGetFBUserNameByUid(request, sendResponse) {
    console.assert(friendListCached);

    var uname = "Unknown";
    var profile_url = "http://facebook.com";
    for (var i = 0; i < friendListCached.length; i++) {
      if (friendListCached[i].uid == request.uid) {
        uname = friendListCached[i].name;
        profile_url = friendListCached[i].profile_url;
        break;
      }
    }
    sendResponse({ uname: uname, profile_url: profile_url });
    return true;
  }

  function onGetMyUidForExternal(request, sendResponse) {
    return onGraphApiCall({ path: '/me', params: { fields: 'id' } },
                   function (response) {
                     sendResponse({ id: response.id });
                   });
  }

  function onChangeOwnStatus(request) {
    if (request.status == 'unavailable') {
      enableIdleChatState('idle', false);
    } else if (request.status == 'available') {
      startChatAgain();
    }
    return false;
  }

  function sendNotLoggedInResponse(sendResponse) {
    sendResponse({ error: 'Not logged in. Please login before sending requests to Facebook' });
  }

  function setFacebookStatus(request, sendResponse) {
    if (!localStorage.accessToken || !request.msg)
      return;

    var params = {};
    params.access_token = localStorage.accessToken;
    params.message = request.msg;

    var path = '/me/feed';

    var xhr = $.post(GRAPH_API_URL + path,
          params,
          function(pdata) {
            if (pdata.error) {
              var errorMsg = 'Unable to POST data to Facebook feed, url:' +
                  path + '\nError: ' + pdata.error.type +
                  ': ' + pdata.error.message;

              console.error(errorMsg);

              errorMsg += '\nTry to logout and login once again.';

              callOnError({ error: errorMsg });

              notifyObservingExtensions(
                {
                  type: 'operationFailed',
                  message: errorMsg
                }
              );
              return;
            }
            callOnSuccess(pdata);
          },
          'json');

    if (callOnError)
      xhr.callOnError = callOnError;

    function callOnSuccess() {
      sendResponse({ error: 'no' });
    }

    function callOnError() {
      sendResponse({ error: 'yes' });
    }

    return true;
  }

  function onRequestFriendList(request) {
    getFriendList();
    return false;
  }

  // -------------------------------------------------------------------------------
  // Private data
  var observingExtensionIds = [];
  var handlers = {
    //observe: onObserve,
    login: onLogin,
    logout: logout,
    sendChatMessage: onSendChatMessage,
    sendInvite: onSendInvite,
    graphApiCall: onGraphApiCall,
    fqlQuery: onFqlQuery,
    restApiCall: onRestApiCall,
    getFBUserNameByUid: onGetFBUserNameByUid,
    getMyUid: onGetMyUidForExternal,
    changeOwnStatus: onChangeOwnStatus,
    setFacebookStatusMessage: setFacebookStatus,
    forceFriendListSend: onRequestFriendList
  };

  return public;
})();
