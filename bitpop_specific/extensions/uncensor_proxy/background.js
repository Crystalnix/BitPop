var DOMAINS_UPDATE_INTERVAL = 24 /*hours*/ * 60 /*minutes factor*/ * 60 /*seconds factor*/ *
                              1000 /*milliseconds factor*/;
var UPDATED_NOTIFICATION_SHOW_TIME = 10 * 1000; /* 10 seconds */

String.prototype.endsWith = function(suffix) {
    return this.indexOf(suffix, this.length - suffix.length) !== -1;
};

var settings = new Store("settings", {
  "last_update_time": 0,
  "domains": [],
  "cached_domains": {},
  "country_code": '',
  "country_name": '',
  "proxy_control": 'use_auto',
  "proxy_active_message": true
});

var globalControlTransform = [ 'use_auto', 'never_use', 'ask_me'];

chrome.bitpop.prefs.blockedSitesList.onChange.addListener(function(details) {
  var domains = JSON.parse(details.value);
  settings.set('domains', domains);
  //chrome.extension.sendMessage({ reason: 'settingsChanged' });
});

chrome.bitpop.prefs.globalProxyControl.onChange.addListener(function(details) {
  settings.set('proxy_control', globalControlTransform[+details.value])
  //chrome.extension.sendMessage({ reason: 'settingsChanged' });
});

chrome.bitpop.prefs.showMessageForActiveProxy.onChange.addListener(function(details) {
  settings.set('proxy_active_message', details.value);
  //chrome.extension.sendMessage({ reason: 'settingsChanged' });
});

function init() {
  chrome.bitpop.prefs.globalProxyControl.get({}, function(details) {
    settings.set('proxy_control', globalControlTransform[+details.value]);
    //chrome.extension.sendMessage({ reason: 'settingsChanged' });
  });
  chrome.bitpop.prefs.showMessageForActiveProxy.get({}, function(details) {
    settings.set('proxy_active_message', details.value);
    //chrome.extension.sendMessage({ reason: 'settingsChanged' });
  });
  chrome.bitpop.prefs.blockedSitesList.get({}, function(details) {
    if (details.value) {
      var domains = JSON.parse(details.value);
      settings.set('domains', domains);
      //chrome.extension.sendMessage({ reason: 'settingsChanged' });
    }
  });


  (function() {
    if (Date.now() -
        settings.get('last_update_time') > DOMAINS_UPDATE_INTERVAL ||
        settings.get('domains').length == 0) {
      updateProxifiedDomains();
    }
    setTimeout(arguments.callee, 1000 * 60 * 60); // recheck once in hour
  })();

  chrome.tabs.onUpdated.addListener(onTabUpdated);
  chrome.webRequest.onBeforeRequest.addListener(
    onBeforeRequestListener,
    {
      types: [ "main_frame" ],
      urls: [ "*://*/*" ]
    },
    []
  );
  chrome.bitpop.onProxyDomainsUpdate.addListener(updateProxifiedDomains);

  var domainsAsk = [];
  function getEntriesForAsk()
  {
    var entries = "";
    for (var i = 0; i < domainsAsk.length; i++)
      entries += getEntryForDomain(domainsAsk[i].domain);
    return entries;
  }
  chrome.extension.onMessage.addListener(function(request, sender, sendResponse) {
    if (request && request.type && request.type == 'enableProxyForDomain') {
      if (request.domain) {
        domainsAsk.push({ tab_id: sender.tab.id, domain: request.domain });
        setProxyConfig(getAutoEntries() + getEntriesForAsk());
        sendResponse({});
        return true;
      }
    }
  });
  chrome.tabs.onRemoved.addListener(function (tab_id, remove_info) {
    var removed = false;
    for (var i = 0; i < domainsAsk.length; i++) {
      if (domainsAsk[i].tab_id == tab_id) {
        domainsAsk.splice(i, 1);
        removed = true;
      }
    }
    if (removed) {
      setProxyConfig(getAutoEntries() + getEntriesForAsk(request.domain));
    }
  });
}

