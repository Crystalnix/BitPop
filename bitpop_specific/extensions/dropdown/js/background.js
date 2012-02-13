var cache = {
  history: {},
  options: {
    sync: false
  }
};

function init() {
  initDefaultOptions();
  loadHistoryIntoCache();

  if (localStorage['sync'] == 'yes') {
    cache.options.sync = true;
    loadSyncId();
    attachSyncListeners();
  }

  //first start
  chrome.tabs.getAllInWindow(null, function(tabs) {
    for(var i=0;i<tabs.length;i++) {
      chrome.pageAction.show(tabs[i].id);
    }
  });

  chrome.tabs.onUpdated.addListener(function(tabId, changeInfo, tab) {
    if(changeInfo.status == "loading") {
      chrome.pageAction.show(tabId);
    }
  });

  readHistory();
  setInterval(function() {
    readHistory();
	}, 5*60*1000);
}

function initDefaultOptions() {
  //default options
  if(!localStorage["list_style"]) {
    localStorage["list_style"] = "double_title_url";
  }

  if(!localStorage["show_protocol"]) {
    localStorage["show_protocol"] = "no";
  }

  if(!localStorage["typed_only"]) {
    localStorage["typed_only"] = "yes";
  }

  if(!localStorage["link_num"]) {
    localStorage["link_num"] = "12";
  }

  if(!localStorage["primary_color"]) {
    localStorage["primary_color"] = "#858586";
  }

  if(!localStorage["secondary_color"]) {
    localStorage["secondary_color"] = "#A5B7A5";
  }

  if(!localStorage["hover_color"]) {
    localStorage["hover_color"] = "#CDE5FF";
  }

  if(!localStorage["border_color"]) {
    localStorage["border_color"] = "#F1F8FF";
  }

  if(!localStorage["background_color"]) {
    localStorage["background_color"] = "#FFFFFF";
  }

  if(!localStorage["middle"]) {
    localStorage["middle"] = "foreground";
  }

  if(!localStorage["width"]) {
    localStorage["width"] = "600";
  }

  if(!localStorage["ignore"]) {
    localStorage["ignore"] = JSON.stringify(new Array());
  }

  if (!localStorage['sync']) {
    localStorage['sync'] = 'no';
  }
}

function loadHistoryIntoCache() {
  if (localStorage['history']) {
    try {
      var hist = JSON.parse(localStorage['history']);
      if (typeof hist === 'SyncHistory') {
        // Debug code in case the encapsulated styles bug reappears
        console.trace();
        console.log(hist);
        cache.history = hist;
      }
      else {
        cache.history = new SyncHistory(hist);
      }
    }

    catch (e) {
      console.log(e);
      cache.history = new SyncHistory({});
      cache.history.persist();
    }
  }

  else {
    cache.history = new SyncHistory({});
    cache.history.persist();
  }
}

function readHistory() {
  chrome.history.search({
      text: "",
      startTime:(new Date()).getTime()-30*24*3600*1000,
      endTime: (new Date()).getTime(),
      maxResults:0
    }, function(items) {

    items.sort(function(a,b){return b.visitCount - a.visitCount;});

    var history = new Array();
    var ignoreList = JSON.parse(localStorage["ignore"]);

    for(var i=0;i<items.length;i++) {
      if(history.length < localStorage["link_num"]) {
        if(ignoreList.indexOf(items[i].url.toLowerCase()) == -1) {
          if(localStorage["typed_only"] == "no" || (localStorage["typed_only"] == "yes" && items[i].typedCount > 0)) {
            delete items[i].id;
            delete items[i].lastVisitTime;
            delete items[i].typedCount;
            history.push(items[i]);
          }
          if (localStorage["typed_only"] == "yes" && items[i].typedCount == 0) {
            chrome.history.getVisits({url:items[i].url}, function(results) {
              for (var counter in results) {
                if (results[counter].transition == 'typed') {
                  delete items[i].id;
                  delete items[i].lastVisitTime;
                  delete items[i].typedCount;
                  history.push(items[i]);
                  break;
                }
              }
            });
          }
        }
      } else {
        break;
      }
    }

    mergeHistory(history);
    cache.history.push();
  });
}

function saveOption(name, value) {
  cache.options[name] = value;
  localStorage[name] = value;
}

function saveHistory(data) {
  cache.history = new SyncHistory(data);
  cache.history.persist();
}

function mergeHistory(newHistory, oldHistory) {
  cache.history.merge(newHistory, oldHistory);
}

function SyncHistory(param) {
  this.history = param;
}

SyncHistory.prototype.get = function(index) {
  if (index === undefined)
    return this.history;
  else
    return this.history[index];
};

SyncHistory.prototype.persist = function() {
  var jsonString = JSON.stringify(this.history);
  localStorage['history'] = jsonString;
}

SyncHistory.prototype.merge = function(newHistory, oldHistory) {
  var wasUndefined = false;
  if (oldHistory === undefined) {
    wasUndefined = true;
    oldHistory = this.history;
  }

  var res = [];
  var i, j, k, m, n;
  i = 0;
  j = 0;
  k = 0;
  m = newHistory.length;
  n = oldHistory.length;
  while (i < m && j < n) {
    if (newHistory[i].visitCount >= oldHistory[j].visitCount) {
      res[k] = newHistory[i];
      i++;
    } else {
      res[k] = oldHistory[j];
      j++;
    }
    k++;
  }
  if (i < m) {
    for (var p = i; p < m; p++) {
      res[k] = newHistory[p];
      k++;
    }
  } else {
    for (var p = j; p < n; p++) {
      res[k] = oldHistory[p];
      k++;
    }
  }

  // remove duplicates
  var a = [];
  var l = res.length;
  for(i = 0; i < l; i++) {
    for(j = i + 1; j < l; j++) {
      // If res[i] is found later in the array
      if (res[i].url === res[j].url) {
        if (res[i].visitCount < res[j].visitCount)
          res[i].visitCount = res[j].visitCount;
        j = ++i;
      }
    }
    a.push(res[i]);
  }

  if (a.length > localStorage['link_num']) {
    a = a.splice(localStorage['link_num'], a.length - localStorage['link_num']);
  }

  if (wasUndefined) {
    this.history = a;
    this.persist();
  }

  return a;
};

/**
  * Syncs the styles object i.e. pushes changes to the bookmark url
  */
SyncHistory.prototype.push = function() {
  console.log('Pushing history to the cloud...');
  if (cache.options.sync)
    saveSyncData(this.history);
};

/**
  * Initialize the background page
  */
window.addEventListener('load', function() {
  init();
});
