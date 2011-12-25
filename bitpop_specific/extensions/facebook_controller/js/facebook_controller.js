/* ======================================================
 * Copyright (C) 2011 House of Life Property Ltd. All rights reserved.
 * Copyright (C) 2001 Crystalnix <vgachkaylo@crystalnix.com>
 *
 * @requires
 *   Strophe
 *   jQuery
 * ======================================================*/

var bitpop;
if (!bitpop)
  bitpop = {};
bitpop.FacebookController = (function() {
  // -------------------------------------------------------------------------------
  // Constants
  var BOSH_SERVER_URL = "http://tools.bitpop.com:5280";
  var FB_APPLICATION_ID = "190635611002798";
  var SUCCESS_URL = 'http://www.facebook.com/connect/login_success.html';
  var FB_LOGOUT_URL = 'https://www.facebook.com/logout.php';
  var LOGOUT_NEXT_URL = SUCCESS_URL + '#logout';
  var GRAPH_API_URL = 'https://graph.facebook.com';
  var FQL_API_URL = 'https://graph.facebook.com/fql';
  var REST_API_URL = 'https://api.facebook.com/method/';

  var FRIEND_LIST_UPDATE_INTERVAL = 1000 * 60;

  var FB_PERMISSIONS = 'xmpp_login,offline_access,' +
          'user_online_presence,friends_online_presence,manage_notifications';

  // Facebook error codes
  var FQL_ERROR = {
    API_EC_PARAM_ACCESS_TOKEN: 190
  };

  var IDS = {
    friends: "engefnlnhcgeegefndkhijjfdfbpbeah"
  };

  // -------------------------------------------------------------------------------
  // Public methods
  var public = {
    init: function() {
      chrome.tabs.onUpdated.addListener(onTabUpdated);
      chrome.extension.onRequestExternal.addListener(onRequest);

      setupAjaxErrorHandler();

      onObserve({ extensionId: 'engefnlnhcgeegefndkhijjfdfbpbeah' });
      // messages extension
      onObserve({ extensionId: 'dhcejgafhmkdfanoalflifpjimaaijda' });

      if (localStorage.accessToken) {
        notifyObservingExtensions({ type: 'accessTokenAvailable',
                                        accessToken: localStorage.accessToken });
        if (localStorage.myUid)
          onGotUid();
        else
          getMyUid();
      }
    }
  };

  // -------------------------------------------------------------------------------
  // Private methods
  function notifyObservingExtensions(notificationObject) {
    for (var i = 0; i < observingExtensionIds.length; i++)
      chrome.extension.sendRequest(observingExtensionIds[i], notificationObject);
  }

  function notifyFriendsExtension(notificationObject) {
    chrome.extension.sendRequest(IDS.friends, notificationObject);
  }

  function setupAjaxErrorHandler() {
    $.ajaxSetup({
      error:function(x,e){
        var errorMsg = null;

        if (x.status==0){
          errorMsg = 'You are offline!!\n Please Check Your Network.';
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
      }
    });
  }

  function connectToFacebookChat() {
    if (!connection)
      connection = new Strophe.Connection(BOSH_SERVER_URL + '/http-bind/');

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
    } else if (status == Strophe.Status.DISCONNECTING) {
      console.log('Strophe is disconnecting.');
    } else if (status == Strophe.Status.DISCONNECTED) {
      console.log('Strophe is disconnected.');
    } else if (status == Strophe.Status.CONNECTED) {
      console.log('Strophe is connected.');
      //console.log('Send a message to ' + connection.jid +
      //' to talk to me.');

      connection.addHandler(onMessage, null, 'message', null, null, null);
      connection.addHandler(onPresence, null, 'presence', null, null, null);
      connection.send($pres().tree());

      notifyFriendsExtension({ type: 'chatAvailable' });
    }
  }

  function onMessage(msg) {
    var to = msg.getAttribute('to');
    var from = msg.getAttribute('from');
    var type = msg.getAttribute('type');
    var elems = msg.getElementsByTagName('body');

    if (type == "chat" && elems.length > 0) {
      var body = elems[0];
      var msgText = Strophe.getText(body);
      var msgDate = new Date();

      var fromUid = null;
      var matches = from.match(/\-?(\d+)@.*/);
      if (matches.length == 2) {
        fromUid = matches[1];
      }

      notifyFriendsExtension({ type: 'newMessage', body: msgText, from: fromUid });

      /*
      var found = false;
      var vs = chrome.extension.getViews();
      for (var i = 0; i < vs.length; ++i) {
        if (vs[i].location.hash.slice(1) == fromUid) {
          found = true;
          break;
        }
      }

      if (found) {
        chrome.extension.sendRequest({ requestType: 'newMessage', body: msgText,
          from: from, ts: msgDate });
      } else {
        saveToLocalStorage(localStorage.myUid, fromUid,
          preprocessMessageText(msgText),
          msgDate,
          false);

        for (var i = 0; i < friendsCached.length; ++i) {
          if (friendsCached[i].uid == fromUid) {
            chrome.chromePrivate.newIncomingMessage(
                               friendsCached[i].uid.toString(),
                               friendsCached[i].name,
                               friendsCached[i].online_presence,
                               msgText);
            break;
          }
        }
      }
      */

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
      // var statusMap = {
      //   available: 'active',
      //   idle: 'idle',
      //   error: 'error',
      //   unavailable: 'offline'
      // };

      // var st = statusMap[type];
      // if (!st)
      //   st = 'offline';

      // chrome.chromePrivate.newIncomingMessage(fromUid, "", st, "");
    }

    // we must return true to keep the handler alive.
    // returning false would remove it after it finishes.
    return true;
  }

  function sendMessage(message, uidTo) {
    var to = '-' + uidTo + "@chat.facebook.com";

    if(message && to){
      var reply = $msg({
        to: to,
        type: 'chat'
      })
      .cnode(Strophe.xmlElement('body', message));
      connection.send(reply.tree());

      console.log('I sent ' + to + ': ' + message);
    }
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
    if (changeInfo.url && changeInfo.url.indexOf(SUCCESS_URL) == 0) {
      if (changeInfo.url == LOGOUT_NEXT_URL) {
        localStorage.removeItem('accessToken');
        localStorage.removeItem('myUid');
        notifyObservingExtensions({ type: 'loggedOut' });
        chrome.chromePrivate.loggedOutFacebookSession();
      } else if (!localStorage.accessToken) {
        var accessToken = accessTokenFromSuccessURL(changeInfo.url);
        if (!accessToken)
          console.warn('Could not get an access token from url %s',
              changeInfo.url);
        else {
          localStorage.accessToken = accessToken;
          notifyObservingExtensions({ type: 'accessTokenAvailable',
                                        accessToken: accessToken });

          getMyUid();
        }
      }

      chrome.tabs.remove(tabId);
    }
  }

  function getMyUid() {
    graphApiRequest('/me', { fields: 'id' }, function(data) {
            // do all hard initialization work
            // when got the user id
            //
            localStorage.myUid = data.id;
            onGotUid();
          });
  }

  function getFriendList() {
    if (localStorage.accessToken && localStorage.myUid) {
      var friendListQuery =
            'SELECT uid, name, pic_square, online_presence' +
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

  function login() {
    var url = "https://www.facebook.com/dialog/oauth?client_id=" +
        FB_APPLICATION_ID +
        "&response_type=token" +
        "&redirect_uri=" + SUCCESS_URL +
        '&display=popup' +
        '&scope=' + FB_PERMISSIONS;

    chrome.windows.create({ url: url, type: "popup", width: 400, height: 580 });
    // popupWindow = window.open(url, 'Login to Facebook',
    //     'height=580,width=400,toolbar=no,directories=no,status=no,' +
    //     'menubar=no,scrollbars=no,resizable=no,modal=yes');
  }

  function logout() {
    if (localStorage.accessToken) {
      var url = FB_LOGOUT_URL + '?next=' +
        escape(LOGOUT_NEXT_URL) +
        '&access_token=' + localStorage.accessToken;

      chrome.windows.create({ url: url, type: "popup",
                              top: 0, left: 0, width: 50, height: 50 });
    }
  }

  function onRequest(request, sender, sendResponse) {
    if (!request.type || !handlers[request.type]) {
      console.warn('Invalid request received');
      return;
    }
    handlers[request.type](request, sendResponse);
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

  function onLogin(request) {
    if (!localStorage.accessToken) // do not allow to login twice
      login();
  }

  function onSendChatMessage(request) {
    if (request.message && request.uidTo)
      sendMessage(request.message, request.uidTo);
  }

  function onGotUid() {
    notifyObservingExtensions({ type: 'myUidAvailable',
                                        myUid: localStorage.myUid });

    getFriendList();
    if (friendListRequestInterval)
      clearInterval(friendListRequestInterval);

    friendListRequestInterval = setInterval(getFriendList,
      FRIEND_LIST_UPDATE_INTERVAL);

    connectToFacebookChat();
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
          sendMessage(msg, request.uidTo);
          sendResponse({msg: msg});
        }
      }
    }
  }

  function onGraphApiCall(request, sendResponse) {
    if (localStorage.accessToken)
      graphApiRequest(request.path, request.params, sendResponse, sendResponse);
    else
      sendNotLoggedInResponse(sendResponse);
  }

  function onFqlQuery(request, sendResponse) {
    if (localStorage.accessToken)
      fqlRequest(request.query, sendResponse, sendResponse);
    else
      sendNotLoggedInResponse(sendResponse);
  }

  function onRestApiCall(request, sendResponse) {
    if (localStorage.accessToken)
      restApiCall(request.method, request.params, sendResponse, sendResponse);
    else
      sendNotLoggedInResponse(sendResponse);
  }

  function sendNotLoggedInResponse(sendResponse) {
    sendResponse({ error: 'Not logged in. Please login before sending requests to Facebook' });
  }
  // function onRequestFriendList(request, sendResponse) {
  //   if (friendListCached) {
  //     sendResponse({ data: friendListCached });
  //   }
  // }

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
    restApiCall: onRestApiCall
  //  requestFriendList: onRequestFriendList
  };

  var friendListCached = null;
  var friendListLastUpdateTime = null;
  var connection = null;
  var friendListRequestInterval = null;

  return public;
})();