function haveDomainsChanged(domains) {
  var changed = false;
  var old_domains = settings.get('domains');

  if (domains.length != old_domains.length) {
    changed = true;
  } else {
    for (var i = 0; i < domains.length; i++) {
      if (changed = changed ||
          domains[i] != old_domains[i].description)
        break;
    }
  }

  return changed;
}

function updateProxifiedDomains() {
  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4) {
      var response = JSON.parse(xhr.responseText);
      if (!response.domains)
        return;

      var domains = response.domains;
      if (haveDomainsChanged(domains)) {
        setDomains(domains);
        settings.set('country_code', response.country_code);
        settings.set('country_name', response.country_name);
        chrome.bitpop.prefs.ipRecognitionCountryName.set({ value: response.country_name });

        //chrome.extension.sendMessage({ reason: 'settingsChanged' });

        var notification = webkitNotifications.createNotification(
          // icon url - can be relative
          '48uncensorp.png',
          // notification title
          'Uncensor ISP',
          // notification body text
          'The list of domains to use proxy for, was updated successfully.' +
          ' Country detected is ' + response.country_name + '.'
        );
        notification.show();
        setTimeout(function() {
          notification.cancel();
        }, UPDATED_NOTIFICATION_SHOW_TIME);
      }

      settings.set('last_update_time', Date.now());
    }
  }
  xhr.open("GET", "http://tools.bitpop.com/service/uncensorp_domains",
           true);
  xhr.send();
}

function getAutoEntries() {
  var domains = settings.get('domains');
  var allowedDomainsLines = "";
  for (var i = 0; i < domains.length; i++) {
    if (domains[i].value == 'use_auto' ||
        (domains[i].value == 'use_global' && proxyControl == 'use_auto')) {
      allowedDomainsLines = getEntryForDomain(domains[i].description);
    }
  }
}

function getEntryForDomain(domain_name) {
  return "  if (host == '" + domain_name + "' || shExpMatch(host, '*." + domain_name + "'))\n" +
         "  {\n" +
         "    return 'PROXY 31.192.228.61:8228';\n" +
         "  }\n";
}

function setProxyConfig(domainEntriesPacString) {
  var config = {
    mode: "pac_script",
    pacScript: {
      data: "function FindProxyForURL(url, host) {\n" +
            domainEntriesPacString +
            "  return 'DIRECT';\n" +
            "}"
    }
  };
  chrome.proxy.settings.set({ value: config, scope: "regular" },
                            function() {});
}

function setDomains(newDomains) {
  var i;
  var oldDomains = settings.get('domains');
  var cachedDomains = settings.get('cached_domains');

  for (i = 0; i < oldDomains.length; i++) {
    if (oldDomains[i].value !== 'use_global') {
      cachedDomains[oldDomains[i].description] = oldDomains[i].value;
    }
  }
  settings.set('cached_domains', cachedDomains);

  var domains = [];
  for (i = 0; i < newDomains.length; i++) {
    var curDomain = newDomains[i];
    domains.push({
      description: curDomain,
      value: cachedDomains[curDomain] || 'use_global'
    });
  }
  settings.set('domains', domains);
  chrome.bitpop.prefs.blockedSitesList.set({ value: JSON.stringify(domains) });

  setProxyConfig(getAutoEntries());
}

