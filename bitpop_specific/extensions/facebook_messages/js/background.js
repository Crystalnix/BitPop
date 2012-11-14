var bitpop = {
  CONTROLLER_EXTENSION_ID: "igddmhdmkpkonlbfabbkkdoploafopcn"
};

cleanLocalStorage();

window.options.init();
var current = window.options.current;

var myUid = null;

setTimeout(
  function() {
    if (!myUid) {
      chrome.extension.sendRequest(
        bitpop.CONTROLLER_EXTENSION_ID,
        { type: 'getMyUid' },
        function(response) {
          myUid = response.id;

          DesktopNotifications.threads_unseen_before = [];
          DesktopNotifications.just_connected = true;

          DesktopNotifications.start(current.refreshTime);
        }
      );
    }
  },
  5000);

DesktopNotifications.controllerExtensionId = current.controllerExtensionId;
DesktopNotifications.start(current.refreshTime);

chrome.extension.onRequestExternal.addListener(function (request, sender, sendResponse) {
  if (!request.type)
    return;

  if (request.type == 'myUidAvailable') {
    myUid = request.myUid;

    DesktopNotifications.threads_unseen_before = [];
    DesktopNotifications.just_connected = true;

    DesktopNotifications.start(current.refreshTime);

  } else if (request.type == 'newMessage') {
    DesktopNotifications.received_cache.push({ body: request.body,
                                               from: request.from,
                                               timestamp: new Date()
                                               });
    if (DesktopNotifications.received_cache.length > 50)
      DesktopNotifications.received_cache.shift();
  } else if (request.type == 'loggedOut') {
    DesktopNotifications.just_connected = false;
  } else if (request.type == 'wentOffline' || request.type == 'chatIsIdle') {
    DesktopNotifications.threads_unseen_before = [];
    DesktopNotifications.just_connected = true;
    if (request.type == 'chatIsIdle')
      DesktopNotifications.stop();
  } else if (request.type == 'chatIsAvailable') {
    DesktopNotifications.start(current.refreshTime);
  }
});

