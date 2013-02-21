window.options.init();
var current = window.options.current;

DesktopNotifications.controllerExtensionId = current.controllerExtensionId;
DesktopNotifications.start(current.refreshTime);

chrome.extension.onMessageExternal.addListener(function(request, sender, sendResponse) {
  if (!request.type)
    return;

  if (request.type == 'myUidAvailable') {
    DesktopNotifications.start(current.refreshTime);
  }
});