function onBeforeRequestListener(details) {
  var uri = parseUri(details.url);
  var domains = settings.get('domains');
  if (!domains || domains.length == 0)
    return;
  for (var i = 0; i < domains.length; i++) {
    if (uri['host'].endsWith(domains[i].description)) {
      var proxyControl = null;
      if (domains[i].value == 'use_global') {
        proxyControl = settings.get('proxy_control').replace(/"/g, '');
      }
      else {
        proxyControl = domains[i].value;
      }

      switch (proxyControl) {
        case 'use_auto': {
          var updatedListener = function(tabId, changeInfo, tab) {
            if (changeInfo.status == 'complete' && tabId == details.tabId) {
              chrome.tabs.insertCSS(tab.id, { file: 'infobar.css' });
              chrome.tabs.executeScript(tab.id, {
                code: 'var bitpop_uncensor_proxy_options = {' +
                '  reason: "setJippi", url: "' + details.url + '" };'
              }, function() {
                chrome.tabs.executeScript(tab.id,
                                          { file: 'infobar_script.js' });
              });
              chrome.tabs.onUpdated.removeListener(arguments.callee);
            }
          };

          chrome.tabs.onUpdated.addListener(updatedListener);
          chrome.tabs.onRemoved.addListener(function(tabId, removeInfo) {
            if (tabId == details.tabId) {
              chrome.tabs.onUpdated.removeListener(updatedListener);
              chrome.tabs.onRemoved.removeListener(arguments.callee);
            }
          });
        }
        break;

        case 'never_use':
          return;  // do nothing

        case 'ask_me': {
          var updatedListener = function(tabId, changeInfo, tab) {
            if (changeInfo.status == 'complete' && tabId == details.tabId) {
              clearTimeout(siteLoadTimeout);

              chrome.tabs.insertCSS(tab.id, { file: 'infobar.css' });
              chrome.tabs.executeScript(tab.id, {
                code: 'var bitpop_uncensor_proxy_options = {' +
                      '  reason: "setAsk",' +
                      '  url: "' + details.url + '",' +
                      '  domain: "' + domains[i].description + '",' +
                      '  country_name: "' + settings.get('country_name') +
                      '" };'
                }, function () {
                  chrome.tabs.executeScript(tab.id,
                                            { file: 'infobar_script.js' });
                }
              );
              chrome.tabs.onUpdated.removeListener(arguments.callee);
            }
          };

          var siteLoadTimeout = setTimeout(function() {
              updatedListener(
                details.tabId,
                { status: 'complete' },
                { id: details.tabId }
              );
            },
            5000);

          chrome.tabs.onUpdated.addListener(updatedListener);
          chrome.tabs.onRemoved.addListener(function(tabId, removeInfo) {
            if (tabId == details.tabId) {
              clearTimeout(siteLoadTimeout);
              chrome.tabs.onUpdated.removeListener(updatedListener);
              chrome.tabs.onRemoved.removeListener(arguments.callee);
            }
          });
        }
        break;
      }
      break;
    }
  }
}

function onTabUpdated(tabId, changeInfo, tab) {
  var uri = parseUri(tab.url);
  var domains = settings.get('domains');
  if (!domains || domains.length == 0)
    return;
  for (var i = 0; i < domains.length; i++) {
    if (uri['host'].endsWith(domains[i].description)) {
      var proxyControl = null;
      if (domains[i].value == 'use_global') {
        proxyControl = settings.get('proxy_control').replace(/"/g, '');
      }
      else {
        proxyControl = domains[i].value;
      }

      switch (proxyControl) {
        case 'use_auto':
          if (changeInfo.status == 'loading') {
            chrome.tabs.update(tab.id, {
                url: navigate(tab, tab.url)
              }, function(tab) {
                if (!tab) { return; }

                setTimeout(function() {
                  chrome.tabs.get(tab.id, function(tab) {
                    if (!tab) { return; }

                    chrome.tabs.insertCSS(tab.id, { file: 'infobar.css' });
                    chrome.tabs.executeScript(tab.id, {
                      code: 'var bitpop_uncensor_proxy_options = {' +
                      '  reason: "setJippi" };'
                    }, function() {
                      chrome.tabs.executeScript(tab.id, { file: 'infobar_script.js' });
                    });
                  });
                }, 5000);
              }
            );
          }
          break;

        case 'never_use':
          return;  // do nothing

        case 'ask_me':
          if (changeInfo.status == 'loading') {
            chrome.tabs.insertCSS(tab.id, { file: 'infobar.css' });
            chrome.tabs.executeScript(tab.id, {
              code: 'var bitpop_uncensor_proxy_options = {' +
                    '  reason: "setAsk",' +
                    '  url: "' + navigate(tab, changeInfo.url || tab.url) + '",' +
                    '  domain: "' + domains[i].description + '",' +
                    '  country_name: "' + settings.get('country_name') +
                    '" };'
              }, function () {
                chrome.tabs.executeScript(tab.id, { file: 'infobar_script.js' });
              }
            );
          }
          break;
      }
      break;
    }
  }
}

init();
