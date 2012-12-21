String.prototype.endsWith = function(suffix) {
  return this.indexOf(suffix, this.length - suffix.length) !== -1;
};

String.prototype.format = function() {
  var formatted = this;
  for (var i = 0; i < arguments.length; i++) {
      var regexp = new RegExp('\\{'+i+'\\}', 'gi');
      formatted = formatted.replace(regexp, arguments[i]);
  }
  return formatted;
};

var prefsString = ("prefs" in localStorage) ? localStorage.prefs : "";
var prefs = (prefsString != "") ? JSON.parse(prefsString) : {};

function downloadFilterData() {
  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4) {
      var d = JSON.parse(xhr.responseText);
      var changed = false;
      var od = {};
      for (var x in d) {
        od[d[x]['srcDomain']] = d[x]['dstDomain'];
        if (!(d[x]['srcDomain'] in prefs.domainFilter))
          changed = true;
      }

      for (var x in prefs.domainFilter)
        if (!x in od)
          changed = true;

      if (changed) {
        var notification = webkitNotifications.createNotification(
          '48uncensor.png',  // icon url - can be relative
          chrome.i18n.getMessage("extName"),  // notification title
          chrome.i18n.getMessage("updateSuccessMsg")  // notification body text
        );
        notification.show();
        setTimeout(function() {
          notification.cancel();
        }, 5000);
      }

      prefs.domainFilter = od;

      for (var excDomain in prefs.domainExceptions) {
        if (!(excDomain in prefs.domainFilter)) {
          delete prefs.domainExceptions[excDomain];
        }
      }

      prefs.lastUpdate = Date.now();
      localStorage.prefs = JSON.stringify(prefs);
    }
  };
  xhr.open("GET", 'http://tools.bitpop.com/service/uncensor_domains', true);
  xhr.send();
}

function firstInitPrefs() {
  prefs = {};
  prefs.shouldRedirect = true;
  prefs.showMessage = true;
  prefs.notifyUpdate = true;
  prefs.domainFilter = {};
  prefs.domainExceptions = {};
  prefs.lastUpdate = 0;

  localStorage.prefs = JSON.stringify(prefs);
}

function checkAndUpdate() {
  if (Date.now() - prefs.lastUpdate > 1000 * 60 * 60 * 24)
    downloadFilterData();
  setTimeout("checkAndUpdate()", 1000 * 60 * 60);
}

// Called when the url of a tab changes.
      function redirectListener(tabId, changeInfo, tab) {
  if (!prefs || !prefs.shouldRedirect)
    return;

  var uri = parseUri(tab.url);
  for (var domain in prefs.domainFilter) {
    if (!(domain in prefs.domainExceptions) && uri["host"].endsWith(domain) &&
        prefs.showMessage && changeInfo.status == "loading") {

      uri['host'] = uri['host'].replace(new RegExp(domain+'$', ''), prefs.domainFilter[domain]);
      var newUri = reconstructUri(uri);
      chrome.tabs.update(tabId, {"url": newUri});

      var notification = webkitNotifications.createNotification(
        '48uncensor.png',  // icon url - can be relative
        chrome.i18n.getMessage("extName"),  // notification title
        chrome.i18n.getMessage("redirectedMsg").format(newUri)  // notification body text
      );
      notification.show();

      setTimeout(function() {
        notification.cancel();
      }, 5000);
    }
  }
      };

(function() {
  if (prefsString == "")
    firstInitPrefs();
  checkAndUpdate();

  window.addEventListener("storage", function(e) {
    if (e.key == "prefs" && e.newValue != "") {
      prefsString = e.newValue;
      prefs = JSON.parse(e.newValue);
    }
  }, false);

  // Listen for any changes to the URL of any tab.
  chrome.tabs.onUpdated.addListener(redirectListener);
})();
