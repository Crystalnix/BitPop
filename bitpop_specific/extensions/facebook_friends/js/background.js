var friendList = null;
var myUid = null;
var statuses = {};
var inboxData = null;

//chrome.extension.sendRequest(bitpop.CONTROLLER_EXTENSION_ID,
//  { type: 'observe',
//    extensionId: chrome.i18n.getMessage('@@extension_id')
//  });

(function () {
  if (chrome.browserAction)
    chrome.browserAction.onClicked.addListener(function (tab) {
      chrome.bitpop.facebookChat.getFriendsSidebarVisible(function(is_visible) {
        chrome.bitpop.facebookChat.setFriendsSidebarVisible(!is_visible);
        onSuppressChatChanged();
      });
    });
  else
    setTimeout(arguments.callee, 1000);
})();

chrome.extension.onRequestExternal.addListener(function (request, sender, sendResponse) {
  if (!request.type)
    return;

  if (request.type == 'myUidAvailable') {
      myUid = request.myUid;
      sendInboxRequest();
  } else if (request.type == 'friendListReceived') {
    friendList = request.data;
  } else if (request.type == 'loggedOut') {
    statuses = {};
    friendList = null;
  } else if (request.type == 'wentOffline') {
    statuses = {};
    friendList = null;
  } else if (request.type == 'userStatusChanged') {
    // send change status message: empty message body signals to only check
    // for status change
    chrome.bitpop.facebookChat.newIncomingMessage(request.uid, "",
        request.status, "");

    // set global variable storing each user status, reported by XMPP
    statuses[request.uid.toString()] = request.status;

  } else if (request.type == 'newMessage') {
    var msgDate = new Date();  // set 'now' as the message arrive time

    console.assert(myUid != null);

    bitpop.saveToLocalStorage(myUid, request.from,
      bitpop.preprocessMessageText(request.body),
      msgDate,
      false);

    var found = false;
    var vs = chrome.extension.getViews();
    for (var i = 0; i < vs.length; ++i) {
      if (vs[i].location.hash.slice(1) == request.from) {
        found = true;
        break;
      }
    }

    if (!found) {
      for (var i = 0; i < friendList.length; ++i) {
        if (friendList[i].uid == request.from) {
          // use status from fql result first,
          // then from xmpp server status update,
          // else set it to offline
          var status = null;
          if (friendList[i].online_presence !== null)
            status = friendList[i].online_presence;
          else if (statuses[friendList[i].uid.toString()])
            status = statuses[friendList[i].uid.toString()];
          else
            status = 'offline';

          chrome.bitpop.facebookChat.newIncomingMessage(
                             friendList[i].uid.toString(),
                             friendList[i].name,
                             status,
                             request.body);
          break;
        }
      }
    }
  }
});

function sendInboxRequest() {
  chrome.extension.sendRequest(bitpop.CONTROLLER_EXTENSION_ID,
    { type: 'graphApiCall',
      path: '/me/inbox',
      params: {}
    },
    function (response) {
      inboxData = response.data;
      replaceLocalHistory(inboxData);
    }
  );
}

function replaceLocalHistory(data) {
  console.assert(myUid != null);
  console.assert(data != null);

  for (var i = 0; i < data.length; i++) {
    var from_id = data[i].from.id;
    var to_ids = [];
    for (var j = 0; j < data[i].to.data.length; j++) {
      to_ids.push(data[i].to.data[j].id);
      if (to_ids[j] == myUid) // exclude my uid from to_ids list
        to_ids.pop();
    }

    if (to_ids.length > 1)
      continue;
    var jid = myUid + ':' + to_ids[0].toString();
    localStorage.removeItem(jid);

    localStorage[jid + '.thread_id'] = data.id;

    if (!data[i].comments || !data[i].comments.data)
      continue;

    if (data[i].comments.data.length < 20 && data[i].message) {
      bitpop.saveToLocalStorage(myUid, to_ids[0],
          bitpop.preprocessMessageText(data[i].message),
          0, // leave 0 as a timestamp
          from_id == myUid
      );
    }

    for (var j = 0; j < data[i].comments.data.length; j++) {
      bitpop.saveToLocalStorage(myUid, to_ids[0],
          bitpop.preprocessMessageText(data[i].comments.data[j].message),
          (new Date(data[i].comments.data[j].created_time)).getTime(),
          data[i].comments.data[j].from.id == myUid
      );
    }
  }
}

// ********************************************************
// facebook chat enable/disable functionality
//
// ********************************************************
//
// the list of tab ids which this extension is interested in.
// Mostly facebook.com tabs
var fbTabs = {};

