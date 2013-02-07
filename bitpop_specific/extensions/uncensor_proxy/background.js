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
  chrome.extension.sendRequest({ reason: 'settingsChanged' });
});

chrome.bitpop.prefs.globalProxyControl.onChange.addListener(function(details) {
  settings.set('proxy_control', globalControlTransform[+details.value])
  chrome.extension.sendRequest({ reason: 'settingsChanged' });
});

chrome.bitpop.prefs.showMessageForActiveProxy.onChange.addListener(function(details) {
  settings.set('proxy_active_message', details.value);
  chrome.extension.sendRequest({ reason: 'settingsChanged' });
});

function init() {
  chrome.bitpop.prefs.globalProxyControl.get({}, function(details) {
    settings.set('proxy_control', globalControlTransform[+details.value]);
    chrome.extension.sendRequest({ reason: 'settingsChanged' });
  });
  chrome.bitpop.prefs.showMessageForActiveProxy.get({}, function(details) {
    settings.set('proxy_active_message', details.value);
    chrome.extension.sendRequest({ reason: 'settingsChanged' });
  });
  chrome.bitpop.prefs.blockedSitesList.get({}, function(details) {
    if (details.value) {
      var domains = JSON.parse(details.value);
      settings.set('domains', domains);
      chrome.extension.sendRequest({ reason: 'settingsChanged' });
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

        chrome.extension.sendRequest({ reason: 'settingsChanged' });

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
          chrome.tabs.update(details.tabId, {
              url: navigate(details.url)
              });

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
                      '  url: "' + navigate(details.url) + '",' +
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

// Copyrights to the following code are going to the authors of the
// HideMyAss Chrome extension
var proxy_sites = {
  'random' : 'Random site',
  'hidemyass.com' : 'HideMyAss.com',
  'anon.me' : 'Anon.me',
  'anonr.com' : 'Anonr.com',
  'armyproxy.com' : 'ArmyProxy.com',
  'boratproxy.com' : 'BoratProxy.com',
  'browse.ms' : 'Browse.ms',
  'hidemy.info' : 'HideMy.info',
  'invisiblesurfing.com' : 'InvisibleSurfing.com',
  'kroxy.net' : 'Kroxy.net',
  'limitkiller.com' : 'Limitkiller.com',
  'nuip.net' : 'Nuip.net',
  'ourproxy.com' : 'Ourproxy.com',
  'proxrio.com' : 'Proxrio.com',
  'proxybuddy.com' : 'Proxybuddy.com',
  'proxymafia.net' : 'Proxymafia.net',
  'sitesurf.net' : 'Sitesurf.net',
  'texasproxy.com' : 'Texasproxy.com',
  'unblocked.org' : 'Unblocked.org',
  'unblock.biz' : 'Unblock.biz'
}

var svc_url_path = '/includes/process.php?action=update&idx=0&u=%url&obfuscation=%hash';
var proxy_sites_count = getSitesCount();

function navigate(url_to_hide) {
  var encoded_url,
      proxy_site,
      svc_url;

  if (!checkURLCredibility(url_to_hide)) {
    return;
  }
  encoded_url = encodeURIComponent(url_to_hide); //window.btoa(url_to_hide.substr(4)).replace(/=+$/, '');
  //proxy_site = getRandomSite();
  proxy_site = 'hidemyass.com';
  if (proxy_site == 'hidemyass.com') {
    var server = Math.ceil(5 * Math.random());
    svc_url = 'http://' + server + '.' + proxy_site;
  } else {
    svc_url = 'http://' + proxy_site;
  }
  svc_url += svc_url_path.replace('%url', encoded_url).replace('%hash', 2);

  return svc_url;
}

function checkURLCredibility(url) {
  var i, reg;
  if (!/^https?:/.test(url)) {
    return false;
  }
  for (i in proxy_sites) {
    if (i == 'random') {
      continue;
    }
    reg = RegExp('^https?:\/\/([^\/\.]+\.)?' + i);
    if (reg.test(url)) {
      return false;
    }
  }
  return true;
}

function getSitesCount() {
  var i, j;
  for (i in proxy_sites) {
    if (i != 'random') {
      j++;
    }
  }
  return j;
}

function getRandomSite() {
  var i, j, sites = [];
  for (i in proxy_sites) {
    if (i != 'random') {
      sites.push(i);
    }
  }
  return sites[Math.floor(sites.length * Math.random())];
}

init();