var TRACE = 0;
var DEBUG = 1;
var INFO  = 2;
var logLevel = DEBUG;

function myLog( ) {
    if(logLevel <= DEBUG) {
        console.log(arguments);
    }
}

chrome.tabs.onRemoved.addListener(
    function(tab)
    {
        delete fbTabs[tab];
    }
);

function sendResponseToContentScript(sender, data, status, response)
{
    if(chrome.extension.lastError) {
        status = "error";
        response = chrome.extension.lastError;
    }
    myLog("Sending response ", data.action, data.id, status, response);
    sender({
        'action': data.action,
        id: data.id,
        status: status,
        response: response
    });
}

/**
 * extract the doman name from a URL
 */
function getDomain(url) {
   return url.match(/:\/\/(.[^/]+)/)[1];
}

function addFbFunctionality( )
{
    // add a listener to events coming from contentscript
    chrome.extension.onRequest.addListener(
        function(request, sender, sendResponse) {
            myLog("Received request ", request);
            if(request) {
                var data = JSON.parse(request);
                try {
                    if(data.action === 'chat' && data.friendId !== undefined) {
                        // chat request event
                        //fbfeed.openChatWindow(data.friendId, function(a) {
                        //    sendResponseToContentScript(sendResponse, data, "ok", a);
                        //});
                    } else if(data.action === 'shouldEnableFbJewelsAndChat' &&
                        data.userId !== undefined) {
                        // save the id of the tabs which want the Jewel/Chat enable/disable
                        // so that they can be informed when quite mode changes
                        fbTabs[sender.tab.id] = {value: 1, injected: true};
                        if(sender.tab.incognito) {
                            // if the broser is in incognito mode make a local decision
                            // no need to consult the native side.
                            var response =  {
                                    enableChat: true,
                                    enableJewels: true
                                };
                            sendResponseToContentScript(sendResponse, data, "ok", response);
                        } else {
                            chrome.bitpop.facebookChat.getFriendsSidebarVisible(function(is_visible) {
                              var response = null;
                              if (is_visible) {
                                response = {
                                  enableChat:   ffSettings.get('show_chat'),
                                  enableJewels: ffSettings.get('show_jewels')
                                };
                              }
                              else {
                                response = { enableChat:true, enableJewels:true };
                              }

                              sendResponseToContentScript(sendResponse, data,
                                                          "ok", response);

                            // fbfeed.shouldEnableFbJewelsAndChat(
                            //     data.userId,
                            //     sender.tab.incognito,
                            //     function(a) {
                            //         sendResponseToContentScript(sendResponse, data, "ok", a);
                            //     });
                            });
                        }
                    }
                    // else if(data.action === 'getPreference') {
                    //     rmPreferences.getPreference(data.prefName,
                    //         function(a) {
                    //             sendResponseToContentScript(sendResponse, data, "ok", a);
                    //         }
                    //     );
                    // }
                } catch(e) {
                    sendResponseToContentScript(sendResponse, data, "error", e);
                }
            }
        });

    // add listener to prefs/quite mode change
    // rmPreferences.notifications.onChanged.addListener(
    //     function(a) {
    //         var checkJewelAndChat = false;
    //         if('quiteMode' in a) {
    //             checkJewelAndChat = true;
    //         } else if('prefName' in a) {
    //             if(a.prefName == 'rockmelt.always_show_fb_chat' ||
    //                a.prefName == 'rockmelt.always_show_fb_jewels') {
    //                checkJewelAndChat = true;
    //             }
    //         }
    //         if(checkJewelAndChat) {
    //             // send notifications to the content script of all fb.com tabs
    //             for(var i in fbTabs) {
    //                 if(fbTabs[i].injected) {
    //                     var id = parseInt(i);
    //                     chrome.tabs.sendRequest(
    //                         id,
    //                         a,
    //                         function(responseData) {
    //                         // ignore nothing to do here.
    //                         });
    //                 }
    //             }
    //         }
    //     }
    //     );
}

function onSuppressChatChanged() {
  for(var i in fbTabs) {
    if(fbTabs[i].injected) {
        var id = parseInt(i);
        chrome.tabs.sendRequest(
            id,
            {},
            function(responseData) {
            // ignore nothing to do here.
            });
    }
  }
}

ffSettings.addEvent("show_chat", onSuppressChatChanged);
ffSettings.addEvent("show_jewels", onSuppressChatChanged);

addFbFunctionality();
